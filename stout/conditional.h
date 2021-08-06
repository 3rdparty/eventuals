#pragma once

#include "stout/eventual.h"
#include "stout/then.h" // For '_Then::Adaptor'.
#include "stout/type-traits.h" // For 'type_identity'.

////////////////////////////////////////////////////////////////////////

namespace stout {
namespace eventuals {

////////////////////////////////////////////////////////////////////////

namespace detail {

////////////////////////////////////////////////////////////////////////

struct _Conditional {
  template <
      typename K_,
      typename Condition_,
      typename Then_,
      typename Else_,
      typename Arg_>
  struct Continuation {
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
        then_adaptor_.emplace(
            then_(std::forward<Args>(args)...)
                .template k<void>(_Then::Adaptor<K_>{k_}));

        if (interrupt_ != nullptr) {
          then_adaptor_->Register(*interrupt_);
        }

        eventuals::succeed(*then_adaptor_);
      } else {
        else_adaptor_.emplace(
            else_(std::forward<Args>(args)...)
                .template k<void>(_Then::Adaptor<K_>{k_}));

        if (interrupt_ != nullptr) {
          else_adaptor_->Register(*interrupt_);
        }

        eventuals::succeed(*else_adaptor_);
      }
    }

    template <typename... Args>
    void Fail(Args&&... args) {
      eventuals::fail(k_, std::forward<Args>(args)...);
    }

    void Stop() {
      eventuals::stop(k_);
    }

    void Register(Interrupt& interrupt) {
      assert(interrupt_ == nullptr);
      interrupt_ = &interrupt;
      k_.Register(interrupt);
    }

    K_ k_;
    Condition_ condition_;
    Then_ then_;
    Else_ else_;

    Interrupt* interrupt_ = nullptr;

    using ThenE_ = std::invoke_result_t<Then_, Arg_>;
    using ElseE_ = std::invoke_result_t<Else_, Arg_>;

    using ThenValue_ = typename ThenE_::template ValueFrom<void>;
    using ElseValue_ = typename ElseE_::template ValueFrom<void>;

    static_assert(
        std::disjunction_v<
            std::is_same<ThenValue_, ElseValue_>,
            std::is_void<ThenValue_>,
            std::is_void<ElseValue_>>,
        "\"then\" and \"else\" branch of 'Conditional' *DO NOT* return "
        "an eventual value of the same type");

    using ThenAdaptor_ = decltype(std::declval<ThenE_>().template k<void>(
        std::declval<_Then::Adaptor<K_>>()));

    using ElseAdaptor_ = decltype(std::declval<ElseE_>().template k<void>(
        std::declval<_Then::Adaptor<K_>>()));

    std::optional<ThenAdaptor_> then_adaptor_;
    std::optional<ElseAdaptor_> else_adaptor_;
  };

  template <typename Condition_, typename Then_, typename Else_>
  struct Composable {
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
      return Continuation<K, Condition_, Then_, Else_, Arg>{
          std::move(k),
          std::move(condition_),
          std::move(then_),
          std::move(else_)};
    }

    Condition_ condition_;
    Then_ then_;
    Else_ else_;
  };
};

////////////////////////////////////////////////////////////////////////

} // namespace detail

////////////////////////////////////////////////////////////////////////

template <typename Condition, typename Then, typename Else>
auto Conditional(Condition condition, Then then, Else els3) {
  return detail::_Conditional::Composable<Condition, Then, Else>{
      std::move(condition),
      std::move(then),
      std::move(els3)};
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals
} // namespace stout

////////////////////////////////////////////////////////////////////////
