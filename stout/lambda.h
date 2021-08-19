#pragma once

#include "stout/eventual.h"

////////////////////////////////////////////////////////////////////////

namespace stout {
namespace eventuals {

////////////////////////////////////////////////////////////////////////

namespace detail {

////////////////////////////////////////////////////////////////////////

struct _Lambda {
  template <typename K_, typename F_>
  struct Continuation {
    template <typename... Args>
    void Start(Args&&... args) {
      if constexpr (std::is_void_v<std::invoke_result_t<F_, Args...>>) {
        f_(std::forward<Args>(args)...);
        eventuals::succeed(k_);
      } else {
        eventuals::succeed(k_, f_(std::forward<Args>(args)...));
      }
    }

    template <typename... Args>
    void Fail(Args&&... args) {
      eventuals::fail(k_, std::forward<Args>(args)...);
    }

    void Stop() {
      eventuals::stop(k_);
    }

    void Register(Interrupt& interrupt) {
      k_.Register(interrupt);
    }

    K_ k_;
    F_ f_;
  };

  template <typename F_>
  struct Composable {
    template <typename Arg>
    using ValueFrom = typename std::conditional_t<
        std::is_void_v<Arg>,
        std::invoke_result<F_>,
        std::invoke_result<F_, Arg>>::type;

    template <typename Arg, typename K>
    auto k(K k) && {
      return Continuation<K, F_>{std::move(k), std::move(f_)};
    }

    F_ f_;
  };
};

////////////////////////////////////////////////////////////////////////

} // namespace detail

////////////////////////////////////////////////////////////////////////

template <typename F>
auto Lambda(F f) {
  return detail::_Lambda::Composable<F>{std::move(f)};
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals
} // namespace stout

////////////////////////////////////////////////////////////////////////
