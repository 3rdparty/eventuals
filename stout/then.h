#pragma once

#include "stout/eventual.h"

////////////////////////////////////////////////////////////////////////

namespace stout {
namespace eventuals {

////////////////////////////////////////////////////////////////////////

namespace detail {

////////////////////////////////////////////////////////////////////////

struct _Then {
  template <typename K_>
  struct Adaptor {
    template <typename... Args>
    void Start(Args&&... args) {
      k_.Start(std::forward<Args>(args)...);
    }

    template <typename... Args>
    void Fail(Args&&... args) {
      k_.Fail(std::forward<Args>(args)...);
    }

    void Stop() {
      k_.Stop();
    }

    void Register(Interrupt&) {
      // Already registered K once in 'Then::Register()'.
    }

    K_& k_;
  };

  template <typename K_, typename F_, typename Arg_>
  struct Continuation {
    using E_ = typename std::conditional_t<
        std::is_void_v<Arg_>,
        std::invoke_result<F_>,
        std::invoke_result<F_, Arg_>>::type;

    template <typename... Args>
    void Start(Args&&... args) {
      adaptor_.emplace(
          f_(std::forward<Args>(args)...).template k<void>(Adaptor<K_>{k_}));

      if (interrupt_ != nullptr) {
        adaptor_->Register(*interrupt_);
      }

      adaptor_->Start();
    }

    template <typename... Args>
    void Fail(Args&&... args) {
      k_.Fail(std::forward<Args>(args)...);
    }

    void Stop() {
      k_.Stop();
    }

    void Register(Interrupt& interrupt) {
      assert(interrupt_ == nullptr);
      interrupt_ = &interrupt;
      k_.Register(interrupt);
    }

    K_ k_;
    F_ f_;

    Interrupt* interrupt_ = nullptr;

    using Adaptor_ = decltype(std::declval<E_>().template k<void>(
        std::declval<Adaptor<K_>>()));

    std::optional<Adaptor_> adaptor_;
  };

  template <typename F_>
  struct Composable {
    template <typename Arg>
    using ValueFrom = typename std::conditional_t<
        std::is_void_v<Arg>,
        std::invoke_result<F_>,
        std::invoke_result<F_, Arg>>::type::template ValueFrom<void>;

    template <typename Arg, typename K>
    auto k(K k) && {
      return Continuation<K, F_, Arg>{std::move(k), std::move(f_)};
    }

    F_ f_;
  };
};

////////////////////////////////////////////////////////////////////////

} // namespace detail

////////////////////////////////////////////////////////////////////////

template <typename F>
auto Then(F f) {
  return detail::_Then::Composable<F>{std::move(f)};
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals
} // namespace stout

////////////////////////////////////////////////////////////////////////
