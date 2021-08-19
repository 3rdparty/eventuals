#pragma once

#include <memory>

#include "stout/context.h"
#include "stout/eventual.h"
#include "stout/libuv/loop.h"

using stout::uv::Loop;

namespace stout {
namespace uv {

auto Signal(Loop& loop, const int signal_code) {
  struct Data {
    uv_signal_t signal;
    std::atomic<bool> once_ = false;
  };

  return stout::eventuals::Eventual<int>()
      .context(stout::eventuals::Context<Data>())
      .start([&, signal_code](auto& data, auto& k) {
        auto status = uv_signal_init(loop, &(data->signal));
        if (status) {
          stout::eventuals::fail(k, uv_err_name(status));
        } else {
          //data->once_ = true;
          data->signal.data = &k;

          auto signal_cb = [](uv_signal_t* handle, int signum) {
            auto res = uv_signal_stop(handle);
            if (res) {
              stout::eventuals::fail(
                  *static_cast<decltype(&k)>(handle->data),
                  uv_err_name(res));
            } else {
              stout::eventuals::succeed(
                  *static_cast<decltype(&k)>(handle->data),
                  signum);
            }
          };
          auto res = uv_signal_start(&(data->signal), signal_cb, signal_code);
          if (res) {
            stout::eventuals::fail(k, uv_err_name(res));
          }
          data->once_ = true;
        }
      })
      .interrupt([](auto& data, auto& k) {
        if (data->once_) {
          uv_signal_stop(&(data->signal));
        }
        stout::eventuals::stop(k);
      });
}

} // namespace uv
} // namespace stout