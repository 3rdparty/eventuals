#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <future>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <tuple>

#include "eventuals/callback.h"
#include "eventuals/closure.h"
#include "eventuals/eventual.h"
#include "eventuals/lazy.h"
#include "eventuals/then.h"
#include "uv.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

class EventLoop : public Scheduler {
 public:
  // Moveable and Copyable.
  class Buffer {
   public:
    Buffer() {
      buffer_ = uv_buf_init(nullptr, 0);
    }

    Buffer(const size_t& size) {
      data_.resize(size);
      buffer_ = uv_buf_init(const_cast<char*>(data_.data()), size);
    }

    Buffer(const std::string& string)
      : data_(string) {
      buffer_ = uv_buf_init(const_cast<char*>(data_.data()), data_.size());
    }

    Buffer(const Buffer& that) {
      data_ = that.data_;
      buffer_ = uv_buf_init(const_cast<char*>(data_.data()), data_.size());
    }

    Buffer(Buffer&& that) {
      data_ = std::move(that.data_);
      buffer_ = uv_buf_init(const_cast<char*>(data_.data()), data_.size());

      that.buffer_.len = 0;
      that.buffer_.base = nullptr;
    }

    Buffer& operator=(const std::string& string) {
      data_ = string;
      buffer_ = uv_buf_init(const_cast<char*>(data_.data()), data_.size());

      return *this;
    }

    Buffer& operator=(std::string&& string) {
      data_ = std::move(string);
      buffer_ = uv_buf_init(const_cast<char*>(data_.data()), data_.size());

      return *this;
    }

    Buffer& operator=(const Buffer& that) {
      data_ = that.data_;
      buffer_ = uv_buf_init(const_cast<char*>(data_.data()), data_.size());

      return *this;
    }

    Buffer& operator=(Buffer&& that) {
      data_ = std::move(that.data_);
      buffer_ = uv_buf_init(const_cast<char*>(data_.data()), data_.size());

      that.buffer_.len = 0;
      that.buffer_.base = nullptr;

      return *this;
    }

    Buffer& operator+=(const std::string& string) {
      data_ += string;
      buffer_ = uv_buf_init(const_cast<char*>(data_.data()), data_.size());

      return *this;
    }

    Buffer& operator+=(const Buffer& that) {
      data_ += that.data_;
      buffer_ = uv_buf_init(const_cast<char*>(data_.data()), data_.size());

      return *this;
    }

    ~Buffer() {}

    // Extracts the data from the buffer as a universal reference.
    // Empties out the buffer inside.
    std::string&& Extract() noexcept {
      buffer_.len = 0;
      buffer_.base = nullptr;

      return std::move(data_);
    }

    size_t Size() const noexcept {
      return data_.size();
    }

    void Resize(const size_t& size) {
      data_.resize(size, 0);
      buffer_ = uv_buf_init(const_cast<char*>(data_.data()), size);
    }

    // Used as an adaptor to libuv functions.
    operator uv_buf_t*() noexcept {
      return &buffer_;
    }

   private:
    // Used for performance purposes (SSO?)
    std::string data_ = "";

    // base - ptr to data; len - size of data
    uv_buf_t buffer_ = {};
  };

  class Clock {
   public:
    Clock(const Clock&) = delete;

    Clock(EventLoop& loop)
      : loop_(loop) {}

    std::chrono::nanoseconds Now();

    auto Timer(const std::chrono::nanoseconds& nanoseconds);

    bool Paused();

    void Pause();

    void Resume();

    void Advance(const std::chrono::nanoseconds& nanoseconds);

    // Submits the given callback to be invoked when the clock is not
    // paused or the specified number of nanoseconds have been
    // advanced from the paused time.
    void Submit(
        Callback<const std::chrono::nanoseconds&> callback,
        const std::chrono::nanoseconds& nanoseconds) {
      if (!Paused()) { // TODO(benh): add 'unlikely()'.
        callback(nanoseconds);
      } else {
        std::scoped_lock lock(mutex_);
        pending_.emplace_back(
            Pending{nanoseconds + advanced_, std::move(callback)});
      }
    }

    EventLoop& loop() {
      return loop_;
    }

