#pragma once

#include "stout/eventual.h"
#include "stout/then.h"

////////////////////////////////////////////////////////////////////////

namespace stout {
namespace eventuals {

////////////////////////////////////////////////////////////////////////

namespace detail {

////////////////////////////////////////////////////////////////////////

struct _Catch {
  template <typename K_, typename F_>
  struct Continuation {
    template <typename... Args>
    void Start(Args&&... args) {
      eventuals::succeed(k_, std::forward<Args>(args)...);
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

      eventuals::succeed(*e);
    }

    void Stop() {
      eventuals::stop(k_);
    }

    void Register(Interrupt& interrupt) {
      interrupt_ = &interrupt;
      k_.Register(interrupt);
    }

    K_ k_;
    F_ f_;

    Interrupt* interrupt_ = nullptr;

    // TODO(benh): propagate eventual errors so we don't need to
    // allocate on the heap in order to type erase.
    std::unique_ptr<void, Callback<void*>> e_;
  };

  template <typename F_>
  struct Composable {
    template <typename Arg>
    using ValueFrom = Arg;

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
auto Catch(F f) {
  return detail::_Catch::Composable<F>{std::move(f)};
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals
} // namespace stout

////////////////////////////////////////////////////////////////////////
