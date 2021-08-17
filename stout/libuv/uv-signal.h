#pragma once

#include <memory>

#include "stout/eventual.h"
#include "stout/libuv/loop.h"

using stout::uv::Loop;

namespace stout {
namespace uv {


class Signal {
 public:
  Signal() {
    p_signal = std::make_unique<uv_signal_t>();
  }

  Signal(const Signal& signal) = delete;

  auto operator()(Loop& loop, const int signal) {
    return stout::eventuals::Eventual<void>()
        .start([&, signal](auto& k) {
          auto status = uv_signal_init(loop, p_signal.get());
          if (status) {
            stout::eventuals::fail(k, uv_err_name(status));
          } else {
            p_signal->data = &k;
            auto signal_cb = [](uv_signal_t* handle, int signum) {
              auto res = uv_signal_stop(handle);
              if (res) {
                stout::eventuals::fail(
                    *static_cast<decltype(&k)>(handle->data),
                    uv_err_name(res));
              } else {
                stout::eventuals::succeed(
                    *static_cast<decltype(&k)>(handle->data));
              }
            };

            auto res = uv_signal_start(p_signal.get(), signal_cb, signal);
            if (res) {
              stout::eventuals::fail(k, uv_err_name(res));
            }
          }
        });
  }

 private:
  std::unique_ptr<uv_signal_t> p_signal;
};


} // namespace uv
} // namespace stout