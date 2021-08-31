#pragma once

#include <memory>

#include "stout/context.h"
#include "stout/event-loop.h"
#include "stout/eventual.h"

namespace stout {
namespace eventuals {

inline auto Signal(
    stout::eventuals::EventLoop& loop,
    const int signal_code) {
  struct Data {
    EventLoop& loop;
    int signal_code;
    void* k = nullptr;
    uv_signal_t signal;
    EventLoop::Callback start;
    EventLoop::Callback interrupt;
  };

  return stout::eventuals::Eventual<int>()
      .context(Data{loop, signal_code})
      .start([](auto& data_context, auto& k) {
        using K = std::decay_t<decltype(k)>;
        data_context.k = static_cast<void*>(&k);
        data_context.signal.data = &data_context;
        data_context.start = [&data_context](EventLoop& loop) {
          auto status = uv_signal_init(loop, &(data_context.signal));
          if (status) {
            (static_cast<K*>(data_context.k))
                ->Fail(uv_err_name(status));
          } else {
            auto signal_cb = [](uv_signal_t* handle, int signum) {
              Data* data = static_cast<Data*>(handle->data);
              uv_close((uv_handle_t*) (&data->signal), nullptr);
              (static_cast<K*>(data->k))->Start(signum);
            };

            auto error = uv_signal_start_oneshot(
                &(data_context.signal),
                signal_cb,
                data_context.signal_code);
            if (error > 0) {
              uv_close((uv_handle_t*) (&data_context.signal), nullptr);
              (static_cast<K*>(data_context.k))
                  ->Fail(uv_err_name(error));
            }
          }
        };
        data_context.loop.Invoke(&data_context.start);
      })
      .interrupt([](auto& data_context, auto& k) {
        using K = std::decay_t<decltype(k)>;
        data_context.interrupt = [&data_context](EventLoop& loop) {
          if (uv_is_active((uv_handle_t*) &data_context.signal)) {
            auto error = uv_signal_stop(&data_context.signal);
            uv_close((uv_handle_t*) (&data_context.signal), nullptr);
            if (error) {
              (static_cast<K*>(data_context.k))
                  ->Fail(uv_strerror(error));
            } else {
              (static_cast<K*>(data_context.k))->Stop();
            }
          } else {
            uv_close((uv_handle_t*) &data_context.signal, nullptr);
            (static_cast<K*>(data_context.k))->Stop();
          }
        };
        data_context.loop.Invoke(&data_context.interrupt);
      });
}

inline auto Signal(const int signal_code) {
  return Signal(EventLoop::Default(), signal_code);
}

} // namespace eventuals
} // namespace stout