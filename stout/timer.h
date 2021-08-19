#pragma once

#include "stout/event-loop.h"
#include "stout/eventual.h"

namespace stout {
namespace eventuals {

auto Timer(EventLoop& loop, const std::chrono::milliseconds& milliseconds) {
  return Eventual<void>()
      .context(uv_timer_t())
      .start([&loop, milliseconds](auto& timer, auto& k) mutable {
        uv_timer_init(loop, &timer);
        timer.data = &k;

        auto start = [&timer](const std::chrono::milliseconds& milliseconds) {
          uv_timer_start(
              &timer,
              [](uv_timer_t* timer) {
                eventuals::succeed(*(decltype(&k)) timer->data);
              },
              milliseconds.count(),
              0);
        };

        if (!loop.clock().Paused()) {
          start(milliseconds);
        } else {
          loop.clock().Enqueue(milliseconds, std::move(start));
        }
      });
}

} // namespace eventuals
} // namespace stout
