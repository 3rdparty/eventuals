#pragma once

#include "stout/eventual.h"

////////////////////////////////////////////////////////////////////////

namespace stout {
namespace eventuals {

////////////////////////////////////////////////////////////////////////

namespace detail {

////////////////////////////////////////////////////////////////////////

template<typename K_, typename T_, typename Value_>
struct Raise {
    using Value = typename ValueFrom<K_, Value_>::type;

    Raise(K_ k, T_ t) : k_(std::move(k)), t_(std::move(t)) {}

    template<typename Value, typename K, typename T>
    static auto create(K k, T t) {
        return Raise<K, T, Value>(std::move(k), std::move(t));
    }

    template<typename K, std::enable_if_t<IsContinuation<K>::value, int> = 0>
    auto k(K k) && {
        return create<Value_>(
            [&]() {
                if constexpr (!IsUndefined<K_>::value) {
                    return std::move(k_) | std::move(k);
                } else {
                    return std::move(k);
                }
            }(),
            std::move(t_));
    }

    template<typename F, std::enable_if_t<!IsContinuation<F>::value, int> = 0>
    auto k(F f) && {
        return std::move(*this) | eventuals::Lambda(std::move(f));
    }

    template<typename... Args>
    void Start(Args&&...) {
        eventuals::fail(k_, std::move(t_));
    }

    template<typename... Args>
    void Fail(Args&&... args) {
        eventuals::fail(k_, std::forward<Args>(args)...);
    }

    void Stop() { eventuals::stop(k_); }

    void Register(Interrupt& interrupt) { k_.Register(interrupt); }

    K_   k_;
    T_   t_;
};

////////////////////////////////////////////////////////////////////////

}   // namespace detail

////////////////////////////////////////////////////////////////////////

template<typename K, typename T, typename Value>
struct IsContinuation<detail::Raise<K, T, Value>> : std::true_type {};

////////////////////////////////////////////////////////////////////////

template<typename K, typename T, typename Value>
struct HasTerminal<detail::Raise<K, T, Value>> : HasTerminal<K> {};

////////////////////////////////////////////////////////////////////////

template<typename K, typename T, typename Value_>
struct Unify<detail::Raise<K, T, Value_>> {
    template<typename Value>
    static auto unify(detail::Raise<K, T, Value_> raise) {
        auto k = eventuals::unify<Value>(std::move(raise.k_));
        return detail::Raise<decltype(k), T, Value>(std::move(k),
                                                    std::move(raise.t_));
    }
};

////////////////////////////////////////////////////////////////////////

template<typename T>
auto Raise(T t) {
    return detail::Raise<Undefined, T, Undefined>(Undefined(), std::move(t));
}

}   // namespace eventuals
}   // namespace stout
