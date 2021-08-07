#pragma once

#include "stout/compose.h"
#include "stout/eventual.h"

////////////////////////////////////////////////////////////////////////

namespace stout {
namespace eventuals {

////////////////////////////////////////////////////////////////////////

namespace detail {

////////////////////////////////////////////////////////////////////////

struct _Just {
  template <typename T_>
  struct Composable {
    template <typename>
    using ValueFrom = T_;

    template <typename Arg, typename K>
    auto k(K k) && {
      auto start = [t = std::move(t_)](auto& k, auto&&...) {
        eventuals::succeed(k, std::move(t));
      };

      return _Eventual::Continuation<
          decltype(k),
          Undefined,
          decltype(start),
          Undefined,
          Undefined,
          Undefined,
          T_>{
          Reschedulable<K, T_>{std::move(k)},
          Undefined(),
          std::move(start),
          Undefined(),
          Undefined(),
          Undefined()};
    }

    T_ t_;
  };

  template <>
  struct Composable<void> {
    template <typename>
    using ValueFrom = void;

    template <typename Arg, typename K>
    auto k(K k) && {
      auto start = [](auto& k, auto&&...) {
        eventuals::succeed(k);
      };

      return _Eventual::Continuation<
          decltype(k),
          Undefined,
          decltype(start),
          Undefined,
          Undefined,
          Undefined,
          void>{
          Reschedulable<K, void>{std::move(k)},
          Undefined(),
          std::move(start),
          Undefined(),
          Undefined(),
          Undefined()};
    }
  };
};

////////////////////////////////////////////////////////////////////////

} // namespace detail

////////////////////////////////////////////////////////////////////////

template <typename T>
auto Just(T t) {
  return detail::_Just::Composable<T>{std::move(t)};
}

inline auto Just() {
  return detail::_Just::Composable<void>{};
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals
} // namespace stout

////////////////////////////////////////////////////////////////////////
