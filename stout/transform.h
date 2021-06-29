#pragma once

#include "stout/adaptor.h"
#include "stout/interrupt.h"
#include "stout/loop.h"
#include "stout/terminal.h"
#include "stout/then.h"
#include "stout/undefined.h"

////////////////////////////////////////////////////////////////////////

namespace stout {
namespace eventuals {

////////////////////////////////////////////////////////////////////////

// Forward declaration to break circular dependency with stream.h.
template <typename K>
void next(K& k);

////////////////////////////////////////////////////////////////////////

namespace detail {

////////////////////////////////////////////////////////////////////////

template <
  typename K_,
  typename Context_,
  typename Start_,
  typename Body_,
  typename Ended_,
  typename Fail_,
  typename Stop_,
  typename Interrupt_,
  typename Value_,
  typename... Errors_>
struct Transform
{
  using Value = typename ValueFrom<K_, Value_>::type;

  K_ k_;

  Context_ context_;
  Start_ start_;
  Body_ body_;
  Ended_ ended_;
  Fail_ fail_;
  Stop_ stop_;
  Interrupt_ interrupt_;

  std::optional<Interrupt::Handler> handler_;

  template <
    typename Value,
    typename... Errors,
    typename K,
    typename Context,
    typename Start,
    typename Body,
    typename Ended,
    typename Fail,
    typename Stop,
    typename Interrupt>
  static auto create(
      K k,
      Context context,
      Start start,
      Body body,
      Ended ended,
      Fail fail,
      Stop stop,
      Interrupt interrupt)
  {
    return Transform<
      K,
      Context,
      Start,
      Body,
      Ended,
      Fail,
      Stop,
      Interrupt,
      Value,
      Errors...> {
      std::move(k),
      std::move(context),
      std::move(start),
      std::move(body),
      std::move(ended),
      std::move(fail),
      std::move(stop),
      std::move(interrupt)
    };
  }

  template <typename K>
  auto k(K k) &&
  {
    static_assert(
        IsLoop<K>::value || IsTerminal<K>::value,
        "Expecting a 'Loop' or a 'Terminal'");

    static_assert(
        !IsLoop<K>::value || !HasLoop<K_>::value,
        "Redundant 'Loop''");

    static_assert(
        !IsTerminal<K>::value || !HasTerminal<K_>::value,
        "Redundant 'Terminal''");

    return create<Value_, Errors_...>(
        [&]() {
          if constexpr (!IsUndefined<K_>::value) {
            return std::move(k_) | std::move(k);
          } else {
            return std::move(k);
          }
        }(),
        std::move(context_),
        std::move(start_),
        std::move(body_),
        std::move(ended_),
        std::move(fail_),
        std::move(stop_),
        std::move(interrupt_));
  }

  template <typename Context>
  auto context(Context context) &&
  {
    static_assert(IsUndefined<Context_>::value, "Duplicate 'context'");
    return create<Value_, Errors_...>(
        std::move(k_),
        std::move(context),
        std::move(start_),
        std::move(body_),
        std::move(ended_),
        std::move(fail_),
        std::move(stop_),
        std::move(interrupt_));
  }

  template <typename Start>
  auto start(Start start) &&
  {
    static_assert(IsUndefined<Start_>::value, "Duplicate 'start'");
    return create<Value_, Errors_...>(
        std::move(k_),
        std::move(context_),
        std::move(start),
        std::move(body_),
        std::move(ended_),
        std::move(fail_),
        std::move(stop_),
        std::move(interrupt_));
  }

  template <typename Body>
  auto body(Body body) &&
  {
    static_assert(IsUndefined<Body_>::value, "Duplicate 'body'");
    return create<Value_, Errors_...>(
        std::move(k_),
        std::move(context_),
        std::move(start_),
        std::move(body),
        std::move(ended_),
        std::move(fail_),
        std::move(stop_),
        std::move(interrupt_));
  }

  template <typename Ended>
  auto ended(Ended ended) &&
  {
    static_assert(IsUndefined<Ended_>::value, "Duplicate 'ended'");
    return create<Value_, Errors_...>(
        std::move(k_),
        std::move(context_),
        std::move(start_),
        std::move(body_),
        std::move(ended),
        std::move(fail_),
        std::move(stop_),
        std::move(interrupt_));
  }

  template <typename Fail>
  auto fail(Fail fail) &&
  {
    static_assert(IsUndefined<Fail_>::value, "Duplicate 'fail'");
    return create<Value_, Errors_...>(
        std::move(k_),
        std::move(context_),
        std::move(start_),
        std::move(body_),
        std::move(ended_),
        std::move(fail),
        std::move(stop_),
        std::move(interrupt_));
  }

  template <typename Stop>
  auto stop(Stop stop) &&
  {
    static_assert(IsUndefined<Stop_>::value, "Duplicate 'stop'");
    return create<Value_, Errors_...>(
        std::move(k_),
        std::move(context_),
        std::move(start_),
        std::move(body_),
        std::move(ended_),
        std::move(fail_),
        std::move(stop),
        std::move(interrupt_));
  }