   private:
    struct _Timer {
      template <typename K_>
      struct Continuation {
        Continuation(K_ k, Clock& clock, std::chrono::nanoseconds&& nanoseconds)
          : k_(std::move(k)),
            clock_(clock),
            nanoseconds_(std::move(nanoseconds)),
            context_(&clock.loop(), "Timer (start/fail/stop)"),
            interrupt_context_(&clock.loop(), "Timer (interrupt)") {}

        Continuation(Continuation&& that)
          : k_(std::move(that.k_)),
            clock_(that.clock_),
            nanoseconds_(std::move(that.nanoseconds_)),
            context_(&that.clock_.loop(), "Timer (start/fail/stop)"),
            interrupt_context_(&that.clock_.loop(), "Timer (interrupt)") {
          CHECK(!that.started_ || !that.completed_) << "moving after starting";
          CHECK(!handler_);
        }

        ~Continuation() {
          CHECK(!started_ || closed_);
        }

        void Start() {
          CHECK(!started_ && !completed_);

          // Clock is basically a "scheduler" for timers so we need to
          // "submit" a callback to be executed when the clock is not
          // paused which might be right away but might also be at
          // some later timer after a paused clock has been advanced
          // or unpaused.
          clock_.Submit(
              [this](const auto& nanoseconds) {
                // NOTE: need to update nanoseconds in the event the clock
                // was paused/advanced and the nanosecond count differs.
                nanoseconds_ = nanoseconds;

                loop().Submit(
                    [this]() {
                      if (!completed_) {
                        started_ = true;

                        CHECK_EQ(0, uv_timer_init(loop(), timer()));

                        uv_handle_set_data(handle(), this);

                        auto timeout = std::chrono::duration_cast<
                            std::chrono::milliseconds>(nanoseconds_);

                        CHECK(!error_);
                        error_ = uv_timer_start(
                            timer(),
                            [](uv_timer_t* timer) {
                              auto& continuation = *(Continuation*) timer->data;
                              CHECK_EQ(timer, continuation.timer());
                              CHECK_EQ(
                                  &continuation,
                                  continuation.handle()->data);
                              if (!continuation.completed_) {
                                continuation.completed_ = true;
                                CHECK(!continuation.error_);
                                continuation.error_ =
                                    uv_timer_stop(continuation.timer());
                                uv_close(
                                    continuation.handle(),
                                    [](uv_handle_t* handle) {
                                      auto& continuation =
                                          *(Continuation*) handle->data;
                                      continuation.closed_ = true;
                                      if (!continuation.error_) {
                                        continuation.k_.Start();
                                      } else {
                                        continuation.k_.Fail(
                                            uv_strerror(continuation.error_));
                                      }
                                    });
                              }
                            },
                            timeout.count(),
                            /* repeat = */ 0);

                        if (error_) {
                          completed_ = true;
                          CHECK_EQ(this, handle()->data);
                          uv_close(handle(), [](uv_handle_t* handle) {
                            auto& continuation = *(Continuation*) handle->data;
                            continuation.closed_ = true;
                            CHECK(continuation.error_);
                            continuation.k_.Fail(
                                uv_strerror(continuation.error_));
                          });
                        }
                      }
                    },
                    &context_);
              },
              nanoseconds_);
        }

        template <typename... Args>
        void Fail(Args&&... args) {
          // TODO(benh): avoid allocating on heap by storing args in
          // pre-allocated buffer based on composing with Errors.
          using Tuple = std::tuple<decltype(this), Args...>;
          auto tuple = std::make_unique<Tuple>(
              this,
              std::forward<Args>(args)...);

          // Submitting to event loop to avoid race with interrupt.
          loop().Submit(
              [tuple = std::move(tuple)]() mutable {
                std::apply(
                    [](auto* continuation, auto&&... args) {
                      auto& k_ = continuation->k_;
                      k_.Fail(std::forward<decltype(args)>(args)...);
                    },
                    std::move(*tuple));
              },
              &context_);
        }

        void Stop() {
          // Submitting to event loop to avoid race with interrupt.
          loop().Submit(
              [this]() {
                k_.Stop();
              },
              &context_);
        }

