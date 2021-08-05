#pragma once

#include "stout/eventual.h"

////////////////////////////////////////////////////////////////////////

namespace stout {
namespace eventuals {

////////////////////////////////////////////////////////////////////////

namespace detail {

////////////////////////////////////////////////////////////////////////

struct _Raise {
  template <typename K_, typename T_>
  struct Continuation {
    template <typename... Args>
    void Start(Args&&...) {
      eventuals::fail(k_, std::move(t_));
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
    T_ t_;
  };

  template <typename T_>
  struct Composable {
    template <typename>
    using ValueFrom = void;

    template <typename Arg, typename K>
    auto k(K k) && {
      return Continuation<K, T_>{std::move(k), std::move(t_)};
    }

    T_ t_;
  };
};

////////////////////////////////////////////////////////////////////////

} // namespace detail

////////////////////////////////////////////////////////////////////////

template <typename T>
auto Raise(T t) {
  return detail::_Raise::Composable<T>{std::move(t)};
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals
} // namespace stout

////////////////////////////////////////////////////////////////////////
