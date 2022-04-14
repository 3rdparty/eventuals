#pragma once

#include "eventuals/eventual.h"
#include "eventuals/then.h" // For '_Then::Adaptor'.
#include "eventuals/type-traits.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

struct _If final {
  template <typename K_, typename YesE_, typename NoE_>
  struct Continuation final {
    Continuation(K_ k, bool condition, YesE_ yes, NoE_ no)
      : condition_(condition),
        yes_(std::move(yes)),
        no_(std::move(no)),
        k_(std::move(k)) {}

    template <typename... Args>
    void Start(Args&&...) {
      if (condition_) {
        yes_adapted_.emplace(
            std::move(yes_).template k<void>(_Then::Adaptor<K_>{k_}));

        if (interrupt_ != nullptr) {
          yes_adapted_->Register(*interrupt_);
        }

        yes_adapted_->Start();
      } else {
        no_adapted_.emplace(
            std::move(no_).template k<void>(_Then::Adaptor<K_>{k_}));

        if (interrupt_ != nullptr) {
          no_adapted_->Register(*interrupt_);
        }

        no_adapted_->Start();
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
      assert(interrupt_ == nullptr);
      interrupt_ = &interrupt;
      k_.Register(interrupt);
    }

    bool condition_;
    YesE_ yes_;
    NoE_ no_;

    Interrupt* interrupt_ = nullptr;

    using YesValue_ = typename YesE_::template ValueFrom<void>;
    using NoValue_ = typename NoE_::template ValueFrom<void>;

    static_assert(
        std::disjunction_v<
            std::is_same<YesValue_, NoValue_>,
            std::is_void<YesValue_>,
            std::is_void<NoValue_>>,
        "'yes' and 'no' of 'If' *DO NOT* return "
        "an eventual value of the same type");

    using YesAdapted_ =
        decltype(std::declval<YesE_>().template k<void>(
            std::declval<_Then::Adaptor<K_>>()));

    using NoAdapted_ =
        decltype(std::declval<NoE_>().template k<void>(
            std::declval<_Then::Adaptor<K_>>()));

    std::optional<YesAdapted_> yes_adapted_;
    std::optional<NoAdapted_> no_adapted_;

    // NOTE: we store 'k_' as the _last_ member so it will be
    // destructed _first_ and thus we won't have any use-after-delete
    // issues during destruction of 'k_' if it holds any references or
    // pointers to any (or within any) of the above members.
    K_ k_;
  };

  template <typename YesE_, typename NoE_>
  struct Builder final {
    template <typename YesValue, typename NoValue>
    using Unify_ = typename std::conditional_t<
        std::is_same_v<YesValue, NoValue>,
        type_identity<YesValue>,
        std::conditional_t<
            std::is_void_v<YesValue>,
            type_identity<NoValue>,
            std::enable_if<std::is_void_v<NoValue>, YesValue>>>::type;

    template <typename Arg>
    using ValueFrom = Unify_<
        // NOTE: we propagate 'void' as the value type for both
        // 'YesE_' and 'NoE_' until they are defined (which
        // they must eventually be otherwise a 'static_assert()' will
        // raise a compile error when invoking 'k()').
        typename std::conditional_t<
            IsUndefined<YesE_>::value,
            decltype(Eventual<void>()),
            YesE_>::template ValueFrom<void>,
        typename std::conditional_t<
            IsUndefined<NoE_>::value,
            decltype(Eventual<void>()),
            NoE_>::template ValueFrom<void>>;

    template <typename Arg, typename Errors>
    using ErrorsFrom = tuple_types_union_all_t<
        Errors,
        typename std::conditional_t<
            IsUndefined<YesE_>::value,
            decltype(Eventual<void>()),
            YesE_>::template ErrorsFrom<Arg, Errors>,
        typename std::conditional_t<
            IsUndefined<NoE_>::value,
            decltype(Eventual<void>()),
            NoE_>::template ErrorsFrom<Arg, Errors>>;

    template <typename YesE, typename NoE>
    static auto create(bool condition, YesE yes, NoE no) {
      return Builder<YesE, NoE>{
          condition,
          std::move(yes),
          std::move(no)};
    }


    template <typename Arg, typename K>
    auto k(K k) && {
      static_assert(!IsUndefined<YesE_>::value, "Missing 'yes'");
      static_assert(!IsUndefined<NoE_>::value, "Missing 'no'");

      return Continuation<K, YesE_, NoE_>(
          std::move(k),
          condition_,
          std::move(yes_),
          std::move(no_));
    }

    template <typename YesF>
    auto yes(YesF yes) && {
      static_assert(IsUndefined<YesE_>::value, "Duplicate 'yes'");

      static_assert(
          !HasValueFrom<YesF>::value,
          "'If().yes()' expects a callable (e.g., a lambda) not an eventual");

      return create(condition_, Then(std::move(yes)), std::move(no_));
    }

    template <typename NoF>
    auto no(NoF no) && {
      static_assert(IsUndefined<NoE_>::value, "Duplicate 'no'");

      static_assert(
          !HasValueFrom<NoF>::value,
          "'If().no()' expects a callable (e.g., a lambda) not an eventual");

      return create(condition_, std::move(yes_), Then(std::move(no)));
    }

    bool condition_;
    YesE_ yes_;
    NoE_ no_;
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