        void Register(Interrupt& interrupt) {
          k_.Register(interrupt);

          handler_.emplace(&interrupt, [this]() {
            // NOTE: even though we execute the interrupt handler on
            // the event loop we will properly context switch to the
            // scheduling context that first created the timer because
            // we used 'RescheduleAfter()' in 'EventLoop::Close::Timer()'.
            loop().Submit(
                [this]() {
                  if (!started_) {
                    CHECK(!completed_);
                    completed_ = true;
                    k_.Stop();
                  } else if (!completed_) {
                    CHECK(started_);
                    completed_ = true;
                    CHECK(!error_);
                    if (uv_is_active(handle())) {
                      error_ = uv_timer_stop(timer());
                    }
                    CHECK_EQ(this, handle()->data);
                    uv_close(handle(), [](uv_handle_t* handle) {
                      auto& continuation = *(Continuation*) handle->data;
                      continuation.closed_ = true;
                      if (!continuation.error_) {
                        continuation.k_.Stop();
                      } else {
                        continuation.k_.Fail(
                            uv_strerror(continuation.error_));
                      }
                    });
                  }
                },
                &interrupt_context_);
          });

          // NOTE: we always install the handler in case 'Start()'
          // never gets called i.e., due to a paused clock.
          handler_->Install();
        }

       private:
        EventLoop& loop() {
          return clock_.loop();
        }

        // Adaptors to libuv functions.
        uv_timer_t* timer() {
          return &timer_;
        }

        uv_handle_t* handle() {
          return reinterpret_cast<uv_handle_t*>(&timer_);
        }

        K_ k_;
        Clock& clock_;
        std::chrono::nanoseconds nanoseconds_;

        uv_timer_t timer_;

        bool started_ = false;
        bool completed_ = false;
        bool closed_ = false;

        int error_ = 0;

        // NOTE: we use 'context_' in each of 'Start()', 'Fail()', and
        // 'Stop()' because only one of them will called at runtime.
        Scheduler::Context context_;
        Scheduler::Context interrupt_context_;

        std::optional<Interrupt::Handler> handler_;
      };

      struct Composable {
        template <typename Arg>
        using ValueFrom = void;

        template <typename Arg, typename K>
        auto k(K k) && {
          return Continuation<K>{std::move(k), clock_, std::move(nanoseconds_)};
        }

        Clock& clock_;
        std::chrono::nanoseconds nanoseconds_;
      };
    };

    EventLoop& loop_;

    // Stores paused time, no time means clock is not paused.
    std::optional<std::chrono::nanoseconds> paused_;
    std::chrono::nanoseconds advanced_;

    struct Pending {
      std::chrono::nanoseconds nanoseconds;
      Callback<const std::chrono::nanoseconds&> callback;
    };

    // NOTE: using "blocking" synchronization here as pausing the
    // clock should only be done in tests.
    std::mutex mutex_;
    std::list<Pending> pending_;
  };

  // Getter/Resetter for default event loop.
  static EventLoop& Default();
  static void ConstructDefault();
  static void DestructDefault();

  static void ConstructDefaultAndRunForeverDetached();

  EventLoop();
  EventLoop(const EventLoop&) = delete;
  virtual ~EventLoop();

  void RunForever();

  template <typename T>
  void RunUntil(std::future<T>& future) {
    auto status = std::future_status::ready;
    do {
      in_event_loop_ = true;
      running_ = true;

      // NOTE: We use 'UV_RUN_NOWAIT' because we don't want to block on
      // I/O.
      uv_run(&loop_, UV_RUN_NOWAIT);

      running_ = false;
      in_event_loop_ = false;

      status = future.wait_for(std::chrono::nanoseconds::zero());
    } while (status != std::future_status::ready);
  }

  // Interrupts the event loop; necessary to have the loop redetermine
  // an I/O polling timeout in the event that a timer was removed
  // while it was executing.
  void Interrupt();

  bool Continuable(Scheduler::Context* context) override;

  void Submit(Callback<> callback, Scheduler::Context* context) override;

  void Clone(Context* child) override {}

  // Schedules the eventual for execution on the event loop thread.
  template <typename E>
  auto Schedule(E e);

  template <typename E>
  auto Schedule(std::string&& name, E e);

  bool Alive() {
    return uv_loop_alive(&loop_);
  }

  bool Running() {
    return running_.load();
  }

  static bool InEventLoop() {
    return in_event_loop_;
  }

  operator uv_loop_t*() {
    return &loop_;
  }

