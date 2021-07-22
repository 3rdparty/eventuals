#pragma once

#include "stout/eventual.h"
#include "stout/invoke-result.h"
#include "stout/just.h"
#include "stout/stream.h"
#include "stout/then.h"
#include "stout/transform.h"

////////////////////////////////////////////////////////////////////////

namespace stout {
namespace eventuals {

////////////////////////////////////////////////////////////////////////

namespace detail {

////////////////////////////////////////////////////////////////////////

template<typename K_, typename Condition_, typename Arg_>
struct Until {
    using Value = typename ValueFrom<K_, Arg_>::type;

    Until(K_ k, Condition_ condition) :
        k_(std::move(k)),
        condition_(std::move(condition)) {}

    template<typename Arg, typename K, typename Condition>
    static auto create(K k, Condition condition) {
        return Until<K, Condition, Arg>(std::move(k), std::move(condition));
    }

    template<typename K, std::enable_if_t<IsContinuation<K>::value, int> = 0>
    auto k(K k) && {
        return create<Arg_>(
            [&]() {
                if constexpr (!IsUndefined<K_>::value) {
                    return std::move(k_) | std::move(k);
                } else {
                    return std::move(k);
                }
            }(),
            std::move(condition_));
    }

    template<typename F, std::enable_if_t<!IsContinuation<F>::value, int> = 0>
    auto k(F f) && {
        static_assert(!HasLoop<K_>::value, "Can't add *invocable* after loop");

        return std::move(*this) |
               eventuals::Map(eventuals::Lambda(std::move(f)));
    }

    template<typename... Args>
    void Start(Args&&... args) {
        eventuals::succeed(k_, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void Fail(Args&&... args) {
        eventuals::fail(k_, std::forward<Args>(args)...);
    }

    void Stop() { eventuals::stop(k_); }

    void Register(Interrupt& interrupt) {
        interrupt_ = &interrupt;
        k_.Register(interrupt);
    }

    template<typename K, typename... Args>
    void Body(K& k, Args&&... args) {
        if constexpr (IsContinuation<Condition_>::value) {
            static_assert(sizeof...(args) == 0 || sizeof...(args) == 1,
                          "'Until' only supports 0 or 1 value");

            static_assert(sizeof...(args) == 0 ||
                              std::is_convertible_v<std::tuple<Args...>,
                                                    std::tuple<Arg_>>,
                          "Expecting a different type");

            if constexpr (sizeof...(args) == 1) {
                arg_.emplace(std::forward<Args>(args)...);
            }

            if (!adaptor_) {
                if constexpr (sizeof...(args) > 0) {
                    adaptor_.emplace(
                        std::move(condition_) |
                        Adaptor<K_, bool, std::optional<Arg_>*>(
                            k_, &arg_, [&k](auto& k_, auto* arg_, bool done) {
                                if (done) {
                                    eventuals::done(k);
                                } else {
                                    eventuals::body(k_, k, std::move(**arg_));
                                }
                            }));
                } else {
                    adaptor_.emplace(
                        std::move(condition_) |
                        Adaptor<K_, bool, std::optional<Arg_>*>(
                            k_, nullptr, [&k](auto& k_, auto*, bool done) {
                                if (done) {
                                    eventuals::done(k);
                                } else {
                                    eventuals::body(k_, k);
                                }
                            }));
                }

                if (interrupt_ != nullptr) { adaptor_->Register(*interrupt_); }
            }

            if constexpr (sizeof...(args) > 0) {
                eventuals::succeed(*adaptor_,
                                   *arg_);   // NOTE: passing '&' not '&&'.
            } else {
                eventuals::succeed(*adaptor_);
            }
        } else {
            if (condition_(args...)) {
                eventuals::done(k);
            } else {
                eventuals::body(k_, k, std::forward<Args>(args)...);
            }
        }
    }

    void                Ended() { eventuals::ended(k_); }

    K_                  k_;
    Condition_          condition_;

    Interrupt*          interrupt_ = nullptr;

    std::optional<Arg_> arg_;

    using E_       = std::conditional_t<IsContinuation<Condition_>::value,
                                  Condition_, Undefined>;

    using Adaptor_ = typename EKPossiblyUndefined<
        E_, Adaptor<K_, bool, std::optional<Arg_>*>>::type;

    std::optional<Adaptor_> adaptor_;
};

////////////////////////////////////////////////////////////////////////

}   // namespace detail

////////////////////////////////////////////////////////////////////////

template<typename K, typename Condition, typename Arg>
struct IsContinuation<detail::Until<K, Condition, Arg>> : std::true_type {};

////////////////////////////////////////////////////////////////////////

template<typename K, typename Condition, typename Arg>
struct HasTerminal<detail::Until<K, Condition, Arg>> : HasTerminal<K> {};

////////////////////////////////////////////////////////////////////////

template<typename K, typename Condition, typename Arg_>
struct Compose<detail::Until<K, Condition, Arg_>> {
    template<typename Arg>
    static auto compose(detail::Until<K, Condition, Arg_> until) {
        auto k = eventuals::compose<Arg>(std::move(until.k_));
        return detail::Until<decltype(k), Condition, Arg>(
            std::move(k), std::move(until.condition_));
    }
};

////////////////////////////////////////////////////////////////////////

template<typename Condition>
auto Until(Condition condition) {
    return detail::Until<Undefined, Condition, Undefined>(
        Undefined(), std::move(condition));
}

////////////////////////////////////////////////////////////////////////

}   // namespace eventuals
}   // namespace stout

////////////////////////////////////////////////////////////////////////
