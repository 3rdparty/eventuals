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
#include "eventuals/stream.h"
#include "eventuals/then.h"
#include "eventuals/type-traits.h"
#include "stout/borrowed_ptr.h"
#include "uv.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

// Possible events to poll on; see 'operator|' defined below for the
// ability to combine events together.
enum class PollEvents {
  Readable = 1,
  Writable = 2,
  Disconnect = 4,
  Prioritized = 8,
};

////////////////////////////////////////////////////////////////////////

class EventLoop final : public Scheduler {
 public:
  // Moveable and Copyable.
  class Buffer final {
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
      if (this == &that) {
        return *this;
      }

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

  class Clock final : public stout::enable_borrowable_from_this<Clock> {
   public:
    Clock(const Clock&) = delete;

    Clock(EventLoop& loop)
      : loop_(loop) {}

    std::chrono::nanoseconds Now();

    auto Timer(std::chrono::nanoseconds&& nanoseconds);

    bool Paused();

    void Pause();

    void Resume();

    void Advance(const std::chrono::nanoseconds& nanoseconds);

    // Submits the given callback to be invoked when the clock is not
    // paused or the specified number of nanoseconds have been
    // advanced from the paused time.
    void Submit(
        Callback<void(const std::chrono::nanoseconds&)> callback,
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
    struct _Timer final {
      template <typename K_>
      struct Continuation final
        : public stout::enable_borrowable_from_this<Continuation<K_>> {
        Continuation(
            K_ k,
            stout::borrowed_ref<Clock> clock,
            std::chrono::nanoseconds&& nanoseconds)
          : clock_(std::move(clock)),
            nanoseconds_(std::move(nanoseconds)),
            context_(&clock_->loop(), "Timer (start/fail/stop)"),
            interrupt_context_(&clock_->loop(), "Timer (interrupt)"),
            k_(std::move(k)) {}

        Continuation(Continuation&& that)
          : clock_(std::move(that.clock_)),
            nanoseconds_(std::move(that.nanoseconds_)),
            context_(&clock_->loop(), "Timer (start/fail/stop)"),
            interrupt_context_(&clock_->loop(), "Timer (interrupt)"),
            k_(std::move(that.k_)) {
          CHECK(!that.started_ || !that.completed_) << "moving after starting";
          CHECK(!handler_);
        }

        ~Continuation() {
          CHECK(!started_ || closed_);
        }

        void Start() {
          // Clock is basically a "scheduler" for timers so we need to
          // "submit" a callback to be executed when the clock is not
          // paused which might be right away but might also be at
          // some later timer after a paused clock has been advanced
          // or unpaused.
          clock_->Submit(
              this->Borrow([this](const auto& nanoseconds) {
                // NOTE: need to update nanoseconds in the event the clock
                // was paused/advanced and the nanosecond count differs.
                nanoseconds_ = nanoseconds;

                loop().Submit(
                    this->Borrow([this]() {
                      if (!completed_) {
                        CHECK(!started_);
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
                                        continuation.k_.Fail(std::runtime_error(
                                            uv_strerror(continuation.error_)));
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
                            continuation.k_.Fail(std::runtime_error(
                                uv_strerror(continuation.error_)));
                          });
                        }
                      }
                    }),
                    context_);
              }),
              nanoseconds_);
        }

        template <typename Error>
        void Fail(Error&& error) {
          // TODO(benh): avoid allocating on heap by storing args in
          // pre-allocated buffer based on composing with Errors.
          using Tuple = std::tuple<decltype(this), Error>;
          auto tuple = std::make_unique<Tuple>(
              this,
              std::forward<Error>(error));

          // Submitting to event loop to avoid race with interrupt.
          loop().Submit(
              this->Borrow([tuple = std::move(tuple)]() mutable {
                std::apply(
                    [](auto* continuation, auto&&... args) {
                      if (!continuation->completed_) {
                        CHECK(!continuation->started_);
                        continuation->completed_ = true;
                        auto& k_ = continuation->k_;
                        k_.Fail(std::forward<decltype(args)>(args)...);
                      }
                    },
                    std::move(*tuple));
              }),
              context_);
        }

