#include "eventuals/event-loop.h"

#include <sstream>

////////////////////////////////////////////////////////////////////////

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

  // NOTE: We use 0u to suppress -Wsign-compare (on GCC)
  CHECK_EQ(0u, timers)
      << "pausing the clock with outstanding timers is unsupported";

  paused_.emplace(Now());

  advanced_ = std::chrono::milliseconds(0);
}

////////////////////////////////////////////////////////////////////////

void EventLoop::Clock::Resume() {
  CHECK(Paused()) << "clock is not paused";

  std::scoped_lock lock(mutex_);

  for (auto& pending : pending_) {
    pending.callback(pending.nanoseconds - advanced_);
  }

  pending_.clear();

  paused_.reset();

  // Now run the event loop in the event any waiters were enqueued and
  // should be invoked due to the clock having been resumed.
  loop_.RunWhileWaiters();
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
              pending.callback(std::chrono::nanoseconds(0));
              return true;
            } else {
              return false;
            }
          }),
      pending_.end());

  // Now run the event loop in the event any waiters were enqueued and
  // should be invoked due to the clock having been advanced.
  loop_.RunWhileWaiters();
}

////////////////////////////////////////////////////////////////////////

// NOTE: If loop is nullptr then memory isn't initialized.
static int8_t loop_memory[sizeof(EventLoop)] = {};
static EventLoop* loop = nullptr;

////////////////////////////////////////////////////////////////////////

EventLoop& EventLoop::Default() {
  if (!loop) {
    LOG(FATAL)
        << "\n"
        << "\n"
        << "****************************************************************\n"
        << "*  A default event loop has not yet been constructed!          *\n"
        << "*                                                              *\n"
        << "*  If you're seeing this message it probably means you forgot  *\n"
        << "*  to do 'EventLoop::ConstructDefault()' or possibly           *\n"
        << "*  'EventLoop::ConstructDefaultAndRunForeverDetached()'.       *\n"
        << "*                                                              *\n"
        << "*  If you're seeing this message coming from a test it means   *\n"
        << "*  you forgot to inherit from 'EventLoopTest'.                 *\n"
        << "*                                                              *\n"
        << "*  And don't forget that you not only need to construct the    *\n"
        << "*  event loop but you also need to run it!                     *\n"
        << "****************************************************************\n"
        << "\n";
  } else {
    return *loop;
  }
}

////////////////////////////////////////////////////////////////////////

void EventLoop::ConstructDefault() {
  CHECK(!loop) << "default already constructed";

  loop = new (loop_memory) EventLoop();
}

////////////////////////////////////////////////////////////////////////

void EventLoop::DestructDefault() {
  CHECK(loop) << "default not yet constructed";

  loop->~EventLoop();
  loop = nullptr;
}

////////////////////////////////////////////////////////////////////////

bool EventLoop::HasDefault() {
  return loop != nullptr;
}

////////////////////////////////////////////////////////////////////////

void EventLoop::ConstructDefaultAndRunForeverDetached() {
  ConstructDefault();

  auto thread = std::thread([]() {
    while (true) {
      EventLoop::Default().running_ = true;
      EventLoop::Default().in_event_loop_ = true;

      // NOTE: We use 'UV_RUN_NOWAIT' because we don't want to block on
      // I/O.
      uv_run(&EventLoop::Default().loop_, UV_RUN_NOWAIT);

      EventLoop::Default().running_ = false;
      EventLoop::Default().in_event_loop_ = false;
    }

    // We should never get out of run.
    LOG(FATAL) << "unreachable";
  });

  thread.detach();
}

////////////////////////////////////////////////////////////////////////

EventLoop::EventLoop()
  : clock_(*this) {
  uv_loop_init(&loop_);

  // NOTE: we use 'uv_check_t' instead of 'uv_prepare_t' because it
  // runs after the event loop has performed all of it's functionality
  // so we know that once 'Check()' has completed _and_ the loop is no
  // longer alive there shouldn't be any more work to do (with the
  // caveat that another thread can still 'Submit()' a callback at any
  // point in time that we may "miss" but there isn't anything we can
  // do about that except 'RunForever()' or application-level
  // synchronization).
  uv_check_init(&loop_, &check_);
  uv_check_init(&loop_, &asio_check_);

  check_.data = this;
  asio_check_.data = this;

  uv_check_start(&check_, [](uv_check_t* check) {
    ((EventLoop*) check->data)->Check();
  });

  uv_check_start(&asio_check_, [](uv_check_t* check) {
    ((EventLoop*) check->data)->AsioCheck();
  });

  // NOTE: we unreference 'check_' so that when we run the event
  // loop it's presence doesn't factor into whether or not the loop is
  // considered alive.
  uv_unref((uv_handle_t*) &check_);

  // TODO: we should consider event loop to be alive
  // while this check is running.
  // Before we find a better way to determine if there are
  // still pending jobs in the asio io_context,
  // we will just unreference this check.
  uv_unref((uv_handle_t*) &asio_check_);

  uv_async_init(&loop_, &async_, nullptr);

  // NOTE: see comments in 'RunUntil()' as to why we don't unreference
  // 'async_' like we do with 'check_'.
}

////////////////////////////////////////////////////////////////////////

