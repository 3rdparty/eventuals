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

  std::scoped_lock lock(mutex_);

  for (auto& pending : pending_) {
    pending.start(pending.nanoseconds - advanced_);
  }

  pending_.clear();

  paused_.reset();
}

////////////////////////////////////////////////////////////////////////

void EventLoop::Clock::Advance(const std::chrono::nanoseconds& nanoseconds) {
  CHECK(Paused()) << "clock is not paused";

  advanced_ += nanoseconds;

  std::scoped_lock lock(mutex_);

  pending_.erase(
      std::remove_if(
          pending_.begin(),
          pending_.end(),
          [this](Pending& pending) {
            if (advanced_ >= pending.nanoseconds) {
              pending.start(std::chrono::nanoseconds(0));
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

EventLoop::EventLoop()
  : clock_(*this) {
  uv_loop_init(&loop_);

  uv_prepare_init(&loop_, &prepare_);

  prepare_.data = this;

  uv_prepare_start(&prepare_, [](uv_prepare_t* prepare) {
    ((EventLoop*) prepare->data)->Prepare();
  });

  uv_async_init(&loop_, &async_, nullptr);
}

////////////////////////////////////////////////////////////////////////

EventLoop::~EventLoop() {
  CHECK(!Running());

  uv_prepare_stop(&prepare_);
  uv_close((uv_handle_t*) &prepare_, nullptr);

  uv_close((uv_handle_t*) &async_, nullptr);

  // Run the event loop one last time to handle uv_close()'s.
  uv_run(&loop_, UV_RUN_NOWAIT);

  if (Alive()) {
    LOG(WARNING) << "destructing EventLoop with active handles: ";
    uv_print_all_handles(&loop_, stderr);
  }

  uv_loop_close(&loop_);
}

////////////////////////////////////////////////////////////////////////

void EventLoop::Run() {
  bool again = false;
  do {
    in_event_loop_ = true;
    running_ = true;
    again = uv_run(&loop_, UV_RUN_NOWAIT);
    running_ = false;
    in_event_loop_ = false;

    uv_unref((uv_handle_t*) &prepare_);
    uv_unref((uv_handle_t*) &async_);

    again = Alive();

    uv_ref((uv_handle_t*) &prepare_);
    uv_ref((uv_handle_t*) &async_);

    if (!again) {
      again = callbacks_.load(std::memory_order_relaxed) != nullptr;
    }
  } while (again);
}

////////////////////////////////////////////////////////////////////////

// Interrupts the event loop; necessary to have the loop redetermine
// an I/O polling timeout in the event that a timer was removed
// while it was executing.
void EventLoop::Interrupt() {
  auto error = uv_async_send(&async_);
  if (error) {
    LOG(FATAL) << uv_strerror(error);
  }
}

////////////////////////////////////////////////////////////////////////

void EventLoop::Invoke(Callback* callback) {
  if (InEventLoop()) {
    CHECK_NOTNULL(callback)->f(*this);
  } else {
    CHECK(callback->next == nullptr);
    while (!callbacks_.compare_exchange_weak(
        callback->next,
        callback,
        std::memory_order_release,
        std::memory_order_relaxed)) {}

    Interrupt();
  }
}

////////////////////////////////////////////////////////////////////////

void EventLoop::Prepare() {
  Callback* callback = nullptr;
  do {
  load:
    callback = callbacks_.load(std::memory_order_relaxed);

    if (callback != nullptr) {
      if (callback->next == nullptr) {
        if (!callbacks_.compare_exchange_weak(
                callback,
                nullptr,
                std::memory_order_release,
                std::memory_order_relaxed)) {
          goto load; // Try again.
        }
      } else {
        while (callback->next->next != nullptr) {
          callback = callback->next;
        }

        CHECK(callback->next != nullptr);

        auto* next = callback->next;
        callback->next = nullptr;
        callback = next;
      }

      CHECK_NOTNULL(callback);

      callback->f(*this);
    }
  } while (callback != nullptr);
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals
} // namespace stout

////////////////////////////////////////////////////////////////////////
