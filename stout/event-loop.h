#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <future>
#include <list>
#include <mutex>
#include <optional>

#include "stout/callback.h"
#include "stout/closure.h"
#include "stout/context.h"
#include "stout/eventual.h"
#include "stout/then.h"
#include "uv.h"

////////////////////////////////////////////////////////////////////////

namespace stout {
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

  struct Waiter : public Scheduler::Context {
   public:
    Waiter(EventLoop* loop, std::string&& name)
      : Scheduler::Context(loop),
        name_(std::move(name)) {}

    Waiter(Waiter&& that)
      : Scheduler::Context(that.scheduler()),
        name_(that.name_) {
      // NOTE: should only get moved before it's "started".
      CHECK(!that.waiting && !callback && next == nullptr);
    }

    const std::string& name() override {
      return name_;
    }

    EventLoop* loop() {
      return static_cast<EventLoop*>(scheduler());
    }

    bool waiting = false;
    Callback<> callback;
    Waiter* next = nullptr;

   private:
    std::string name_;
  };

  class Signal {
   public:
    Signal(EventLoop& loop) {
      handle_ = new uv_signal_t();
      CHECK_EQ(
          uv_signal_init(loop, handle_),
          0);
    }

    Signal(const Signal& that) = delete;

    Signal(Signal&& that) {
      handle_ = that.handle_;
      that.handle_ = nullptr;
    }

    ~Signal() {
      if (handle_ == nullptr) {
        return;
      }

      if (uv_is_active(reinterpret_cast<uv_handle_t*>(handle_))) {
        uv_signal_stop(handle_);
      }

      uv_close(base_handle(), [](uv_handle_t* handle) {
        delete handle;
      });
    }

    // Adaptors to libuv functions.
    uv_signal_t* handle() {
      CHECK_NOTNULL(handle_);
      return handle_;
    }

    uv_handle_t* base_handle() {
      CHECK_NOTNULL(handle_);
      return reinterpret_cast<uv_handle_t*>(handle_);
    }

   private:
    // NOTE: Determines the ownership over the handle.
    // If pointer is nullptr then it was transfered to another object.
    uv_signal_t* handle_ = nullptr;
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
            start_(&clock.loop(), "Timer (start)"),
            interrupt_(&clock.loop(), "Timer (interrupt)") {
          // NOTE: we need to heap allocate because when closing the timer
          // in the destructor the memory needs to remain valid until
          // _after_ the callback passed to 'uv_close()' gets executed!
          timer_ = new uv_timer_t();
          CHECK_EQ(0, uv_timer_init(loop(), timer()));
        }

        Continuation(Continuation&& that)
          : k_(std::move(that.k_)),
            clock_(that.clock_),
            nanoseconds_(std::move(that.nanoseconds_)),
            start_(&that.clock_.loop(), "Timer (start)"),
            interrupt_(&that.clock_.loop(), "Timer (interrupt)") {
          CHECK(!started_ || completed_) << "moving after starting";
          CHECK(!handler_);
          timer_ = that.timer_;
          that.timer_ = nullptr;
        }

        ~Continuation() {
          if (timer_ != nullptr) {
            if (uv_is_active(handle())) {
              uv_timer_stop(timer());
            }

            uv_close(handle(), [](uv_handle_t* handle) {
              delete handle;
            });
          }
        }

