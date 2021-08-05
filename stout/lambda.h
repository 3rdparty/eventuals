#pragma once

#include "stout/eventual.h"

////////////////////////////////////////////////////////////////////////

namespace stout {
namespace eventuals {

////////////////////////////////////////////////////////////////////////

namespace detail {

////////////////////////////////////////////////////////////////////////

template <typename K_, typename F_>
struct Lambda {
  template <typename... Args>
  void Start(Args&&... args) {
    eventuals::succeed(k_, f_(std::forward<Args>(args)...));
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

////////////////////////////////////////////////////////////////////////

template <typename F_>
struct LambdaComposable {
  template <typename Arg>
  using ValueFrom = typename std::conditional_t<
      std::is_void_v<Arg>,
      std::invoke_result<F_>,
      std::invoke_result<F_, Arg>>::type;

  template <typename Arg, typename K>
  auto k(K k) && {
    return Lambda<K, F_>{std::move(k), std::move(f_)};
  }

  F_ f_;
};

////////////////////////////////////////////////////////////////////////

} // namespace detail

////////////////////////////////////////////////////////////////////////

template <typename F>
auto Lambda(F f) {
  return detail::LambdaComposable<F>{std::move(f)};
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals
} // namespace stout
