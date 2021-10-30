#pragma once

#include "eventuals/eventual.h"
#include "eventuals/just.h"
#include "eventuals/type-traits.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

namespace detail {

////////////////////////////////////////////////////////////////////////

template <typename, typename = void>
struct HasValueFrom : std::false_type {};

template <typename T>
struct HasValueFrom<T, std::void_t<void_template<T::template ValueFrom>>>
  : std::true_type {};

////////////////////////////////////////////////////////////////////////

template <typename T, typename Arg>
using ValueFromMaybeComposable = typename std::conditional_t<
    !HasValueFrom<T>::value,
    decltype(Eventual<T>()),
    T>::template ValueFrom<Arg>;

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

  template <
      typename K_,
      typename F_,
      typename Arg_,
      bool = HasValueFrom<
          typename std::conditional_t<
              std::is_void_v<Arg_>,
              std::invoke_result<F_>,
              std::invoke_result<F_, Arg_>>::type>::value>
  struct Continuation;

  template <typename F_>
  struct Composable {
    template <typename Arg>
    using ValueFrom = ValueFromMaybeComposable<
        typename std::conditional_t<
            std::is_void_v<Arg>,
            std::invoke_result<F_>,
            std::invoke_result<F_, Arg>>::type,
        void>;

    template <typename Arg, typename K>
    auto k(K k) && {
      return Continuation<K, F_, Arg>{std::move(k), std::move(f_)};
    }

    F_ f_;
  };
};

////////////////////////////////////////////////////////////////////////

template <typename K_, typename F_, typename Arg_>
struct _Then::Continuation<K_, F_, Arg_, false> {
  template <typename... Args>
  void Start(Args&&... args) {
    if constexpr (std::is_void_v<std::invoke_result_t<F_, Args...>>) {
      f_(std::forward<Args>(args)...);
      k_.Start();
    } else {
      k_.Start(f_(std::forward<Args>(args)...));
    }
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

  K_ k_;
  F_ f_;
};

////////////////////////////////////////////////////////////////////////

template <typename K_, typename F_, typename Arg_>
struct _Then::Continuation<K_, F_, Arg_, true> {
  using E_ = typename std::conditional_t<
      std::is_void_v<Arg_>,
      std::invoke_result<F_>,
      std::invoke_result<F_, Arg_>>::type;

  template <typename... Args>
  void Start(Args&&... args) {
    adapted_.emplace(
        f_(std::forward<Args>(args)...).template k<void>(Adaptor<K_>{k_}));

    if (interrupt_ != nullptr) {
      adapted_->Register(*interrupt_);
    }

    adapted_->Start();
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

  using Adapted_ = decltype(std::declval<E_>().template k<void>(
      std::declval<Adaptor<K_>>()));

  std::optional<Adapted_> adapted_;
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

////////////////////////////////////////////////////////////////////////
