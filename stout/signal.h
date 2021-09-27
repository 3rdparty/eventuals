#pragma once

#include <memory>

#include "stout/event-loop.h"
#include "stout/eventual.h"

////////////////////////////////////////////////////////////////////////

namespace stout {
namespace eventuals {

////////////////////////////////////////////////////////////////////////

inline auto Signal(stout::eventuals::EventLoop& loop, const int& signum) {
  struct Data {
    Data(EventLoop& loop, const int& signum)
      : loop(loop),
        signum(signum),
        signal(loop),
        interrupt(&loop, "Signal (interrupt)") {}

    EventLoop& loop;
    int signum;
    EventLoop::Signal signal;

    void* k = nullptr;

    EventLoop::Waiter interrupt;
  };

  return loop.Schedule(
      "Signal (start)",
      Eventual<int>()
          .context(Data{loop, signum})
          .start([](auto& data, auto& k) {
            using K = std::decay_t<decltype(k)>;

            data.k = static_cast<void*>(&k);

            uv_handle_set_data(
                data.signal.base_handle(),
                &data);

            auto error = uv_signal_start_oneshot(
                data.signal.handle(),
                [](uv_signal_t* signal, int signum) {
                  auto& data = *static_cast<Data*>(signal->data);
                  static_cast<K*>(data.k)->Start(signum);
                },
                data.signum);

            if (error) {
              k.Fail(uv_err_name(error));
            }
          })
          .interrupt([](auto& data, auto& k) {
            using K = std::decay_t<decltype(k)>;

            // NOTE: we need to save the continuation here in
            // case we never started and set it above!
            data.k = static_cast<void*>(&k);

            // NOTE: the continuation 'k' should actually be a
            // 'Reschedule()' since we're using 'loop.Schedule()'
            // above which means that we'll properly switch to the
            // correct scheduler context just by using 'k'.

            data.loop.Submit(
                [&data]() {
                  static_cast<K*>(data.k)->Stop();
                },
                &data.interrupt);
          }));
}

////////////////////////////////////////////////////////////////////////

inline auto Signal(const int signum) {
  return Signal(EventLoop::Default(), signum);
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals
} // namespace stout

////////////////////////////////////////////////////////////////////////