        void Stop() {
          // Submitting to event loop to avoid race with interrupt.
          loop().Submit(
              this->Borrow([this]() {
                if (!completed_) {
                  CHECK(!started_);
                  completed_ = true;
                  k_.Stop();
                }
              }),
              context_);
        }

        void Register(Interrupt& interrupt) {
          k_.Register(interrupt);

          handler_.emplace(&interrupt, this->Borrow([this]() {
            // NOTE: even though we execute the interrupt handler on
            // the event loop we will properly context switch to the
            // scheduling context that first created the timer because
            // we used 'RescheduleAfter()' in 'EventLoop::Close::Timer()'.
            loop().Submit(
                this->Borrow([this]() {
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
                        continuation.k_.Fail(std::runtime_error(
                            uv_strerror(continuation.error_)));
                      }
                    });
                  }
                }),
                interrupt_context_);
          }));

          // NOTE: we always install the handler in case 'Start()'
          // never gets called i.e., due to a paused clock.
          handler_->Install();
        }

       private:
        EventLoop& loop() {
          return clock_->loop();
        }

        // Adaptors to libuv functions.
        uv_timer_t* timer() {
          return &timer_;
        }

        uv_handle_t* handle() {
          return reinterpret_cast<uv_handle_t*>(&timer_);
        }

        stout::borrowed_ref<Clock> clock_;
        std::chrono::nanoseconds nanoseconds_ = std::chrono::nanoseconds(0);

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

        // NOTE: we store 'k_' as the _last_ member so it will be
        // destructed _first_ and thus we won't have any use-after-delete
        // issues during destruction of 'k_' if it holds any references or
        // pointers to any (or within any) of the above members.
        K_ k_;
      };

      struct Composable final {
        template <typename Arg>
        using ValueFrom = void;

        template <typename Arg, typename Errors>
        using ErrorsFrom = tuple_types_union_t<
            Errors,
            std::tuple<std::runtime_error>>;

        template <typename Arg, typename K>
        auto k(K k) && {
          return Continuation<K>(
              std::move(k),
              std::move(clock_),
              std::move(nanoseconds_));
        }

        stout::borrowed_ref<Clock> clock_;
        std::chrono::nanoseconds nanoseconds_ = std::chrono::nanoseconds(0);
      };
    };

    EventLoop& loop_;

    // Stores paused time, no time means clock is not paused.
    std::optional<std::chrono::nanoseconds> paused_;
    std::chrono::nanoseconds advanced_ = std::chrono::nanoseconds(0);

    struct Pending final {
      std::chrono::nanoseconds nanoseconds = std::chrono::nanoseconds(0);
      Callback<void(const std::chrono::nanoseconds&)> callback;
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
  ~EventLoop() override;

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
    } while (status != std::future_status::ready || waiters_.load() != nullptr);
  }

  void RunWhileWaiters() {
    do {
      in_event_loop_ = true;
      running_ = true;

      // NOTE: We use 'UV_RUN_NOWAIT' because we don't want to block on
      // I/O.
      uv_run(&loop_, UV_RUN_NOWAIT);

      running_ = false;
      in_event_loop_ = false;
    } while (waiters_.load() != nullptr);
  }

  // Interrupts the event loop; necessary to have the loop redetermine
  // an I/O polling timeout in the event that a timer was removed
  // while it was executing.
  void Interrupt();

  bool Continuable(const Scheduler::Context& context) override;

  void Submit(Callback<void()> callback, Scheduler::Context& context) override;

  void Clone(Context& child) override {}

  // Schedules the eventual for execution on the event loop thread.
  template <typename E>
  [[nodiscard]] auto Schedule(E e);

  template <typename E>
  [[nodiscard]] auto Schedule(std::string&& name, E e);

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

  // Returns a stream of 'PollEvents' for each invocation of stream
  // 'Next()' until requested to stop via 'Done()'.
  [[nodiscard]] auto Poll(int fd, PollEvents events);

 private:
  struct _WaitForSignal final {
    template <typename K_>
    struct Continuation final
      : public stout::enable_borrowable_from_this<Continuation<K_>> {
      Continuation(K_ k, EventLoop& loop, const int signum)
        : loop_(loop),
          signum_(signum),
          context_(&loop, "WaitForSignal (start/fail/stop)"),
          interrupt_context_(&loop, "WaitForSignal (interrupt)"),
          k_(std::move(k)) {}

      Continuation(Continuation&& that)
        : loop_(that.loop_),
          signum_(that.signum_),
          context_(&that.loop_, "WaitForSignal (start/fail/stop)"),
          interrupt_context_(&that.loop_, "WaitForSignal (interrupt)"),
          k_(std::move(that.k_)) {
        CHECK(!that.started_ || !that.completed_) << "moving after starting";
        CHECK(!handler_);
      }

      ~Continuation() {
        CHECK(!started_ || closed_);
      }

      void Start() {
        loop_.Submit(
            this->Borrow([this]() {
              if (!completed_) {
                CHECK(!started_);
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
                    continuation.k_.Fail(std::runtime_error(
                        uv_strerror(continuation.error_)));
                  });
                }
              }
            }),
            context_);
      }

      template <typename Error>
      void Fail(Error&& error) {
        // TODO(benh): avoid allocating on heap by storing args in
        // pre-allocated buffer based on composing with Errors.
        using Tuple = std::tuple<decltype(this), Error>;
        auto tuple = std::make_unique<Tuple>(
            this,
            std::forward<Error>(error));

        // Submitting to event loop to avoid race with interrupt.
        loop_.Submit(
            this->Borrow([tuple = std::move(tuple)]() {
              std::apply(
                  [](auto* continuation, auto&&... args) {
                    if (!continuation->completed_) {
                      CHECK(!continuation->started_);
                      continuation->completed_ = true;
                      auto& k_ = continuation->k_;
                      k_.Fail(std::forward<decltype(args)>(args)...);
                    }
                  },
                  std::move(*tuple));
            }),
            context_);
      }

      void Stop() {
        // Submitting to event loop to avoid race with interrupt.
        loop_.Submit(
            this->Borrow([this]() {
              if (!completed_) {
                CHECK(!started_);
                completed_ = true;
                k_.Stop();
              }
            }),
            context_);
      }

      void Register(class Interrupt& interrupt) {
        k_.Register(interrupt);

        handler_.emplace(&interrupt, [this]() {
          loop_.Submit(
              this->Borrow([this]() {
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
                      continuation.k_.Fail(std::runtime_error(
                          uv_strerror(continuation.error_)));
                    }
                  });
                }
              }),
              interrupt_context_);
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

      // NOTE: we store 'k_' as the _last_ member so it will be
      // destructed _first_ and thus we won't have any use-after-delete
      // issues during destruction of 'k_' if it holds any references or
      // pointers to any (or within any) of the above members.
      K_ k_;
    };

    struct Composable final {
      template <typename Arg>
      using ValueFrom = void;

      template <typename Arg, typename Errors>
      using ErrorsFrom = tuple_types_union_t<
          Errors,
          std::tuple<std::runtime_error>>;

      template <typename Arg, typename K>
      auto k(K k) && {
        return Continuation<K>(std::move(k), loop_, signum_);
      }

      EventLoop& loop_;
      const int signum_;
    };
  };

  struct _Poll final {
    // Ensure that we're using the same enum values as libuv so we can
    // pass it on unchanged.
    static_assert(
        PollEvents::Readable == static_cast<PollEvents>(UV_READABLE));
    static_assert(
        PollEvents::Writable == static_cast<PollEvents>(UV_WRITABLE));
    static_assert(
        PollEvents::Disconnect == static_cast<PollEvents>(UV_DISCONNECT));
    static_assert(
        PollEvents::Prioritized == static_cast<PollEvents>(UV_PRIORITIZED));

    template <typename K_>
    struct Continuation final
      : public TypeErasedStream,
        public stout::enable_borrowable_from_this<Continuation<K_>> {
      Continuation(K_ k, EventLoop& loop, int fd, PollEvents events)
        : loop_(loop),
          fd_(fd),
          events_(events),
          context_(&loop, "Poll (start/fail/stop)"),
          interrupt_context_(&loop, "Poll (interrupt)"),
          k_(std::move(k)) {}

      Continuation(Continuation&& that)
        : loop_(that.loop_),
          fd_(that.fd_),
          events_(that.events_),
          context_(&that.loop_, "Poll (start/fail/stop)"),
          interrupt_context_(&that.loop_, "Poll (interrupt)"),
          k_(std::move(that.k_)) {
        CHECK(!that.started_ || !that.completed_) << "moving after starting";
        CHECK(!handler_);
      }

      ~Continuation() {
        CHECK(!started_ || closed_);
      }

      void Start() {
        loop_.Submit(
            this->Borrow([this]() {
              if (!completed_) {
                CHECK(!started_);
                started_ = true;
#if !defined(_WIN32)
                error_ = uv_poll_init(loop_, poll(), fd_);
#else
                if (GetFileType(reinterpret_cast<HANDLE>(fd_))
                    == FILE_TYPE_PIPE) {
                  error_ = uv_poll_init_socket(loop_, poll(), fd_);
                } else {
                  error_ = uv_poll_init(loop_, poll(), fd_);
                }
#endif
                if (!error_) {
                  uv_handle_set_data(handle(), this);
                  k_.Begin(*this);
                } else {
                  completed_ = true;
                  closed_ = true;
                  k_.Fail(std::runtime_error(uv_strerror(error_)));
                }
              }
            }),
            context_);
      }

      void Next() override {
        loop_.Submit(
            this->Borrow([this]() {
              if (!completed_) {
                CHECK(started_);
                CHECK_EQ(this, handle()->data);
                error_ = uv_poll_start(
                    poll(),
                    static_cast<int>(events_),
                    [](uv_poll_t* poll, int status, int events) {
                      auto& continuation = *(Continuation*) poll->data;
                      CHECK_EQ(poll, continuation.poll());
                      CHECK_EQ(
                          &continuation,
                          continuation.handle()->data);
                      if (!continuation.completed_) {
                        // NOTE: we stop the callback each time so
                        // that an explicit call to 'Next()' must be
                        // called to get the next event otherwise
                        // libuv would just keep firing our callback
                        // as long as any of the events are still
                        // valid.
                        CHECK(!continuation.error_);
                        continuation.error_ = uv_poll_stop(poll);
                        if (status == 0 && !continuation.error_) {
                          continuation.k_.Body(static_cast<PollEvents>(events));
                        } else {
                          continuation.completed_ = true;
                          if (!continuation.error_) {
                            continuation.error_ = status;
                          }
                          uv_close(
                              continuation.handle(),
                              [](uv_handle_t* handle) {
                                auto& continuation =
                                    *(Continuation*) handle->data;
                                continuation.closed_ = true;
                                CHECK(continuation.error_);
                                continuation.k_.Fail(std::runtime_error(
                                    uv_strerror(continuation.error_)));
                              });
                        }
                      }
                    });

                if (error_) {
                  completed_ = true;
                  CHECK_EQ(this, handle()->data);
                  uv_close(handle(), [](uv_handle_t* handle) {
                    auto& continuation = *(Continuation*) handle->data;
                    continuation.closed_ = true;
                    CHECK(continuation.error_);
                    continuation.k_.Fail(std::runtime_error(
                        uv_strerror(continuation.error_)));
                  });
                }
              }
            }),
            context_);
      }

      void Done() override {
        loop_.Submit(
            this->Borrow([this]() {
              if (!completed_) {
                CHECK(started_);
                completed_ = true;
                if (uv_is_active(handle())) {
                  error_ = uv_poll_stop(poll());
                }
                CHECK_EQ(this, handle()->data);
                uv_close(handle(), [](uv_handle_t* handle) {
                  auto& continuation = *(Continuation*) handle->data;
                  continuation.closed_ = true;
                  if (!continuation.error_) {
                    continuation.k_.Ended();
                  } else {
                    continuation.k_.Fail(std::runtime_error(
                        uv_strerror(continuation.error_)));
                  }
                });
              }
            }),
            context_);
      }

      template <typename Error>
      void Fail(Error&& error) {
        // TODO(benh): avoid allocating on heap by storing args in
        // pre-allocated buffer based on composing with Errors.
        using Tuple = std::tuple<decltype(this), Error>;
        auto tuple = std::make_unique<Tuple>(
            this,
            std::forward<Error>(error));

        // Submitting to event loop to avoid race with interrupt.
        loop_.Submit(
            this->Borrow([tuple = std::move(tuple)]() {
              std::apply(
                  [](auto* continuation, auto&&... args) {
                    if (!continuation->completed_) {
                      CHECK(!continuation->started_);
                      continuation->completed_ = true;
                      auto& k_ = continuation->k_;
                      k_.Fail(std::forward<decltype(args)>(args)...);
                    }
                  },
                  std::move(*tuple));
            }),
            context_);
      }

      void Stop() {
        // Submitting to event loop to avoid race with interrupt.
        loop_.Submit(
            this->Borrow([this]() {
              if (!completed_) {
                CHECK(!started_);
                completed_ = true;
                k_.Stop();
              }
            }),
            context_);
      }

      void Register(class Interrupt& interrupt) {
        k_.Register(interrupt);

        handler_.emplace(&interrupt, [this]() {
          loop_.Submit(
              this->Borrow([this]() {
                if (!started_) {
                  CHECK(!completed_);
                  completed_ = true;
                  k_.Stop();
                } else if (!completed_) {
                  CHECK(started_);
                  completed_ = true;
                  CHECK(!error_);
                  if (uv_is_active(handle())) {
                    error_ = uv_poll_stop(poll());
                  }
                  CHECK_EQ(this, handle()->data);
                  uv_close(handle(), [](uv_handle_t* handle) {
                    auto& continuation = *(Continuation*) handle->data;
                    continuation.closed_ = true;
                    if (!continuation.error_) {
                      continuation.k_.Stop();
                    } else {
                      continuation.k_.Fail(std::runtime_error(
                          uv_strerror(continuation.error_)));
                    }
                  });
                }
              }),
              interrupt_context_);
        });

        // NOTE: we always install the handler in case 'Start()'
        // never gets called.
        handler_->Install();
      }

     private:
      // Adaptors to libuv functions.
      uv_poll_t* poll() {
        return &poll_;
      }

      uv_handle_t* handle() {
        return reinterpret_cast<uv_handle_t*>(&poll_);
      }

      EventLoop& loop_;
      int fd_;
      PollEvents events_;

      uv_poll_t poll_;

      bool started_ = false;
      bool completed_ = false;
      bool closed_ = false;

      int error_ = 0;

      // NOTE: we use 'context_' in each of 'Start()', 'Fail()', and
      // 'Stop()' because only one of them will called at runtime.
      Scheduler::Context context_;
      Scheduler::Context interrupt_context_;

      std::optional<Interrupt::Handler> handler_;

      // NOTE: we store 'k_' as the _last_ member so it will be
      // destructed _first_ and thus we won't have any use-after-delete
      // issues during destruction of 'k_' if it holds any references or
      // pointers to any (or within any) of the above members.
      K_ k_;
    };

    struct Composable final {
      template <typename Arg>
      using ValueFrom = PollEvents;

      template <typename Arg, typename Errors>
      using ErrorsFrom = tuple_types_union_t<
          Errors,
          std::tuple<std::runtime_error>>;

      template <typename Arg, typename K>
      auto k(K k) && {
        return Continuation<K>(std::move(k), loop_, fd_, events_);
      }

      EventLoop& loop_;
      int fd_;
      PollEvents events_;
    };
  };

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

