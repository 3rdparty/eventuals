#pragma once

#include "eventuals/eventual.h"
#include "eventuals/then.h" // For '_Then::Adaptor'.
#include "eventuals/type-traits.h" // For 'type_identity'.

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

struct _If {
  template <typename K_, typename ThenE_, typename OtherwiseE_>
  struct Continuation {
    template <typename... Args>
    void Start(Args&&...) {
      if (condition_) {
        then_adapted_.emplace(
            std::move(then_).template k<void>(_Then::Adaptor<K_>{k_}));

        if (interrupt_ != nullptr) {
          then_adapted_->Register(*interrupt_);
        }

        then_adapted_->Start();
      } else {
        otherwise_adapted_.emplace(
            std::move(otherwise_).template k<void>(_Then::Adaptor<K_>{k_}));

        if (interrupt_ != nullptr) {
          otherwise_adapted_->Register(*interrupt_);
        }

        otherwise_adapted_->Start();
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

    K_ k_;
    bool condition_;
    ThenE_ then_;
    OtherwiseE_ otherwise_;

    Interrupt* interrupt_ = nullptr;

    using ThenValue_ = typename ThenE_::template ValueFrom<void>;
    using OtherwiseValue_ = typename OtherwiseE_::template ValueFrom<void>;

    static_assert(
        std::disjunction_v<
            std::is_same<ThenValue_, OtherwiseValue_>,
            std::is_void<ThenValue_>,
            std::is_void<OtherwiseValue_>>,
        "'then' and 'otherwise' of 'If' *DO NOT* return "
        "an eventual value of the same type");

    using ThenAdapted_ =
        decltype(std::declval<ThenE_>().template k<void>(
            std::declval<_Then::Adaptor<K_>>()));

    using OtherwiseAdapted_ =
        decltype(std::declval<OtherwiseE_>().template k<void>(
            std::declval<_Then::Adaptor<K_>>()));

    std::optional<ThenAdapted_> then_adapted_;
    std::optional<OtherwiseAdapted_> otherwise_adapted_;
  };

  template <typename ThenE_, typename OtherwiseE_>
  struct Builder {
    template <typename ThenValue, typename OtherwiseValue>
    using Unify_ = typename std::conditional_t<
        std::is_same_v<ThenValue, OtherwiseValue>,
        type_identity<ThenValue>,
        std::conditional_t<
            std::is_void_v<ThenValue>,
            type_identity<OtherwiseValue>,
            std::enable_if<std::is_void_v<OtherwiseValue>, ThenValue>>>::type;

    template <typename Arg>
    using ValueFrom = Unify_<
        // NOTE: we propagate 'void' as the value type for both
        // 'ThenE_' and 'OtherwiseE_' until they are defined (which
        // they must eventually be otherwise a 'static_assert()' will
        // raise a compile error when invoking 'k()').
        typename std::conditional_t<
            IsUndefined<ThenE_>::value,
            decltype(Eventual<void>()),
            ThenE_>::template ValueFrom<void>,
        typename std::conditional_t<
            IsUndefined<OtherwiseE_>::value,
            decltype(Eventual<void>()),
            OtherwiseE_>::template ValueFrom<void>>;

    template <typename ThenE, typename OtherwiseE>
    static auto create(bool condition, ThenE then, OtherwiseE otherwise) {
      return Builder<ThenE, OtherwiseE>{
          condition,
          std::move(then),
          std::move(otherwise)};
    }


    template <typename Arg, typename K>
    auto k(K k) && {
      static_assert(!IsUndefined<ThenE_>::value, "Missing 'then'");
      static_assert(!IsUndefined<OtherwiseE_>::value, "Missing 'otherwise'");

      static_assert(
          HasValueFrom<ThenE_>::value,
          "'If' expects an eventual for 'then'");

      static_assert(
          HasValueFrom<OtherwiseE_>::value,
          "'If' expects an eventual for 'otherwise'");

      return Continuation<K, ThenE_, OtherwiseE_>{
          std::move(k),
          condition_,
          std::move(then_),
          std::move(otherwise_)};
    }

    template <typename ThenE>
    auto then(ThenE then) && {
      static_assert(IsUndefined<ThenE_>::value, "Duplicate 'then'");
      return create(condition_, std::move(then), std::move(otherwise_));
    }

    template <typename OtherwiseE>
    auto otherwise(OtherwiseE otherwise) && {
      static_assert(IsUndefined<OtherwiseE_>::value, "Duplicate 'otherwise'");
      return create(condition_, std::move(then_), std::move(otherwise));
    }

    bool condition_;
    ThenE_ then_;
    OtherwiseE_ otherwise_;
  };
};

////////////////////////////////////////////////////////////////////////

inline auto If(bool condition) {
  return _If::Builder<Undefined, Undefined>{
      condition,
      Undefined(),
      Undefined()};
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