  Clock& clock() {
    return clock_;
  }

  auto WaitForSignal(int signum);

 private:
  struct _WaitForSignal {
    template <typename K_>
    struct Continuation {
      Continuation(K_ k, EventLoop& loop, const int signum)
        : k_(std::move(k)),
          loop_(loop),
          signum_(signum),
          context_(&loop, "WaitForSignal (start/fail/stop)"),
          interrupt_context_(&loop, "WaitForSignal (interrupt)") {}

      Continuation(Continuation&& that)
        : k_(std::move(that.k_)),
          loop_(that.loop_),
          signum_(that.signum_),
          context_(&that.loop_, "WaitForSignal (start/fail/stop)"),
          interrupt_context_(&that.loop_, "WaitForSignal (interrupt)") {
        CHECK(!that.started_ || !that.completed_) << "moving after starting";
        CHECK(!handler_);
      }

      ~Continuation() {
        CHECK(!started_ || closed_);
      }

      void Start() {
        CHECK(!started_ && !completed_);

        loop_.Submit(
            [this]() {
              if (!completed_) {
                started_ = true;

                CHECK_EQ(0, uv_signal_init(loop_, signal()));

                uv_handle_set_data(handle(), this);

                CHECK(!error_);
                error_ = uv_signal_start_oneshot(
                    signal(),
                    [](uv_signal_t* signal, int signum) {
                      auto& continuation = *(Continuation*) signal->data;
                      CHECK_EQ(signal, continuation.signal());
                      CHECK_EQ(
                          &continuation,
                          continuation.handle()->data);
                      CHECK_EQ(signum, continuation.signum_);
                      if (!continuation.completed_) {
                        continuation.completed_ = true;
                        uv_close(
                            continuation.handle(),
                            [](uv_handle_t* handle) {
                              auto& continuation =
                                  *(Continuation*) handle->data;
                              continuation.closed_ = true;
                              continuation.k_.Start();
                            });
                      }
                    },
                    signum_);

                if (error_) {
                  completed_ = true;
                  CHECK_EQ(this, handle()->data);
                  uv_close(handle(), [](uv_handle_t* handle) {
                    auto& continuation = *(Continuation*) handle->data;
                    continuation.closed_ = true;
                    CHECK(continuation.error_);
                    continuation.k_.Fail(
                        uv_strerror(continuation.error_));
                  });
                }
              }
            },
            &context_);
      }

      template <typename... Args>
      void Fail(Args&&... args) {
        // TODO(benh): avoid allocating on heap by storing args in
        // pre-allocated buffer based on composing with Errors.
        using Tuple = std::tuple<decltype(this), Args...>;
        auto tuple = std::make_unique<Tuple>(
            this,
            std::forward<Args>(args)...);

        // Submitting to event loop to avoid race with interrupt.
        loop_.Submit(
            [tuple = std::move(tuple)]() {
              std::apply(
                  [](auto* continuation, auto&&... args) {
                    auto& k_ = continuation->k_;
                    k_.Fail(std::forward<decltype(args)>(args)...);
                  },
                  std::move(*tuple));
            },
            &context_);
      }

      void Stop() {
        // Submitting to event loop to avoid race with interrupt.
        loop_.Submit(
            [this]() {
              k_.Stop();
            },
            &context_);
      }

      void Register(class Interrupt& interrupt) {
        k_.Register(interrupt);

        handler_.emplace(&interrupt, [this]() {
          loop_.Submit(
              [this]() {
                if (!started_) {
                  CHECK(!completed_);
                  completed_ = true;
                  k_.Stop();
                } else if (!completed_) {
                  CHECK(started_);
                  completed_ = true;
                  CHECK(!error_);
                  if (uv_is_active(handle())) {
                    error_ = uv_signal_stop(signal());
                  }
                  CHECK_EQ(this, handle()->data);
                  uv_close(handle(), [](uv_handle_t* handle) {
                    auto& continuation = *(Continuation*) handle->data;
                    continuation.closed_ = true;
                    if (!continuation.error_) {
                      continuation.k_.Stop();
                    } else {
                      continuation.k_.Fail(
                          uv_strerror(continuation.error_));
                    }
                  });
                }
              },
              &interrupt_context_);
        });

        // NOTE: we always install the handler in case 'Start()'
        // never gets called.
        handler_->Install();
      }