struct _EventLoopSchedule final {
  template <typename K_, typename E_, typename Arg_>
  struct Continuation final
    : public stout::enable_borrowable_from_this<Continuation<K_, E_, Arg_>> {
    Continuation(K_ k, E_ e, EventLoop* loop, std::string&& name)
      : e_(std::move(e)),
        context_(
            CHECK_NOTNULL(loop),
            std::move(name)),
        k_(std::move(k)) {}

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
        auto previous = Scheduler::Context::Switch(context_->Borrow());
        adapted_->Start(std::forward<Args>(args)...);
        previous = Scheduler::Context::Switch(std::move(previous));
        CHECK_EQ(previous.get(), context_.get());
      } else {
        if constexpr (!std::is_void_v<Arg_>) {
          arg_.emplace(std::forward<Args>(args)...);
        }

        loop()->Submit(
            this->Borrow([this]() {
              Adapt();
              if constexpr (sizeof...(args) > 0) {
                adapted_->Start(std::move(*arg_));
              } else {
                adapted_->Start();
              }
            }),
            *context_);
      }
    }

    template <typename Error>
    void Fail(Error&& error) {
      // NOTE: rather than skip the scheduling all together we make sure
      // to support the use case where code wants to "catch" a failure
      // inside of a 'Schedule()' in order to either recover or
      // propagate a different failure.
      if (loop()->InEventLoop()) {
        Adapt();
        auto previous = Scheduler::Context::Switch(context_->Borrow());
        adapted_->Fail(std::forward<Error>(error));
        previous = Scheduler::Context::Switch(std::move(previous));
        CHECK_EQ(previous.get(), context_.get());
      } else {
        // TODO(benh): avoid allocating on heap by storing args in
        // pre-allocated buffer based on composing with Errors.
        using Tuple = std::tuple<decltype(this), Error>;
        auto tuple = std::make_unique<Tuple>(
            this,
            std::forward<Error>(error));

        loop()->Submit(
            this->Borrow([tuple = std::move(tuple)]() mutable {
              std::apply(
                  [](auto* schedule, auto&&... args) {
                    schedule->Adapt();
                    schedule->adapted_->Fail(
                        std::forward<decltype(args)>(args)...);
                  },
                  std::move(*tuple));
            }),
            *context_);
      }
    }

    void Stop() {
      // NOTE: rather than skip the scheduling all together we make
      // sure to support the use case where code wants to "catch" the
      // stop inside of a 'Schedule()' in order to do something
      // different.

      if (loop()->InEventLoop()) {
        Adapt();
        auto previous = Scheduler::Context::Switch(context_->Borrow());
        adapted_->Stop();
        previous = Scheduler::Context::Switch(std::move(previous));
        CHECK_EQ(previous.get(), context_.get());
      } else {
        loop()->Submit(
            this->Borrow([this]() {
              Adapt();
              adapted_->Stop();
            }),
            *context_);
      }
    }

    void Register(Interrupt& interrupt) {
      interrupt_ = &interrupt;
      k_.Register(interrupt);
    }

    void Adapt() {
      if (!adapted_) {
        // Save previous context (even if it's us).
        stout::borrowed_ref<Scheduler::Context> previous =
            Scheduler::Context::Get().reborrow();

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
                    Reschedule(std::move(previous))
                        .template k<Value_>(_Then::Adaptor<K_>{k_}))));

        if (interrupt_ != nullptr) {
          adapted_->Register(*interrupt_);
        }
      }
    }

    E_ e_;

    std::optional<
        std::conditional_t<!std::is_void_v<Arg_>, Arg_, Undefined>>
        arg_;

    // Need to store context using '_Lazy' because we need to be able to move
    // this class _before_ it's started and 'Context' is not movable.
    Lazy::Of<Scheduler::Context>::Args<
        EventLoop*,
        std::string>
        context_;

    Interrupt* interrupt_ = nullptr;

    using Value_ = typename E_::template ValueFrom<Arg_>;

    using Adapted_ = decltype(std::declval<E_>().template k<Arg_>(
        std::declval<_Reschedule::Composable>()
            .template k<Value_>(std::declval<_Then::Adaptor<K_>>())));

    std::unique_ptr<Adapted_> adapted_;

    // NOTE: we store 'k_' as the _last_ member so it will be
    // destructed _first_ and thus we won't have any use-after-delete
    // issues during destruction of 'k_' if it holds any references or
    // pointers to any (or within any) of the above members.
    K_ k_;
  };

  template <typename E_>
  struct Composable final {
    template <typename Arg>
    using ValueFrom = typename E_::template ValueFrom<Arg>;

    template <typename Arg, typename Errors>
    using ErrorsFrom = tuple_types_union_t<
        Errors,
        typename E_::template ErrorsFrom<Arg, Errors>>;

    template <typename Arg, typename K>
    auto k(K k) && {
      return Continuation<K, E_, Arg>(
          std::move(k),
          std::move(e_),
          loop_,
          std::move(name_));
    }

    E_ e_;
    EventLoop* loop_;
    std::string name_;
  };
};