EventLoop::~EventLoop() {
  CHECK(!Running());

  uv_check_stop(&check_);
  uv_close((uv_handle_t*) &check_, nullptr);

  uv_check_stop(&asio_check_);
  uv_close((uv_handle_t*) &asio_check_, nullptr);

  uv_close((uv_handle_t*) &async_, nullptr);

  // NOTE: ideally we can just run 'uv_run()' once now in order to
  // properly handle the 'uv_close()' calls we just made. Unfortunately
  // libuv has a peculiar behavior where if 'async_' has an
  // outstanding 'uv_async_send()' then we won't actually process the
  // 'uv_close()' call we just made for 'async_' with a single
  // 'uv_run()' but need to run 'uv_run()' at least twice.
  //
  // Moreover, it's possible that there are _other_ handles and/or
  // requests that are still referenced or active which should be
  // considered a bug since we're trying to destruct the event loop
  // here!
  //
  // To manage both of these requirements we run 'uv_run()' repeatedly
  // until the loop is no longer alive (i.e., no more referenced or
  // active handles/requests) and emit warnings every 100k
  // iterations. The reason we're not using something like
  // 'LOG_IF_EVERY_N()' is because we explicitly _don't_ want to emit
  // a warning the first time since the loop might still be alive only
  // because of the 'async_' situation explained above.
  static constexpr size_t ITERATIONS = 100000;
  size_t iterations = ITERATIONS;

  auto alive = Alive();

  CHECK(alive) << "should still have check and async handles to close";

  do {
    alive = uv_run(&loop_, UV_RUN_NOWAIT);

    if (alive && --iterations == 0) {
      std::ostringstream out;

      out << "destructing EventLoop with active handles:\n";

      // NOTE: we use 'uv_walk()' instead of 'uv_print_all_handles()'
      // so that we can use 'LOG(WARNING)' since
      // 'uv_print_all_handles()' requires a FILE*.
      uv_walk(
          &loop_,
          [](uv_handle_t* handle, void* arg) {
            auto& out = *static_cast<std::ostringstream*>(arg);

            out << "[";

            if (uv_has_ref(handle)) {
              out << "R";
            } else {
              out << "-";
            }

            if (uv_is_active(handle)) {
              out << "A";
            } else {
              out << "-";
            }

            // NOTE: internal handles are skipped by 'uv_walk()' by
            // default but since we're trying to mimic the output from
            // 'uv_print_all_handles()' we still insert an '-' here.
            out << "-";

            out << "] " << uv_handle_type_name(handle->type) << " " << handle;
          },
          &out);

      LOG(WARNING) << out.str();

      iterations = ITERATIONS;
    }
  } while (alive);

  CHECK_EQ(uv_loop_close(&loop_), 0);
}

////////////////////////////////////////////////////////////////////////

void EventLoop::RunForever() {
  while (true) {
    running_ = true;
    in_event_loop_ = true;

    // NOTE: We use 'UV_RUN_NOWAIT' because we don't want to block on
    // I/O.
    uv_run(&EventLoop::Default().loop_, UV_RUN_NOWAIT);

    running_ = false;
    in_event_loop_ = false;
  }

  LOG(FATAL) << "unreachable";
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

bool EventLoop::Continuable(const Scheduler::Context& context) {
  return InEventLoop();
}

////////////////////////////////////////////////////////////////////////

void EventLoop::Submit(Callback<void()> callback, Context& context) {
  CHECK(!context.blocked()) << context.name();

  CHECK_EQ(this, context.scheduler());

  context.block();

  Waiter* waiter = &context.waiter;

  waiter->context = context.Borrow();

  waiter->callback = std::move(callback);

  CHECK(waiter->next == nullptr) << context.name();

  waiter->next = waiters_.load(std::memory_order_relaxed);

  while (!waiters_.compare_exchange_weak(
      waiter->next,
      waiter,
      std::memory_order_release,
      std::memory_order_relaxed)) {}

  Interrupt();
}

////////////////////////////////////////////////////////////////////////

void EventLoop::Check() {
  Waiter* waiter = nullptr;
  do {
  load:
    waiter = waiters_.load(std::memory_order_relaxed);

    if (waiter != nullptr) {
      if (waiter->next == nullptr) {
        if (!waiters_.compare_exchange_weak(
                waiter,
                nullptr,
                std::memory_order_release,
                std::memory_order_relaxed)) {
          goto load; // Try again.
        }
      } else {
        while (waiter->next->next != nullptr) {
          waiter = waiter->next;
        }

        CHECK(waiter->next != nullptr);

        auto* next = waiter->next;
        waiter->next = nullptr;
        waiter = next;
      }

      CHECK_NOTNULL(waiter);

      Context* context = CHECK_NOTNULL(waiter->context.get());

      context->unblock();

      stout::borrowed_ref<Context> previous =
          Context::Switch(std::move(waiter->context).reference());

      CHECK(waiter->callback);

      Callback<void()> callback = std::move(waiter->callback);

      callback();

      ////////////////////////////////////////////////////
      // NOTE: can't use 'waiter' at this point in time //
      // because it might have been deallocated!        //
      ////////////////////////////////////////////////////

      CHECK_EQ(context, Context::Switch(std::move(previous)).get());
    }
  } while (waiter != nullptr);
}

////////////////////////////////////////////////////////////////////////

void EventLoop::AsioCheck() {
  io_context().restart();
  io_context().poll();
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
