#include "stout/event-loop.h"

////////////////////////////////////////////////////////////////////////

namespace stout {
namespace eventuals {

////////////////////////////////////////////////////////////////////////

bool EventLoop::Clock::Paused() {
  return paused_.has_value();
}

////////////////////////////////////////////////////////////////////////

void EventLoop::Clock::Pause() {
  CHECK(!Paused()) << "clock is already paused";

  // Make sure there aren't any started (i.e., active) timers.
  size_t timers = 0;

  uv_walk(
      loop_,
      [](uv_handle_t* handle, void* args) {
        size_t* timers = (size_t*) args;
        if (handle->type == UV_TIMER && uv_is_active(handle)) {
          (*timers)++;
        }
      },
      &timers);

  CHECK_EQ(0, timers)
      << "pausing the clock with outstanding timers is unsupported";

  paused_.emplace(Now());

  advanced_ = std::chrono::milliseconds(0);
}

////////////////////////////////////////////////////////////////////////

void EventLoop::Clock::Resume() {
  CHECK(Paused()) << "clock is not paused";

  for (auto& pending : pending_) {
    pending.start(pending.milliseconds - advanced_);
  }

  pending_.clear();

  paused_.reset();
}

////////////////////////////////////////////////////////////////////////

void EventLoop::Clock::Advance(const std::chrono::milliseconds& milliseconds) {
  CHECK(Paused()) << "clock is not paused";

  advanced_ += milliseconds;

  pending_.erase(
      std::remove_if(
          pending_.begin(),
          pending_.end(),
          [this](Pending& pending) {
            if (advanced_ >= pending.milliseconds) {
              pending.start(std::chrono::milliseconds(0));
              return true;
            } else {
              return false;
            }
          }),
      pending_.end());
}

////////////////////////////////////////////////////////////////////////

static EventLoop* loop = new EventLoop();

////////////////////////////////////////////////////////////////////////

EventLoop& EventLoop::Default() {
  return *CHECK_NOTNULL(loop);
}

////////////////////////////////////////////////////////////////////////

EventLoop& EventLoop::Default(EventLoop* replacement) {
  delete CHECK_NOTNULL(loop);
  loop = CHECK_NOTNULL(replacement);
  return Default();
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals
} // namespace stout

////////////////////////////////////////////////////////////////////////
