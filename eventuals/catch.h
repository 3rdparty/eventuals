#pragma once

#include <memory> // For 'std::unique_ptr'.

#include "eventuals/eventual.h"
#include "eventuals/then.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

struct _Catch {
  template <typename K_, typename F_>
  struct Continuation {
    Continuation(K_ k, F_ f)
      : f_(std::move(f)),
        k_(std::move(k)) {}

    template <typename... Args>
    void Start(Args&&... args) {
      k_.Start(std::forward<Args>(args)...);
    }

    template <typename... Args>
    void Fail(Args&&... args) {
      using E = decltype(f_(std::forward<Args>(args)...)
                             .template k<void>(_Then::Adaptor<K_>{k_}));

      // TODO(benh): propagate eventual errors so we don't need to
      // allocate on the heap in order to type erase.
      e_ = std::unique_ptr<void, Callback<void*>>(
          new E(f_(std::forward<Args>(args)...)
                    .template k<void>(_Then::Adaptor<K_>{k_})),
          [](void* e) {
            delete static_cast<E*>(e);
          });

      auto* e = static_cast<E*>(e_.get());

      if (interrupt_ != nullptr) {
        e->Register(*interrupt_);
      }

      e->Start();
    }

    void Stop() {
      k_.Stop();
    }

    void Register(Interrupt& interrupt) {
      interrupt_ = &interrupt;
      k_.Register(interrupt);
    }

    F_ f_;

    Interrupt* interrupt_ = nullptr;

    // TODO(benh): propagate eventual errors so we don't need to
    // allocate on the heap in order to type erase.
    std::unique_ptr<void, Callback<void*>> e_;

    // NOTE: we store 'k_' as the _last_ member so it will be
    // destructed _first_ and thus we won't have any use-after-delete
    // issues during destruction of 'k_' if it holds any references or
    // pointers to any (or within any) of the above members.
    K_ k_;
  };

  template <typename F_>
  struct Composable {
    template <typename Arg>
    using ValueFrom = Arg;

    template <typename Arg, typename K>
    auto k(K k) && {
      return Continuation<K, F_>(std::move(k), std::move(f_));
    }

    F_ f_;
  };
};

////////////////////////////////////////////////////////////////////////

template <typename F>
auto Catch(F f) {
  return _Catch::Composable<F>{std::move(f)};
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
