#pragma once

#include "stout/adaptor.h"
#include "stout/continuation.h"
#include "stout/eventual.h"
#include "stout/lambda.h"

////////////////////////////////////////////////////////////////////////

namespace stout {
namespace eventuals {

////////////////////////////////////////////////////////////////////////

namespace detail {

////////////////////////////////////////////////////////////////////////

template<typename K_, typename Condition_, typename Then_, typename Else_,
         typename Arg_, typename Value_>
struct Conditional {
    using Value = typename ValueFrom<K_, Value_>::type;

    Conditional(K_ k, Condition_ condition, Then_ then, Else_ els3) :
        k_(std::move(k)),
        condition_(std::move(condition)),
        then_(std::move(then)),
        else_(std::move(els3)) {}

    template<typename Arg, typename Value, typename K, typename Condition,
             typename Then, typename Else>
    static auto create(K k, Condition condition, Then then, Else els3) {
        return Conditional<K, Condition, Then, Else, Arg, Value>(
            std::move(k), std::move(condition), std::move(then),
            std::move(els3));
    }

    template<typename K, std::enable_if_t<IsContinuation<K>::value, int> = 0>
    auto k(K k) && {
        return create<Arg_, Value_>(
            [&]() {
                if constexpr (!IsUndefined<K_>::value) {
                    return std::move(k_) | std::move(k);
                } else {
                    return std::move(k);
                }
            }(),
            std::move(condition_), std::move(then_), std::move(else_));
    }

    template<typename F, std::enable_if_t<!IsContinuation<F>::value, int> = 0>
    auto k(F f) && {
        return std::move(*this) | eventuals::Lambda(std::move(f));
    }

    template<typename... Args>
    void Start(Args&&... args) {
        if (condition_(std::forward<Args>(args)...)) {
            then_adaptor_.emplace(
                eventuals::unify<Value_>(then_(std::forward<Args>(args)...)) |
                Adaptor<K_, Value_>(k_, [](auto& k_, auto&&... values) {
                    eventuals::succeed(
                        k_, std::forward<decltype(values)>(values)...);
                }));

            if (interrupt_ != nullptr) {
                then_adaptor_->Register(*interrupt_);
            }

            eventuals::succeed(*then_adaptor_);
        } else {
            else_adaptor_.emplace(
                eventuals::unify<Value_>(else_(std::forward<Args>(args)...)) |
                Adaptor<K_, Value_>(k_, [](auto& k_, auto&&... values) {
                    eventuals::succeed(
                        k_, std::forward<decltype(values)>(values)...);
                }));

            if (interrupt_ != nullptr) {
                else_adaptor_->Register(*interrupt_);
            }

            eventuals::succeed(*else_adaptor_);
        }
    }

    template<typename... Args>
    void Fail(Args&&... args) {
        eventuals::fail(k_, std::forward<Args>(args)...);
    }

    void Stop() { eventuals::stop(k_); }

    void Register(Interrupt& interrupt) {
        assert(interrupt_ == nullptr);
        interrupt_ = &interrupt;
        k_.Register(interrupt);
    }

    K_         k_;
    Condition_ condition_;
    Then_      then_;
    Else_      else_;

    Interrupt* interrupt_ = nullptr;

    using ThenE_ = typename InvokeResultPossiblyUndefined<Then_, Arg_>::type;
    using ElseE_ = typename InvokeResultPossiblyUndefined<Else_, Arg_>::type;

    using ThenValue_ = typename ValuePossiblyUndefined<ThenE_>::Value;
    using ElseValue_ = typename ValuePossiblyUndefined<ElseE_>::Value;

    using ThenAdaptor_ =
        typename EKPossiblyUndefined<decltype(eventuals::unify<Value_>(
                                         std::declval<ThenE_>())),
                                     Adaptor<K_, Value_>>::type;

    using ElseAdaptor_ =
        typename EKPossiblyUndefined<decltype(eventuals::unify<Value_>(
                                         std::declval<ElseE_>())),
                                     Adaptor<K_, Value_>>::type;

    std::optional<ThenAdaptor_> then_adaptor_;
    std::optional<ElseAdaptor_> else_adaptor_;
};

////////////////////////////////////////////////////////////////////////

}   // namespace detail

////////////////////////////////////////////////////////////////////////

template<typename K, typename Condition, typename Then, typename Else,
         typename Arg, typename Value>
struct IsContinuation<
    detail::Conditional<K, Condition, Then, Else, Arg, Value>> :
    std::true_type {};

////////////////////////////////////////////////////////////////////////

template<typename K, typename Condition, typename Then, typename Else,
         typename Arg, typename Value>
struct HasTerminal<detail::Conditional<K, Condition, Then, Else, Arg, Value>> :
    HasTerminal<K> {};

////////////////////////////////////////////////////////////////////////

template<typename K, typename Condition, typename Then, typename Else,
         typename Arg_, typename Value_>
struct Compose<detail::Conditional<K, Condition, Then, Else, Arg_, Value_>> {
    template<typename Arg>
    static auto compose(
        detail::Conditional<K, Condition, Then, Else, Arg_, Value_>
            conditional) {
        using ThenE = decltype(std::declval<Then>()(std::declval<Arg>()));
        using ElseE = decltype(std::declval<Else>()(std::declval<Arg>()));

        static_assert(IsContinuation<ThenE>::value,
                      "\"then\" branch of Conditional "
                      "*DOES NOT* return an eventual continuation");

        static_assert(IsContinuation<ElseE>::value,
                      "\"else\" branch of Conditional "
                      "*DOES NOT* return an eventual continuation");

        using ThenValue = typename ThenE::Value;
        using ElseValue = typename ElseE::Value;

        static_assert(
            std::is_same_v<ThenValue, ElseValue> ||
                IsUndefined<ThenValue>::value || IsUndefined<ElseValue>::value,
            "\"then\" and \"else\" branch of Conditional *DO NOT* return "
            "an eventual value of the same type");

        using Value = std::conditional_t<!IsUndefined<ThenValue>::value,
                                         ThenValue, ElseValue>;

        auto k      = eventuals::compose<Value>(std::move(conditional.k_));
        auto then   = eventuals::unify<Value>(std::move(conditional.then_));
        auto els3   = eventuals::unify<Value>(std::move(conditional.else_));

        return detail::Conditional<decltype(k), Condition, decltype(then),
                                   decltype(els3), Arg, Value>(
            std::move(k), std::move(conditional.condition_), std::move(then),
            std::move(els3));
    }
};

////////////////////////////////////////////////////////////////////////

template<typename Condition, typename Then, typename Else>
auto Conditional(Condition condition, Then then, Else els3) {
    return detail::Conditional<Undefined, Condition, Then, Else, Undefined,
                               Undefined>(Undefined(), std::move(condition),
                                          std::move(then), std::move(els3));
}

////////////////////////////////////////////////////////////////////////

}   // namespace eventuals
}   // namespace stout

////////////////////////////////////////////////////////////////////////