  template <typename Interrupt>
  auto interrupt(Interrupt interrupt) &&
  {
    static_assert(IsUndefined<Interrupt_>::value, "Duplicate 'interrupt'");
    return create<Value_, Errors_...>(
        std::move(k_),
        std::move(context_),
        std::move(start_),
        std::move(body_),
        std::move(ended_),
        std::move(fail_),
        std::move(stop_),
        std::move(interrupt));
  }

  template <typename... Args>
  void Start(Args&&... args)
  {
    auto interrupted = [this]() mutable {
      if (handler_) {
        return !handler_->Install();
      } else {
        return false;
      }
    }();

    if (interrupted) {
      assert(handler_);
      handler_->Invoke();
    } else {
      if constexpr (IsUndefined<Start_>::value) {
        eventuals::succeed(k_, std::forward<Args>(args)...);
      } else if constexpr (IsUndefined<Context_>::value) {
        start_(k_, std::forward<Args>(args)...);
      } else {
        start_(context_, k_, std::forward<Args>(args)...);
      }
    }
  }

  template <typename... Args>
  void Fail(Args&&... args)
  {
    if constexpr (IsUndefined<Start_>::value) {
      eventuals::fail(k_, std::forward<Args>(args)...);
    } else if constexpr (IsUndefined<Context_>::value) {
      fail_(k_, std::forward<Args>(args)...);
    } else {
      fail_(context_, k_, std::forward<Args>(args)...);
    }
  }

  void Stop()
  {
    if constexpr (IsUndefined<Start_>::value) {
      eventuals::stop(k_);
    } else if constexpr (IsUndefined<Context_>::value) {
      stop_(k_);
    } else {
      stop_(context_, k_);
    }
  }

  void Register(Interrupt& interrupt)
  {
    k_.Register(interrupt);

    if constexpr (!IsUndefined<Interrupt_>::value) {
      handler_.emplace(&interrupt, [this]() {
        if constexpr (IsUndefined<Context_>::value) {
          interrupt_(k_);
        } else {
          interrupt_(context_, k_);
        }
      });
    }
  }

  template <typename... Args>
  void Body(Args&&... args)
  {
    static_assert(
        !IsUndefined<Body_>::value,
        "Undefined 'body' (and no default)");

    if constexpr (IsUndefined<Context_>::value) {
      body_(k_, std::forward<Args>(args)...);
    } else {
      body_(context_, k_, std::forward<Args>(args)...);
    }
  }

  void Ended()
  {
    static_assert(
        !IsUndefined<Ended_>::value,
        "Undefined 'ended' (and no default)");

    if constexpr (IsUndefined<Context_>::value) {
      ended_(k_);
    } else {
      ended_(context_, k_);
    }
  }
};

////////////////////////////////////////////////////////////////////////

template <typename K_, typename E_, typename Arg_>
struct Map
{
  using Value = typename ValueFrom<K_, typename E_::Value>::type;

  Map(K_ k, E_ e)
    : k_(std::move(k)), e_(std::move(e)) {}

  template <typename Arg, typename K, typename E>
  static auto create(K k, E e)
  {
    return Map<K, E, Arg>(std::move(k), std::move(e));
  }

  template <typename K>
  auto k(K k) &&
  {
    return create<Arg_>(
        [&]() {
          if constexpr (!IsUndefined<K_>::value) {
            return std::move(k_) | std::move(k);
          } else {
            return std::move(k);
          }
        }(),
        std::move(e_));
  }

  template <typename... Args>
  void Start(Args&&... args)
  {
    eventuals::succeed(k_, std::forward<Args>(args)...);
  }

  template <typename... Args>
  void Fail(Args&&... args)
  {
    // TODO(benh): do we need to fail via the eventual?
    eventuals::fail(k_, std::forward<Args>(args)...);
  }

  void Stop()
  {
    // TODO(benh): do we need to stop via the eventual?
    eventuals::stop(k_);
  }

  template <typename K, typename... Args>
  void Body(K& k, Args&&... args)
  {
    if (!adaptor_) {
      if constexpr (!IsUndefined<typename E_::Value>::value) {
        // NOTE: in order to have a single function signature for
        // 'body_' we assume we'll have an rvalue reference '&&'. If
        // we wanted to support lvalue references '&' we'd need to
        // modify 'Adaptor'. One way to do this would be to have
        // 'Adaptor' always std::move() so that the function signature
        // can always take an rvalue reference '&&' and then also an
        // extra boolean that specifies whether or not the it should
        // be moved again.
        adaptor_.emplace(
            std::move(e_).k(
                Adaptor<K_, typename E_::Value>(
                  k_,
                  [&k](auto& k_, typename E_::Value&& value) {
                    eventuals::body(k_, k, std::move(value));
                  })));
      } else {
        adaptor_.emplace(
            std::move(e_).k(
                Adaptor<K_, Undefined>(
                  k_,
                  [&k](auto& k_) {
                    eventuals::body(k_, k);
                  })));
      }

      if (interrupt_ != nullptr) {
        adaptor_->Register(*interrupt_);
      }
    }

    assert(adaptor_);
    eventuals::succeed(*adaptor_, std::forward<Args>(args)...);
  }

