#pragma once

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

      paused_.emplace(uv_now(loop_));

      advanced_ = 0;
    }

    void Resume() {
      CHECK(Paused());

      for (auto& timer : timers_) {
        // uint64_t now = *paused_ + advanced_;
        if (timer.valid) {
          timer.start(timer.milliseconds - advanced_);
        }
      }

      timers_.clear();

      paused_.reset();
    }

    void Advance(uint64_t milliseconds) {
      CHECK(Paused());

      advanced_ += milliseconds;

      for (auto& timer : timers_) {
        // uint64_t now = *paused_ + advanced_;
        if (timer.valid) {
          if (advanced_ >= timer.milliseconds) {
            timer.start(0);
            // TODO(benh): ideally we remove the timer from 'timers_' but
            // for now we just invalidate it so we don't start it again.
            timer.valid = false;
          }
        }
      }
    }

    void Enqueue(uint64_t milliseconds, Callback<uint64_t> start) {
      CHECK(paused_);
      timers_.emplace_back(Timer{milliseconds + advanced_, std::move(start)});
    }

    size_t timers_active() {
      size_t num = 0;
      auto walk_cb = [](uv_handle_t* handle, void* args) {
        size_t* num = (size_t*) args;
        if (handle->type == UV_TIMER && uv_is_active(handle)) {
          (*num)++;
        }
      };

      uv_walk(loop_, walk_cb, &num);

      for (auto& timer : timers_) {
        if (timer.valid) {
          num++;
        }
      }
      return num;
    }

   private:
    Loop& loop_;
    std::optional<uint64_t> paused_; // stores paused time, no time means clock is not paused
    uint64_t advanced_;

    struct Timer {
      uint64_t milliseconds;
      Callback<uint64_t> start;
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