     private:
      // Adaptors to libuv functions.
      uv_signal_t* signal() {
        return &signal_;
      }

      uv_handle_t* handle() {
        return reinterpret_cast<uv_handle_t*>(&signal_);
      }

      K_ k_;
      EventLoop& loop_;
      const int signum_;

      uv_signal_t signal_;

      bool started_ = false;
      bool completed_ = false;
      bool closed_ = false;

      int error_ = 0;

      // NOTE: we use 'context_' in each of 'Start()', 'Fail()', and
      // 'Stop()' because only one of them will called at runtime.
      Scheduler::Context context_;
      Scheduler::Context interrupt_context_;

      std::optional<Interrupt::Handler> handler_;
    };

    struct Composable {
      template <typename Arg>
      using ValueFrom = void;

      template <typename Arg, typename K>
      auto k(K k) && {
        return Continuation<K>{std::move(k), loop_, signum_};
      }

      EventLoop& loop_;
      const int signum_;
    };
  };

  void Check();

  uv_loop_t loop_;
  uv_check_t check_;
  uv_async_t async_;

  std::atomic<bool> running_ = false;

  static inline thread_local bool in_event_loop_ = false;

  std::atomic<Scheduler::Context*> contexts_ = nullptr;

  Clock clock_;
};

////////////////////////////////////////////////////////////////////////

struct _EventLoopSchedule {
  template <typename K_, typename E_, typename Arg_>
  struct Continuation {
    Continuation(K_ k, E_ e, EventLoop* loop, std::string&& name)
      : k_(std::move(k)),
        e_(std::move(e)),
        context_(
            std::make_tuple(
                CHECK_NOTNULL(loop),
                std::move(name))) {}

    // To avoid casting default 'Scheduler*' to 'EventLoop*' each time.
    auto* loop() {
      return static_cast<EventLoop*>(context_->scheduler());
    }

    template <typename... Args>
    void Start(Args&&... args) {
      static_assert(
          !std::is_void_v<Arg_> || sizeof...(args) == 0,
          "'Schedule' only supports 0 or 1 argument");

      if (loop()->InEventLoop()) {
        Adapt();
        auto* previous = Scheduler::Context::Switch(context_.get());
        adapted_->Start(std::forward<Args>(args)...);
        previous = Scheduler::Context::Switch(previous);
        CHECK_EQ(previous, context_.get());
      } else {
        if constexpr (!std::is_void_v<Arg_>) {
          arg_.emplace(std::forward<Args>(args)...);
        }

        loop()->Submit(
            [this]() {
              Adapt();
              if constexpr (sizeof...(args) > 0) {
                adapted_->Start(std::move(*arg_));
              } else {
                adapted_->Start();
              }
            },
            context_.get());
      }
    }

    template <typename... Args>
    void Fail(Args&&... args) {
      // NOTE: rather than skip the scheduling all together we make sure
      // to support the use case where code wants to "catch" a failure
      // inside of a 'Schedule()' in order to either recover or
      // propagate a different failure.
      if (loop()->InEventLoop()) {
        Adapt();
        auto* previous = Scheduler::Context::Switch(context_.get());
        adapted_->Fail(std::forward<Args>(args)...);
        previous = Scheduler::Context::Switch(previous);
        CHECK_EQ(previous, context_.get());
      } else {
        // TODO(benh): avoid allocating on heap by storing args in
        // pre-allocated buffer based on composing with Errors.
        using Tuple = std::tuple<decltype(this), Args...>;
        auto tuple = std::make_unique<Tuple>(
            this,
            std::forward<Args>(args)...);

        loop()->Submit(
            [tuple = std::move(tuple)]() mutable {
              std::apply(
                  [](auto* schedule, auto&&... args) {
                    schedule->Adapt();
                    schedule->adapted_->Fail(
                        std::forward<decltype(args)>(args)...);
                  },
                  std::move(*tuple));
            },
            context_.get());
      }
    }

    void Stop() {
      // NOTE: rather than skip the scheduling all together we make
      // sure to support the use case where code wants to "catch" the
      // stop inside of a 'Schedule()' in order to do something
      // different.

      if (loop()->InEventLoop()) {
        Adapt();
        auto* previous = Scheduler::Context::Switch(context_.get());
        adapted_->Stop();
        previous = Scheduler::Context::Switch(previous);
        CHECK_EQ(previous, context_.get());
      } else {
        loop()->Submit(
            [this]() {
              Adapt();
              adapted_->Stop();
            },
            context_.get());
      }
    }

