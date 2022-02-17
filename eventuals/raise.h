#pragma once

#include "eventuals/eventual.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

struct _Raise {
  template <typename K_, typename T_>
  struct Continuation {
    Continuation(K_ k, T_ t)
      : t_(std::move(t)),
        k_(std::move(k)) {}

    template <typename... Args>
    void Start(Args&&...) {
      k_.Fail(std::move(t_));
    }

    template <typename... Args>
    void Fail(Args&&... args) {
      k_.Fail(std::forward<Args>(args)...);
    }

    void Stop() {
      k_.Stop();
    }

    void Register(Interrupt& interrupt) {
      k_.Register(interrupt);
    }

    T_ t_;

    // NOTE: we store 'k_' as the _last_ member so it will be
    // destructed _first_ and thus we won't have any use-after-delete
    // issues during destruction of 'k_' if it holds any references or
    // pointers to any (or within any) of the above members.
    K_ k_;
  };

  template <typename T_>
  struct Composable {
    template <typename Arg>
    using ValueFrom = Arg;

    template <typename Arg, typename K>
    auto k(K k) && {
      return Continuation<K, T_>{std::move(k), std::move(t_)};
    }

    T_ t_;
  };
};

////////////////////////////////////////////////////////////////////////

template <typename T>
auto Raise(T t) {
  return _Raise::Composable<T>{std::move(t)};
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
