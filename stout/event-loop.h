#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <future>
#include <list>
#include <mutex>
#include <optional>

#include "stout/callback.h"
#include "stout/context.h"
#include "stout/eventual.h"
#include "uv.h"

////////////////////////////////////////////////////////////////////////

namespace stout {
namespace eventuals {

////////////////////////////////////////////////////////////////////////

class EventLoop {
 public:
  struct Callback {
    Callback& operator=(stout::Callback<EventLoop&> callback) {
      f = std::move(callback);
      return *this;
    }

    stout::Callback<EventLoop&> f;

    Callback* next = nullptr;
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

  // Getter/Setter for default event loop.
  //
  // NOTE: takes ownership of event loop pointer.
  static EventLoop& Default();
  static EventLoop& Default(EventLoop* replacement);

  EventLoop();
  EventLoop(const EventLoop&) = delete;
  ~EventLoop();

  void Run();

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

  void Invoke(Callback* callback);

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
  void Prepare();

  uv_loop_t loop_;
  uv_prepare_t prepare_;
  uv_async_t async_;

  std::atomic<bool> running_ = false;

  static inline thread_local bool in_event_loop_ = false;

  std::atomic<Callback*> callbacks_ = nullptr;

  Clock clock_;
};

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
    EventLoop& loop;
    std::chrono::nanoseconds nanoseconds;
    void* k = nullptr;
    uv_timer_t timer;
    bool started = false;
    bool completed = false;

    EventLoop::Callback start;
    EventLoop::Callback interrupt;
  };

  return Eventual<void>()
      .context(Data{loop_, nanoseconds})
      // TODO(benh): borrow 'this'.
      .start([this](auto& data, auto& k) mutable {
        using K = std::decay_t<decltype(k)>;

        CHECK(!data.started || data.completed)
            << "starting timer that hasn't completed";

        data.started = false;
        data.completed = false;

        data.k = &k;
        data.timer.data = &data;

        auto start = [&data](const auto& nanoseconds) {
          // NOTE: need to update nanoseconds in the event the clock
          // was paused/advanced and the nanosecond count differs.
          data.nanoseconds = nanoseconds;
          data.start = [&data](EventLoop& loop) {
            if (!data.completed) {
              auto error = uv_timer_init(loop, &data.timer);
              if (error) {
                data.completed = true;
                static_cast<K*>(data.k)->Fail(uv_strerror(error));
              } else {
                auto milliseconds =
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        data.nanoseconds);

                auto error = uv_timer_start(
                    &data.timer,
                    [](uv_timer_t* timer) {
                      auto& data = *(Data*) timer->data;
                      CHECK_EQ(timer, &data.timer);
                      if (!data.completed) {
                        data.completed = true;
                        uv_close((uv_handle_t*) &data.timer, nullptr);
                        static_cast<K*>(data.k)->Start();
                      }
                    },
                    milliseconds.count(),
                    0);

                if (error) {
                  uv_close((uv_handle_t*) &data.timer, nullptr);
                  static_cast<K*>(data.k)->Fail(uv_strerror(error));
                } else {
                  data.started = true;
                }
              }
            }
          };

          data.loop.Invoke(&data.start);
        };

        if (!Paused()) { // TODO(benh): add 'unlikely()'.
          start(data.nanoseconds);
        } else {
          std::scoped_lock lock(mutex_);
          pending_.emplace_back(
              Pending{data.nanoseconds + advanced_, std::move(start)});
        }
      })
      .interrupt([](auto& data, auto& k) {
        using K = std::decay_t<decltype(k)>;
        data.interrupt = [&data](EventLoop&) {
          if (!data.started) {
            CHECK(!data.completed);
            data.completed = true;
            static_cast<K*>(data.k)->Stop();
          } else if (!data.completed) {
            data.completed = true;
            if (uv_is_active((uv_handle_t*) &data.timer)) {
              auto error = uv_timer_stop(&data.timer);
              uv_close((uv_handle_t*) &data.timer, nullptr);
              if (error) {
                static_cast<K*>(data.k)->Fail(uv_strerror(error));
              } else {
                static_cast<K*>(data.k)->Stop();
              }
            } else {
              uv_close((uv_handle_t*) &data.timer, nullptr);
              static_cast<K*>(data.k)->Stop();
            }
          }
        };

        data.loop.Invoke(&data.interrupt);
      });
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals
} // namespace stout

////////////////////////////////////////////////////////////////////////