////////////////////////////////////////////////////////////////////////

template <typename E>
[[nodiscard]] auto EventLoop::Schedule(E e) {
  return _EventLoopSchedule::Composable<E>{std::move(e), this};
}

////////////////////////////////////////////////////////////////////////
template <typename E>
[[nodiscard]] auto EventLoop::Schedule(std::string&& name, E e) {
  return _EventLoopSchedule::Composable<E>{std::move(e), this, std::move(name)};
}

////////////////////////////////////////////////////////////////////////

// Returns the default event loop's clock.
[[nodiscard]] inline auto& Clock() {
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

[[nodiscard]] inline auto EventLoop::Clock::Timer(
    std::chrono::nanoseconds&& nanoseconds) {
  // NOTE: we use a 'RescheduleAfter()' to ensure we use current
  // scheduling context to invoke the continuation after the timer has
  // fired (or was interrupted).
  return RescheduleAfter(
      _Timer::Composable{Borrow(), std::move(nanoseconds)});
}

////////////////////////////////////////////////////////////////////////

[[nodiscard]] inline auto EventLoop::WaitForSignal(int signum) {
  // NOTE: we use a 'RescheduleAfter()' to ensure we use current
  // scheduling context to invoke the continuation after the signal has
  // fired (or was interrupted).
  return RescheduleAfter(
      // TODO(benh): borrow 'this' so signal can't outlive a loop.
      _WaitForSignal::Composable{*this, signum});
}

////////////////////////////////////////////////////////////////////////

// Helpers for using bitmasks of 'PollEvents', e.g.,
// 'PollEvents::Readable | PollEvents::Writeable'.
[[nodiscard]] inline PollEvents operator|(PollEvents left, PollEvents right) {
  return static_cast<PollEvents>(
      static_cast<std::underlying_type<PollEvents>::type>(left)
      | static_cast<std::underlying_type<PollEvents>::type>(right));
}

[[nodiscard]] inline PollEvents operator&(PollEvents left, PollEvents right) {
  return static_cast<PollEvents>(
      static_cast<std::underlying_type<PollEvents>::type>(left)
      & static_cast<std::underlying_type<PollEvents>::type>(right));
}

////////////////////////////////////////////////////////////////////////

[[nodiscard]] inline auto EventLoop::Poll(int fd, PollEvents events) {
  // NOTE: we use a 'RescheduleAfter()' to ensure we use current
  // scheduling context to invoke the continuation after the poll has
  // fired (or was interrupted).
  return RescheduleAfter(
      // TODO(benh): borrow 'this' so poll can't outlive a loop.
      _Poll::Composable{*this, fd, events});
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
