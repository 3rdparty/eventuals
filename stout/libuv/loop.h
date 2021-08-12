#pragma once

#include <algorithm>
#include <chrono>
#include <iostream>
#include <list>
#include <optional>

#include "glog/logging.h"
#include "stout/callback.h"
#include "uv.h"

using stout::Callback;

namespace stout {
namespace uv {

class Loop {
 public:
  enum RunMode {
    DEFAULT = 0,
    ONCE,
    NOWAIT
  };

  class Clock {
   public:
    Clock() = delete;
    Clock(const Clock&) = delete;

    Clock(Loop& loop)
      : loop_(loop) {}

    bool Paused() {
      return paused_.has_value();
    }

    void Pause() {
      CHECK(!Paused());

      CHECK_EQ(0, timers_active())
          << "pausing the clock with outstanding timers is unsupported";

      paused_.emplace(std::chrono::milliseconds(uv_now(loop_)));

      advanced_ = std::chrono::milliseconds(0);
    }

    void Resume() {
      CHECK(Paused());

      for (auto& timer : timers_) {
        timer.start(timer.milliseconds - advanced_);
      }

      timers_.clear();

      paused_.reset();
    }

    void Advance(const std::chrono::milliseconds& milliseconds) {
      CHECK(Paused());

      advanced_ += milliseconds;

      for (auto& timer : timers_) {
        if (timer.valid) {
          if (advanced_ >= timer.milliseconds) {
            timer.start(std::chrono::milliseconds(0));
            timer.valid = false;
          }
        }
      }

      timers_.erase(std::remove_if(timers_.begin(), timers_.end(), [](Timer& timer) {
                      return !timer.valid;
                    }),
                    timers_.end());
    }

    void Enqueue(const std::chrono::milliseconds& milliseconds, Callback<std::chrono::milliseconds> start) {
      CHECK(Paused());
      timers_.emplace_back(Timer{milliseconds + advanced_, std::move(start)});
    }

    size_t timers_active() {
      size_t num = timers_.size();

      auto walk_cb = [](uv_handle_t* handle, void* args) {
        size_t* num = (size_t*) args;
        if (handle->type == UV_TIMER && uv_is_active(handle)) {
          (*num)++;
        }
      };

      uv_walk(loop_, walk_cb, &num);

      return num;
    }

   private:
    Loop& loop_;
    std::optional<std::chrono::milliseconds> paused_; // stores paused time, no time means clock is not paused
    std::chrono::milliseconds advanced_;

    struct Timer {
      std::chrono::milliseconds milliseconds;
      Callback<std::chrono::milliseconds> start;
      bool valid = true;
    };

    std::list<Timer> timers_;
  };

  Loop()
    : clock_(*this) {
    uv_loop_init(&loop_);
  }

  Loop(const Loop&) = delete;

  ~Loop() {
    uv_loop_close(&loop_);
  }

  void run(const RunMode& run_mode = DEFAULT) {
    uv_run(&loop_, (uv_run_mode) run_mode);
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

} // namespace uv
} // namespace stout