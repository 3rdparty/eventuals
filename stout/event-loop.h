#pragma once

#include <algorithm>
#include <chrono>
#include <list>
#include <optional>

#include "glog/logging.h"
#include "stout/callback.h"
#include "stout/eventual.h"
#include "uv.h"

////////////////////////////////////////////////////////////////////////

namespace stout {
namespace eventuals {

////////////////////////////////////////////////////////////////////////

class EventLoop {
 public:
  class Clock {
   public:
    Clock(const Clock&) = delete;

    Clock(EventLoop& loop)
      : loop_(loop) {}

    std::chrono::milliseconds Now();

    auto Timer(const std::chrono::milliseconds& milliseconds);

    bool Paused();

    void Pause();

    void Resume();

    void Advance(const std::chrono::milliseconds& milliseconds);

   private:
    EventLoop& loop_;

    // Stores paused time, no time means clock is not paused.
    std::optional<std::chrono::milliseconds> paused_;
    std::chrono::milliseconds advanced_;

    struct Pending {
      std::chrono::milliseconds milliseconds;
      Callback<std::chrono::milliseconds> start;
    };

    std::list<Pending> pending_;
  };

  // Getter/Setter for default event loop.
  //
  // NOTE: takes ownership of event loop pointer.
  static EventLoop& Default();
  static EventLoop& Default(EventLoop* replacement);

  EventLoop()
    : clock_(*this) {
    uv_loop_init(&loop_);
  }

  EventLoop(const EventLoop&) = delete;

  ~EventLoop() {
    uv_loop_close(&loop_);
  }

  void Run() {
    uv_run(&loop_, UV_RUN_DEFAULT);
  }

  bool Alive() {
    return uv_loop_alive(&loop_);
  }

  operator uv_loop_t*() {
    return &loop_;
  }

  Clock& clock() {
    return clock_;
  }

 private:
  uv_loop_t loop_;
  Clock clock_;
};

////////////////////////////////////////////////////////////////////////

// Returns the default event loop's clock.
inline auto& Clock() {
  return EventLoop::Default().clock();
}

////////////////////////////////////////////////////////////////////////

inline std::chrono::milliseconds EventLoop::Clock::Now() {
  if (Paused()) { // TODO(benh): add 'unlikely()'.
    return *paused_ + advanced_;
  } else {
    return std::chrono::milliseconds(uv_now(loop_));
  }
}

////////////////////////////////////////////////////////////////////////

inline auto EventLoop::Clock::Timer(
    const std::chrono::milliseconds& milliseconds) {
  return Eventual<void>()
      .context(uv_timer_t())
      // TODO(benh): borrow 'this'.
      .start([this, milliseconds](auto& timer, auto& k) mutable {
        uv_timer_init(loop_, &timer);
        timer.data = &k;

        auto start = [&timer](std::chrono::milliseconds milliseconds) {
          uv_timer_start(
              &timer,
              [](uv_timer_t* timer) {
                eventuals::succeed(*(decltype(&k)) timer->data);
              },
              milliseconds.count(),
              0);
        };

        if (!Paused()) { // TODO(benh): add 'unlikely()'.
          start(milliseconds);
        } else {
          pending_.emplace_back(
              Pending{milliseconds + advanced_, std::move(start)});
        }
      });
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals
} // namespace stout

////////////////////////////////////////////////////////////////////////
