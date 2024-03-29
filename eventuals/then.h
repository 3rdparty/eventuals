#pragma once

#include "eventuals/compose.h"
#include "eventuals/eventual.h"
#include "eventuals/just.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

template <typename T, typename Arg, typename Errors>
using ValueFromMaybeComposable = typename std::conditional_t<
    !HasValueFrom<T>::value,
    decltype(Eventual<T>()),
    T>::template ValueFrom<Arg, Errors>;

////////////////////////////////////////////////////////////////////////

template <typename Arg_, typename Left_, typename Right_, typename Errors_>
struct ErrorsFromComposed {
  using Errors = typename Right_::template ErrorsFrom<
      typename Left_::template ValueFrom<Arg_, Errors_>,
      typename Left_::template ErrorsFrom<Arg_, Errors_>>;
};

template <typename T, typename Arg, typename Errors>
using ErrorsFromMaybeComposable = typename ErrorsFromComposed<
    Arg,
    std::conditional_t<
        !HasValueFrom<T>::value,
        decltype(Just()),
        T>,
    decltype(Just()),
    Errors>::Errors;

////////////////////////////////////////////////////////////////////////

struct _Then final {
  template <typename K_>
  struct Adaptor final {
    template <typename... Args>
    void Start(Args&&... args) {
      k_.Start(std::forward<Args>(args)...);
    }

    template <typename Error>
    void Fail(Error&& error) {
      k_.Fail(std::forward<Error>(error));
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
  struct Composable final {
    template <typename Arg, typename Errors>
    using ValueFrom = ValueFromMaybeComposable<
        typename std::conditional_t<
            std::is_void_v<Arg>,
            std::invoke_result<F_>,
            std::invoke_result<F_, Arg>>::type,
        void,
        std::tuple<>>;

    template <typename Arg, typename Errors>
    using ErrorsFrom =
        tuple_types_union_t<
            Errors,
            ErrorsFromMaybeComposable<
                typename std::conditional_t<
                    std::is_void_v<Arg>,
                    std::invoke_result<F_>,
                    std::invoke_result<F_, Arg>>::type,
                void,
                std::tuple<>>>;

    template <typename Downstream>
    static constexpr bool CanCompose = Downstream::ExpectsValue;

    using Expects = SingleValue;

    template <typename Arg, typename Errors, typename K>
    auto k(K k) && {
      static_assert(
          !std::is_void_v<Arg> || std::is_invocable_v<F_>,
          "callable passed to 'Then' should be "
          "invocable with no arguments");

      static_assert(
          std::is_void_v<Arg> || std::is_invocable_v<F_, Arg>,
          "callable passed to 'Then' is not invocable "
          "with the type of argument it receives");

      return Continuation<K, F_, Arg>(std::move(k), std::move(f_));
    }

    F_ f_;
  };
};

////////////////////////////////////////////////////////////////////////

template <typename K_, typename F_, typename Arg_>
struct _Then::Continuation<K_, F_, Arg_, false> final {
  Continuation(K_ k, F_ f)
    : f_(std::move(f)),
      k_(std::move(k)) {}

  template <typename... Args>
  void Start(Args&&... args) {
    if constexpr (std::is_void_v<std::invoke_result_t<F_, Args...>>) {
      f_(std::forward<Args>(args)...);
      k_.Start();
    } else {
      k_.Start(f_(std::forward<Args>(args)...));
    }
  }

  template <typename Error>
  void Fail(Error&& error) {
    k_.Fail(std::forward<Error>(error));
  }

  void Stop() {
    k_.Stop();
  }

  void Register(Interrupt& interrupt) {
    k_.Register(interrupt);
  }

  F_ f_;

  // NOTE: we store 'k_' as the _last_ member so it will be
  // destructed _first_ and thus we won't have any use-after-delete
  // issues during destruction of 'k_' if it holds any references or
  // pointers to any (or within any) of the above members.
  K_ k_;
};

////////////////////////////////////////////////////////////////////////

template <typename K_, typename F_, typename Arg_>
struct _Then::Continuation<K_, F_, Arg_, true> final {
  Continuation(K_ k, F_ f)
    : f_(std::move(f)),
      k_(std::move(k)) {}

  template <typename... Args>
  void Start(Args&&... args) {
    adapted_.emplace(
        f_(std::forward<Args>(args)...)
            .template k<void, std::tuple<>>(Adaptor<K_>{k_}));

    if (interrupt_ != nullptr) {
      adapted_->Register(*interrupt_);
    }

    adapted_->Start();
  }

  template <typename Error>
  void Fail(Error&& error) {
    k_.Fail(std::forward<Error>(error));
  }

  void Stop() {
    k_.Stop();
  }

  void Register(Interrupt& interrupt) {
    assert(interrupt_ == nullptr);
    interrupt_ = &interrupt;
    k_.Register(interrupt);
  }

  F_ f_;

  Interrupt* interrupt_ = nullptr;

  using E_ = typename std::conditional_t<
      std::is_void_v<Arg_>,
      std::invoke_result<F_>,
      std::invoke_result<F_, Arg_>>::type;

  using Adapted_ = decltype(std::declval<E_>().template k<void, std::tuple<>>(
      std::declval<Adaptor<K_>>()));

  std::optional<Adapted_> adapted_;

  // NOTE: we store 'k_' as the _last_ member so it will be
  // destructed _first_ and thus we won't have any use-after-delete
  // issues during destruction of 'k_' if it holds any references or
  // pointers to any (or within any) of the above members.
  K_ k_;
};

////////////////////////////////////////////////////////////////////////

template <typename F>
[[nodiscard]] auto Then(F f) {
  return _Then::Composable<F>{std::move(f)};
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