        void Start() {
          CHECK(!started_ || completed_)
              << "starting timer that hasn't completed";

          started_ = false;
          completed_ = false;

          uv_handle_set_data(handle(), this);

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
                        auto milliseconds =
                            std::chrono::duration_cast<
                                std::chrono::milliseconds>(
                                nanoseconds_);

                        auto error = uv_timer_start(
                            timer(),
                            [](uv_timer_t* timer) {
                              auto& continuation = *(Continuation*) timer->data;
                              CHECK_EQ(timer, continuation.timer());
                              if (!continuation.completed_) {
                                continuation.completed_ = true;
                                continuation.k_.Start();
                              }
                            },
                            milliseconds.count(),
                            0);

                        if (error) {
                          k_.Fail(uv_strerror(error));
                        } else {
                          started_ = true;
                        }
                      }
                    },
                    &start_);
              },
              nanoseconds_);
        }

        template <typename... Args>
        void Fail(Args&&... args) {
          k_.Fail(std::forward<Args>(args)...);
        }

        void Stop() {
          k_.Stop();
        }

        void Register(Interrupt& interrupt) {
          k_.Register(interrupt);

          handler_.emplace(&interrupt, [this]() {
            // NOTE: even though we execute the interrupt handler on
            // the event loop we will properly context switch to the
            // scheduling context that first created the timer because
            // we compose '_Timer::Composable' with 'Reschedule()' in
            // 'EventLoop::Close::Timer()'.
            loop().Submit(
                [this]() {
                  if (!started_) {
                    CHECK(!completed_);
                    completed_ = true;
                    k_.Stop();
                  } else if (!completed_) {
                    completed_ = true;
                    if (uv_is_active(handle())) {
                      auto error = uv_timer_stop(timer());
                      if (error) {
                        k_.Fail(uv_strerror(error));
                      } else {
                        k_.Stop();
                      }
                    } else {
                      k_.Stop();
                    }
                  }
                },
                &interrupt_);
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
          CHECK_NOTNULL(timer_);
          return timer_;
        }

        uv_handle_t* handle() {
          CHECK_NOTNULL(timer_);
          return reinterpret_cast<uv_handle_t*>(timer_);
        }

        K_ k_;
        Clock& clock_;
        std::chrono::nanoseconds nanoseconds_;

        // NOTE: Determines the ownership over the handle.
        // If pointer is nullptr then it was transfered to another object.
        uv_timer_t* timer_ = nullptr;

        bool started_ = false;
        bool completed_ = false;

        EventLoop::Waiter start_;
        EventLoop::Waiter interrupt_;

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

  void Run();
  void RunForever();

  template <typename T>
  void RunUntil(std::future<T>& future) {
    auto status = std::future_status::ready;
    do {
      Run();
      status = future.wait_for(std::chrono::nanoseconds::zero());
    } while (status != std::future_status::ready);
  }

  // Interrupts the event loop; necessary to have the loop redetermine
  // an I/O polling timeout in the event that a timer was removed
  // while it was executing.
  void Interrupt();

  bool Continuable(Scheduler::Context* context) override;

  void Submit(Callback<> callback, Scheduler::Context* context) override;

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

  bool InEventLoop() {
    return in_event_loop_;
  }

  operator uv_loop_t*() {
    return &loop_;
  }

  Clock& clock() {
    return clock_;
  }

 private:
  void Check();

  uv_loop_t loop_;
  uv_check_t check_;
  uv_async_t async_;

  std::atomic<bool> running_ = false;

  static inline thread_local bool in_event_loop_ = false;

  std::atomic<Waiter*> waiters_ = nullptr;

  Clock clock_;
};

////////////////////////////////////////////////////////////////////////

struct _EventLoopSchedule {
  template <typename K_, typename E_, typename Arg_>
  struct Continuation : public EventLoop::Waiter {
    // NOTE: explicit constructor because inheriting 'EventLoop::Waiter'.
    Continuation(K_ k, E_ e, EventLoop* loop, std::string&& name)
      : EventLoop::Waiter(loop, std::move(name)),
        k_(std::move(k)),
        e_(std::move(e)) {}

    template <typename... Args>
    void Start(Args&&... args) {
      static_assert(
          !std::is_void_v<Arg_> || sizeof...(args) == 0,
          "'Schedule' only supports 0 or 1 argument");

      if (loop()->InEventLoop()) {
        Adapt();
        auto* previous = Scheduler::Context::Switch(this);
        adaptor_->Start(std::forward<Args>(args)...);
        previous = Scheduler::Context::Switch(previous);
        CHECK_EQ(previous, this);
      } else {
        if constexpr (!std::is_void_v<Arg_>) {
          arg_.emplace(std::forward<Args>(args)...);
        }

        loop()->Submit(
            [this]() {
              Adapt();
              if constexpr (sizeof...(args) > 0) {
                adaptor_->Start(std::move(*arg_));
              } else {
                adaptor_->Start();
              }
            },
            this);
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
        auto* previous = Scheduler::Context::Switch(this);
        adaptor_->Fail(std::forward<Args>(args)...);
        previous = Scheduler::Context::Switch(previous);
        CHECK_EQ(previous, this);
      } else {
        // TODO(benh): avoid allocating on heap by storing args in
        // pre-allocated buffer based on composing with Errors.
        auto* tuple = new std::tuple{this, std::forward<Args>(args)...};

        loop()->Submit(
            [tuple]() {
              std::apply(
                  [](auto* schedule, auto&&... args) {
                    schedule->Adapt();
                    schedule->adaptor_->Fail(
                        std::forward<decltype(args)>(args)...);
                  },
                  std::move(*tuple));
              delete tuple;
            },
            this);
      }
    }

    void Stop() {
      // NOTE: rather than skip the scheduling all together we make
      // sure to support the use case where code wants to "catch" the
      // stop inside of a 'Schedule()' in order to do something
      // different.
      if (loop()->InEventLoop()) {
        Adapt();
        auto* previous = Scheduler::Context::Switch(this);
        adaptor_->Stop();
        previous = Scheduler::Context::Switch(previous);
        CHECK_EQ(previous, this);
      } else {
        loop()->Submit(
            [this]() {
              Adapt();
              adaptor_->Stop();
            },
            this);
      }
    }

    void Register(Interrupt& interrupt) {
      interrupt_ = &interrupt;
      k_.Register(interrupt);
    }

    void Adapt() {
      if (!adaptor_) {
        // Save previous context (even if it's us).
        Scheduler::Context* previous = Scheduler::Context::Get();

        adaptor_.reset(
            // NOTE: for now we're assuming usage of something like
            // 'jemalloc' so 'new' should use lock-free and thread-local
            // arenas. Ideally allocating memory during runtime should
            // actually be *faster* because the memory should have
            // better locality for the execution resource being used
            // (i.e., a local NUMA node). However, we should reconsider
            // this design decision if in practice this performance
            // tradeoff is not emperically a benefit.
            new Adaptor_(
                std::move(e_).template k<Arg_>(
                    Reschedule(previous).template k<Value_>(
                        detail::_Then::Adaptor<K_>{k_}))));

        if (interrupt_ != nullptr) {
          adaptor_->Register(*interrupt_);
        }
      }
    }

    K_ k_;
    E_ e_;

    std::optional<
        std::conditional_t<!std::is_void_v<Arg_>, Arg_, Undefined>>
        arg_;

    Interrupt* interrupt_ = nullptr;

    using Value_ = typename E_::template ValueFrom<Arg_>;

    using Adaptor_ = decltype(std::declval<E_>().template k<Arg_>(
        std::declval<detail::_Reschedule::Composable>()
            .template k<Value_>(std::declval<detail::_Then::Adaptor<K_>>())));

    std::unique_ptr<Adaptor_> adaptor_;
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
  // NOTE: we use a 'Closure' so that we can reschedule using the
  // existing context after the timer has fired (or was interrupted).
  //
  // TODO(benh): borrow 'this' so we avoid timers that outlive a clock.
  return Closure([this, nanoseconds]() mutable {
    Scheduler::Context* previous = Scheduler::Context::Get();
    return _Timer::Composable{*this, std::move(nanoseconds)}
    | Reschedule(previous);
  });
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals
} // namespace stout

////////////////////////////////////////////////////////////////////////
