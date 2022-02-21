#pragma once

#include "eventuals/eventual.h"
#include "eventuals/then.h" // For '_Then::Adaptor'.
#include "eventuals/type-traits.h" // For 'type_identity'.

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

struct _Conditional {
  template <
      typename K_,
      typename Condition_,
      typename Then_,
      typename Else_,
      typename Arg_>

  struct Continuation final {
    Continuation(
        K_ k,
        Condition_ condition,
        Then_ then,
        Else_ els3)
      : condition_(std::move(condition)),
        then_(std::move(then)),
        else_(std::move(els3)),
        k_(std::move(k)) {}

    template <typename... Args>
    void Start(Args&&... args) {
      // static_assert(
      //     ... ThenE has template member 'ValueFrom',
      //     "\"then\" branch of 'Conditional' "
      //     "*DOES NOT* return an eventual continuation");

      // static_assert(
      //     ... Else has template member 'ValueFrom',
      //     "\"else\" branch of 'Conditional' "
      //     "*DOES NOT* return an eventual continuation");

      if (condition_(std::forward<Args>(args)...)) {
        then_adapted_.emplace(
            then_(std::forward<Args>(args)...)
                .template k<void>(_Then::Adaptor<K_>{k_}));

        if (interrupt_ != nullptr) {
          then_adapted_->Register(*interrupt_);
        }

        then_adapted_->Start();
      } else {
        else_adapted_.emplace(
            else_(std::forward<Args>(args)...)
                .template k<void>(_Then::Adaptor<K_>{k_}));

        if (interrupt_ != nullptr) {
          else_adapted_->Register(*interrupt_);
        }

        else_adapted_->Start();
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
      assert(interrupt_ == nullptr);
      interrupt_ = &interrupt;
      k_.Register(interrupt);
    }

    Condition_ condition_;
    Then_ then_;
    Else_ else_;

    Interrupt* interrupt_ = nullptr;

    using ThenE_ = typename std::conditional_t<
        std::is_void_v<Arg_>,
        std::invoke_result<Then_>,
        std::invoke_result<Then_, Arg_>>::type;

    using ElseE_ = typename std::conditional_t<
        std::is_void_v<Arg_>,
        std::invoke_result<Else_>,
        std::invoke_result<Else_, Arg_>>::type;

    using ThenValue_ = typename ThenE_::template ValueFrom<void>;
    using ElseValue_ = typename ElseE_::template ValueFrom<void>;

    static_assert(
        std::disjunction_v<
            std::is_same<ThenValue_, ElseValue_>,
            std::is_void<ThenValue_>,
            std::is_void<ElseValue_>>,
        "\"then\" and \"else\" branch of 'Conditional' *DO NOT* return "
        "an eventual value of the same type");

    using ThenAdapted_ = decltype(std::declval<ThenE_>().template k<void>(
        std::declval<_Then::Adaptor<K_>>()));

    using ElseAdapted_ = decltype(std::declval<ElseE_>().template k<void>(
        std::declval<_Then::Adaptor<K_>>()));

    std::optional<ThenAdapted_> then_adapted_;
    std::optional<ElseAdapted_> else_adapted_;

    // NOTE: we store 'k_' as the _last_ member so it will be
    // destructed _first_ and thus we won't have any use-after-delete
    // issues during destruction of 'k_' if it holds any references or
    // pointers to any (or within any) of the above members.
    K_ k_;
  };

  template <typename Condition_, typename Then_, typename Else_>
  struct Composable final {
    template <typename ThenValue, typename ElseValue>
    using Unify_ = typename std::conditional_t<
        std::is_same_v<ThenValue, ElseValue>,
        type_identity<ThenValue>,
        std::conditional_t<
            std::is_void_v<ThenValue>,
            type_identity<ElseValue>,
            std::enable_if<std::is_void_v<ElseValue>, ThenValue>>>::type;

    template <typename Arg>
    using ValueFrom = Unify_<
        typename std::conditional_t<
            std::is_void_v<Arg>,
            std::invoke_result<Then_>,
            std::invoke_result<Then_, Arg>>::type::template ValueFrom<void>,
        typename std::conditional_t<
            std::is_void_v<Arg>,
            std::invoke_result<Else_>,
            std::invoke_result<Else_, Arg>>::type::template ValueFrom<void>>;

    template <typename Arg, typename K>
    auto k(K k) && {
      return Continuation<K, Condition_, Then_, Else_, Arg>(
          std::move(k),
          std::move(condition_),
          std::move(then_),
          std::move(else_));
    }

    Condition_ condition_;
    Then_ then_;
    Else_ else_;
  };
};

////////////////////////////////////////////////////////////////////////

template <typename Condition, typename Then, typename Else>
auto Conditional(Condition condition, Then then, Else els3) {
  return _Conditional::Composable<Condition, Then, Else>{
      std::move(condition),
      std::move(then),
      std::move(els3)};
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
