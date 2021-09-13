#pragma once

#include <memory>

#include "stout/event-loop.h"
#include "stout/eventual.h"

////////////////////////////////////////////////////////////////////////

namespace stout {
namespace eventuals {

////////////////////////////////////////////////////////////////////////

inline auto Signal(stout::eventuals::EventLoop& loop, const int signum) {
  struct Data {
    Data(EventLoop& loop, const int signum)
      : loop(loop),
        signum(signum),
        interrupt(&loop, "Signal (interrupt)") {}

    EventLoop& loop;
    int signum;
    void* k = nullptr;
    uv_signal_t signal;

    EventLoop::Waiter interrupt;
  };

  return loop.Schedule(
      "Signal (start)",
      Eventual<int>()
          .context(Data{loop, signum})
          .start([](auto& data, auto& k) {
            using K = std::decay_t<decltype(k)>;

            data.k = static_cast<void*>(&k);
            data.signal.data = &data;

            auto status = uv_signal_init(data.loop, &(data.signal));
            if (status) {
              k.Fail(uv_err_name(status));
            } else {
              auto error = uv_signal_start_oneshot(
                  &(data.signal),
                  [](uv_signal_t* signal, int signum) {
                    auto& data = *static_cast<Data*>(signal->data);
                    uv_close((uv_handle_t*) (&data.signal), nullptr);
                    static_cast<K*>(data.k)->Start(signum);
                  },
                  data.signum);

              if (error) {
                uv_close((uv_handle_t*) (&data.signal), nullptr);
                k.Fail(uv_err_name(error));
              }
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
                  if (uv_is_active((uv_handle_t*) &data.signal)) {
                    auto error = uv_signal_stop(&data.signal);
                    uv_close((uv_handle_t*) (&data.signal), nullptr);
                    if (error) {
                      static_cast<K*>(data.k)->Fail(uv_strerror(error));
                    } else {
                      static_cast<K*>(data.k)->Stop();
                    }
                  } else {
                    uv_close((uv_handle_t*) &data.signal, nullptr);
                    static_cast<K*>(data.k)->Stop();
                  }
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