    void Register(Interrupt& interrupt) {
      interrupt_ = &interrupt;
      k_.Register(interrupt);
    }

    void Adapt() {
      if (!adapted_) {
        // Save previous context (even if it's us).
        Scheduler::Context* previous = Scheduler::Context::Get();

        adapted_.reset(
            // NOTE: for now we're assuming usage of something like
            // 'jemalloc' so 'new' should use lock-free and thread-local
            // arenas. Ideally allocating memory during runtime should
            // actually be *faster* because the memory should have
            // better locality for the execution resource being used
            // (i.e., a local NUMA node). However, we should reconsider
            // this design decision if in practice this performance
            // tradeoff is not emperically a benefit.
            new Adapted_(
                std::move(e_).template k<Arg_>(
                    Reschedule(previous).template k<Value_>(
                        _Then::Adaptor<K_>{k_}))));

        if (interrupt_ != nullptr) {
          adapted_->Register(*interrupt_);
        }
      }
    }

    K_ k_;
    E_ e_;

    std::optional<
        std::conditional_t<!std::is_void_v<Arg_>, Arg_, Undefined>>
        arg_;

    // Need to store context using '_Lazy' because we need to be able to move
    // this class _before_ it's started and 'Context' is not movable.
    _Lazy<
        Scheduler::Context,
        EventLoop*,
        std::string>
        context_;

    Interrupt* interrupt_ = nullptr;

    using Value_ = typename E_::template ValueFrom<Arg_>;

    using Adapted_ = decltype(std::declval<E_>().template k<Arg_>(
        std::declval<_Reschedule::Composable>()
            .template k<Value_>(std::declval<_Then::Adaptor<K_>>())));

    std::unique_ptr<Adapted_> adapted_;
  };

  template <typename E_>
  struct Composable {
    template <typename Arg>
    using ValueFrom = typename E_::template ValueFrom<Arg>;

    template <typename Arg, typename K>
    auto k(K k) && {
      return Continuation<K, E_, Arg>{
          std::move(k),
          std::move(e_),
          loop_,
          std::move(name_)};
    }

    E_ e_;
    EventLoop* loop_;
    std::string name_;
  };
};

////////////////////////////////////////////////////////////////////////

template <typename E>
auto EventLoop::Schedule(E e) {
  return _EventLoopSchedule::Composable<E>{std::move(e), this};
}

////////////////////////////////////////////////////////////////////////
template <typename E>
auto EventLoop::Schedule(std::string&& name, E e) {
  return _EventLoopSchedule::Composable<E>{std::move(e), this, std::move(name)};
}

////////////////////////////////////////////////////////////////////////

// Returns the default event loop's clock.
inline auto& Clock() {
  return EventLoop::Default().clock();
}

////////////////////////////////////////////////////////////////////////

inline std::chrono::nanoseconds EventLoop::Clock::Now() {
  if (Paused()) { // TODO(benh): add 'unlikely()'.
    return *paused_ + advanced_;
  } else {
    return std::chrono::nanoseconds(std::chrono::milliseconds(uv_now(loop_)));
  }
}

////////////////////////////////////////////////////////////////////////

inline auto EventLoop::Clock::Timer(
    const std::chrono::nanoseconds& nanoseconds) {
  // NOTE: we use a 'RescheduleAfter()' to ensure we use current
  // scheduling context to invoke the continuation after the timer has
  // fired (or was interrupted).
  return RescheduleAfter(
      // TODO(benh): borrow 'this' so timers can't outlive a clock.
      _Timer::Composable{*this, std::move(nanoseconds)});
}

////////////////////////////////////////////////////////////////////////

inline auto EventLoop::WaitForSignal(int signum) {
  // NOTE: we use a 'RescheduleAfter()' to ensure we use current
  // scheduling context to invoke the continuation after the signal has
  // fired (or was interrupted).
  return RescheduleAfter(
      // TODO(benh): borrow 'this' so signal can't outlive a loop.
      _WaitForSignal::Composable{*this, signum});
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
