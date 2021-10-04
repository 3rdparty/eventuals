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

      uv_close(
          reinterpret_cast<uv_handle_t*>(handle_),
          EventLoop::CloseCallback);
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

  class Timer {
   public:
    Timer(EventLoop& loop) {
      handle_ = new uv_timer_t();
      CHECK_EQ(
          uv_timer_init(loop, handle_),
          0);
    }

    Timer(const Timer& that) = delete;

    Timer(Timer&& that) {
      handle_ = that.handle_;
      that.handle_ = nullptr;
    }

    ~Timer() {
      if (handle_ == nullptr) {
        return;
      }

      if (uv_is_active(reinterpret_cast<uv_handle_t*>(handle_))) {
        uv_timer_stop(handle_);
      }

      uv_close(
          reinterpret_cast<uv_handle_t*>(handle_),
          EventLoop::CloseCallback);
    }

    // Adaptors to libuv functions.
    uv_timer_t* handle() {
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
    uv_timer_t* handle_ = nullptr;
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

   private:
    EventLoop& loop_;

    // Stores paused time, no time means clock is not paused.
    std::optional<std::chrono::nanoseconds> paused_;
    std::chrono::nanoseconds advanced_;

    struct Pending {
      std::chrono::nanoseconds nanoseconds;
      stout::Callback<const std::chrono::nanoseconds&> start;
    };

    // NOTE: using "blocking" synchronization here as pausing the
    // clock should only be done in tests.
    std::mutex mutex_;
    std::list<Pending> pending_;
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

  static void CloseCallback(uv_handle_t* handle) {
    delete handle;
  }

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
  // Helper struct storing multiple data fields.
  struct Data {
    Data(EventLoop& loop, std::chrono::nanoseconds nanoseconds)
      : loop(loop),
        nanoseconds(nanoseconds),
        timer(loop),
        start(&loop, "Timer (start)"),
        interrupt(&loop, "Timer (interrupt)") {}

    EventLoop& loop;
    std::chrono::nanoseconds nanoseconds;
    EventLoop::Timer timer;
    void* k = nullptr;
    bool started = false;
    bool completed = false;

    EventLoop::Waiter start;
    EventLoop::Waiter interrupt;
  };

  // NOTE: we use a 'Closure' so that we can reschedule using the
  // existing context after the timer has fired (or was interrupted).
  //
  // TODO(benh): consider creating a 'struct _Timer' if it simplifies
  // reasoning about this code or speeds up compile times.
  //
  // TODO(benh): borrow 'this' so we avoid timers that outlive a clock.
  return Closure([this, data = Data(loop_, nanoseconds)]() mutable {
    Scheduler::Context* previous = Scheduler::Context::Get();
    return Eventual<void>()
               .start([&](auto& k) mutable {
                 using K = std::decay_t<decltype(k)>;

                 CHECK(!data.started || data.completed)
                     << "starting timer that hasn't completed";

                 data.started = false;
                 data.completed = false;

                 data.k = &k;
                 uv_handle_set_data(data.timer.base_handle(), &data);

                 auto start = [&data](const auto& nanoseconds) {
                   // NOTE: need to update nanoseconds in the event the clock
                   // was paused/advanced and the nanosecond count differs.
                   data.nanoseconds = nanoseconds;

                   data.loop.Submit(
                       [&data]() {
                         if (!data.completed) {
                           auto milliseconds =
                               std::chrono::duration_cast<
                                   std::chrono::milliseconds>(
                                   data.nanoseconds);

                           auto error = uv_timer_start(
                               data.timer.handle(),
                               [](uv_timer_t* timer) {
                                 auto& data = *(Data*) timer->data;
                                 CHECK_EQ(timer, data.timer.handle());
                                 if (!data.completed) {
                                   data.completed = true;
                                   static_cast<K*>(data.k)->Start();
                                 }
                               },
                               milliseconds.count(),
                               0);

                           if (error) {
                             static_cast<K*>(data.k)->Fail(
                                 uv_strerror(error));
                           } else {
                             data.started = true;
                           }
                         }
                       },
                       &data.start);
                 };

                 if (!Paused()) { // TODO(benh): add 'unlikely()'.
                   start(data.nanoseconds);
                 } else {
                   std::scoped_lock lock(mutex_);
                   pending_.emplace_back(
                       Pending{data.nanoseconds + advanced_, std::move(start)});
                 }
               })
               .interrupt([&data](auto& k) {
                 using K = std::decay_t<decltype(k)>;

                 // NOTE: we need to save the continuation here in
                 // case we never started and set it above!
                 data.k = static_cast<void*>(&k);

                 data.loop.Submit(
                     [&data]() {
                       if (!data.started) {
                         CHECK(!data.completed);
                         data.completed = true;
                         static_cast<K*>(data.k)->Stop();
                       } else if (!data.completed) {
                         data.completed = true;
                         if (uv_is_active(data.timer.base_handle())) {
                           auto error = uv_timer_stop(data.timer.handle());
                           if (error) {
                             static_cast<K*>(data.k)->Fail(uv_strerror(error));
                           } else {
                             static_cast<K*>(data.k)->Stop();
                           }
                         } else {
                           static_cast<K*>(data.k)->Stop();
                         }
                       }
                     },
                     &data.interrupt);
               })
        | Reschedule(previous);
  });
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals
} // namespace stout

////////////////////////////////////////////////////////////////////////