  void Ended()
  {
    eventuals::ended(k_);
  }

  void Register(Interrupt& interrupt)
  {
    interrupt_ = &interrupt;
    k_.Register(interrupt);
  }

  K_ k_;
  E_ e_;

  using Adaptor_ = typename EKPossiblyUndefined<
    E_,
    Adaptor<K_, typename E_::Value>>::type;

  std::optional<Adaptor_> adaptor_;

  Interrupt* interrupt_ = nullptr;
};

////////////////////////////////////////////////////////////////////////

} // namespace detail {

////////////////////////////////////////////////////////////////////////

template <typename>
struct IsTransform : std::false_type {};

////////////////////////////////////////////////////////////////////////

template <
  typename K,
  typename Context,
  typename Start,
  typename Body,
  typename Ended,
  typename Fail,
  typename Stop,
  typename Interrupt,
  typename Value,
  typename... Errors>
struct IsTransform<
  detail::Transform<
    K,
    Context,
    Start,
    Body,
    Ended,
    Fail,
    Stop,
    Interrupt,
    Value,
    Errors...>> : std::true_type {};

////////////////////////////////////////////////////////////////////////

template <
  typename K,
  typename Context,
  typename Start,
  typename Body,
  typename Ended,
  typename Fail,
  typename Stop,
  typename Interrupt,
  typename Value,
  typename... Errors>
struct IsContinuation<
  detail::Transform<
    K,
    Context,
    Start,
    Body,
    Ended,
    Fail,
    Stop,
    Interrupt,
    Value,
    Errors...>> : std::true_type {};

////////////////////////////////////////////////////////////////////////

template <
  typename K,
  typename Context,
  typename Start,
  typename Body,
  typename Ended,
  typename Fail,
  typename Stop,
  typename Interrupt,
  typename Value,
  typename... Errors>
struct HasLoop<
  detail::Transform<
    K,
    Context,
    Start,
    Body,
    Ended,
    Fail,
    Stop,
    Interrupt,
    Value,
    Errors...>> : HasLoop<K> {};

////////////////////////////////////////////////////////////////////////

template <
  typename K,
  typename Context,
  typename Start,
  typename Body,
  typename Ended,
  typename Fail,
  typename Stop,
  typename Interrupt,
  typename Value,
  typename... Errors>
struct HasTerminal<
  detail::Transform<
    K,
    Context,
    Start,
    Body,
    Ended,
    Fail,
    Stop,
    Interrupt,
    Value,
    Errors...>> : HasTerminal<K> {};

////////////////////////////////////////////////////////////////////////

template <typename Value, typename... Errors>
auto Transform()
{
  return detail::Transform<
    Undefined,
    Undefined,
    Undefined,
    Undefined,
    Undefined,
    Undefined,
    Undefined,
    Undefined,
    Value,
    Errors...> {
    Undefined(),
    Undefined(),
    Undefined(),
    Undefined(),
    Undefined(),
    Undefined(),
    Undefined(),
    Undefined()
  };
}

////////////////////////////////////////////////////////////////////////

template <typename K, typename E, typename Arg>
struct IsTransform<
  detail::Map<K, E, Arg>> : std::true_type {};

////////////////////////////////////////////////////////////////////////

template <typename K, typename E, typename Arg>
struct IsContinuation<
  detail::Map<K, E, Arg>> : std::true_type {};

////////////////////////////////////////////////////////////////////////

template <typename K, typename E, typename Arg>
struct HasLoop<
  detail::Map<K, E, Arg>> : HasLoop<K> {};

////////////////////////////////////////////////////////////////////////

template <typename K, typename E, typename Arg>
struct HasTerminal<
  detail::Map<K, E, Arg>> : HasTerminal<K> {};

////////////////////////////////////////////////////////////////////////

template <typename K, typename E, typename Arg_>
struct Compose<detail::Map<K, E, Arg_>>
{
  template <typename Arg>
  static auto compose(detail::Map<K, E, Arg_> map)
  {
    auto e = eventuals::compose<Arg>(std::move(map.e_));
    auto k = eventuals::compose<typename decltype(e)::Value>(std::move(map.k_));
    return detail::Map<decltype(k), decltype(e), Arg_>(
        std::move(k),
        std::move(e));
  }
};

////////////////////////////////////////////////////////////////////////

template <
  typename E,
  std::enable_if_t<
    IsContinuation<E>::value, int> = 0>
auto Map(E e)
{
  return detail::Map<Undefined, E, Undefined>(Undefined(), std::move(e));
}

template <
  typename F,
  std::enable_if_t<
    !IsContinuation<F>::value, int> = 0>
auto Map(F f)
{
  return Map(eventuals::Then(std::move(f)));
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals {
} // namespace stout {

////////////////////////////////////////////////////////////////////////
