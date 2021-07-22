#pragma once

#include "stout/eventual.h"
#include "stout/forward.h"
#include "stout/lambda.h"
#include "stout/loop.h"
#include "stout/map.h"

////////////////////////////////////////////////////////////////////////

namespace stout {
namespace eventuals {

////////////////////////////////////////////////////////////////////////

namespace detail {

////////////////////////////////////////////////////////////////////////

template<typename K_, typename Arg_>
struct Repeat {
    using Value = typename K_::Value;

    Repeat(K_ k) : k_(std::move(k)) {}

    template<typename Arg, typename K>
    static auto create(K k) {
        return Repeat<K, Arg>(std::move(k));
    }

    template<typename K, std::enable_if_t<IsContinuation<K>::value, int> = 0>
    auto k(K k) && {
        return create<Arg_>([&]() { return std::move(k_) | std::move(k); }());
    }

    template<typename F, std::enable_if_t<!IsContinuation<F>::value, int> = 0>
    auto k(F f) && {
        static_assert(!HasLoop<K_>::value, "Can't add *invocable* after loop");

        return std::move(*this) |
               eventuals::Map(eventuals::Lambda(std::move(f)));
    }

    template<typename... Args>
    void Start(Args&&... args) {
        if constexpr (!IsUndefined<Arg_>::value) {
            arg_.emplace(std::forward<Args>(args)...);
        }
        eventuals::succeed(k_, *this);
    }

    template<typename... Args>
    void Fail(Args&&... args) {
        eventuals::fail(k_, std::forward<Args>(args)...);
    }

    void Stop() { eventuals::stop(k_); }

    void Register(Interrupt& interrupt) { k_.Register(interrupt); }

    void Next() {
        if constexpr (!IsUndefined<Arg_>::value) {
            eventuals::body(k_, *this, std::move(*arg_));
        } else {
            eventuals::body(k_, *this);
        }
    }

    void                Done() { eventuals::ended(k_); }

    K_                  k_;

    std::optional<Arg_> arg_;
};

////////////////////////////////////////////////////////////////////////

}   // namespace detail

////////////////////////////////////////////////////////////////////////

template<typename K, typename Arg>
struct IsContinuation<detail::Repeat<K, Arg>> : std::true_type {};

////////////////////////////////////////////////////////////////////////

template<typename K, typename Arg>
struct HasTerminal<detail::Repeat<K, Arg>> : HasTerminal<K> {};

////////////////////////////////////////////////////////////////////////

template<typename K, typename Arg_>
struct Compose<detail::Repeat<K, Arg_>> {
    template<typename Arg>
    static auto compose(detail::Repeat<K, Arg_> repeat) {
        auto k = eventuals::compose<Arg>(std::move(repeat.k_));
        return detail::Repeat<decltype(k), Arg>(std::move(k));
    }
};

////////////////////////////////////////////////////////////////////////

template<typename E>
auto Repeat(E e) {
    static_assert(IsContinuation<E>::value,
                  "expecting an eventual continuation for Repeat");

    auto k = eventuals::Map(std::move(e));
    return detail::Repeat<decltype(k), Undefined>(std::move(k));
}

inline auto Repeat() { return Repeat(Forward()); }

////////////////////////////////////////////////////////////////////////

}   // namespace eventuals
}   // namespace stout

////////////////////////////////////////////////////////////////////////
