#pragma once

#include "stout/loop.h"
#include "stout/terminal.h"
#include "stout/undefined.h"

namespace stout {
namespace eventuals {

namespace detail {

template <
  typename Value_,
  typename K_,
  typename Context_,
  typename Start_,
  typename Body_,
  typename Ended_,
  typename Fail_,
  typename Stop_,
  typename... Errors_>
struct Transform
{
  using Value = Value_;

  K_ k_;

  Context_ context_;
  Start_ start_;
  Body_ body_;
  Ended_ ended_;
  Fail_ fail_;
  Stop_ stop_;

  template <
    typename Value,
    typename... Errors,
    typename K,
    typename Context,
    typename Start,
    typename Body,
    typename Ended,
    typename Fail,
    typename Stop>
  static auto create(
      K k,
      Context context,
      Start start,
      Body body,
      Ended ended,
      Fail fail,
      Stop stop)
  {
    return Transform<
      Value,
      K,
      Context,
      Start,
      Body,
      Ended,
      Fail,
      Stop,
      Errors...> {
      std::move(k),
      std::move(context),
      std::move(start),
      std::move(body),
      std::move(ended),
      std::move(fail),
      std::move(stop)
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
        std::move(stop_));
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
        std::move(stop_));
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
        std::move(stop_));
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
        std::move(stop_));
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
        std::move(stop_));
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
        std::move(stop_));
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
        std::move(stop));
  }

  template <typename S>
  void Succeed(S& s)
  {
    if constexpr (IsUndefined<Start_>::value) {
      stout::eventuals::succeed(k_, s);
    } else if constexpr (IsUndefined<Context_>::value) {
      start_(k_, s);
    } else {
      start_(context_, k_, s);
    }
  }

  template <typename S, typename T>
  void Body(S& s, T&& t)
  {
    static_assert(
        !IsUndefined<Body_>::value,
        "Undefined 'body' (and no default)");

    if constexpr (IsUndefined<Context_>::value) {
      body_(k_, s, std::forward<T>(t));
    } else {
      body_(context_, k_, s, std::forward<T>(t));
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

  template <typename Error>
  void Fail(Error&& error)
  {
    static_assert(
        !IsUndefined<Fail_>::value,
        "Undefined 'fail' (and no default)");

    if constexpr (IsUndefined<Context_>::value) {
      fail_(k_, std::forward<Error>(error));
    } else {
      fail_(context_, k_, std::forward<Error>(error));
    }
  }

  void Stop()
  {
    static_assert(
        !IsUndefined<Stop_>::value,
        "Undefined 'stop' (and no default)");

    if constexpr (IsUndefined<Context_>::value) {
      stop_(k_);
    } else {
      stop_(context_, k_);
    }
  }
};

} // namespace detail {


template <typename>
struct IsTransform : std::false_type {};


template <
  typename Value,
  typename K,
  typename Context,
  typename Start,
  typename Body,
  typename Ended,
  typename Fail,
  typename Stop,
  typename... Errors>
struct IsTransform<
  detail::Transform<
    Value,
    K,
    Context,
    Start,
    Body,
    Ended,
    Fail,
    Stop,
    Errors...>> : std::true_type {};


template <
  typename Value,
  typename K,
  typename Context,
  typename Start,
  typename Body,
  typename Ended,
  typename Fail,
  typename Stop,
  typename... Errors>
struct IsContinuation<
  detail::Transform<
    Value,
    K,
    Context,
    Start,
    Body,
    Ended,
    Fail,
    Stop,
    Errors...>> : std::true_type {};


template <
  typename Value,
  typename K,
  typename Context,
  typename Start,
  typename Body,
  typename Ended,
  typename Fail,
  typename Stop,
  typename... Errors>
struct HasLoop<
  detail::Transform<
    Value,
    K,
    Context,
    Start,
    Body,
    Ended,
    Fail,
    Stop,
    Errors...>> : HasLoop<K> {};


template <
  typename Value,
  typename K,
  typename Context,
  typename Start,
  typename Body,
  typename Ended,
  typename Fail,
  typename Stop,
  typename... Errors>
struct HasTerminal<
  detail::Transform<
    Value,
    K,
    Context,
    Start,
    Body,
    Ended,
    Fail,
    Stop,
    Errors...>> : HasTerminal<K> {};


template <typename Value, typename... Errors>
auto Transform()
{
  return detail::Transform<
    Value,
    Undefined,
    Undefined,
    Undefined,
    Undefined,
    Undefined,
    Undefined,
    Undefined,
    Errors...> {
    Undefined(),
    Undefined(),
    Undefined(),
    Undefined(),
    Undefined(),
    Undefined(),
    Undefined()
  };
}


template <typename Value, typename F>
auto map(F f)
{
  return Transform<Value>()
    .context(std::move(f))
    .start([](auto&, auto& k, auto& stream) {
      succeed(k, stream);
    })
    .body([](auto& f, auto& k, auto& stream, auto&& value) {
      body(k, stream, f(std::forward<decltype(value)>(value)));
    })
    .ended([](auto&, auto& k) {
      ended(k);
    })
    .fail([](auto&, auto& k, auto&& error) {
      fail(k, std::forward<decltype(error)>(error));
    })
    .stop([](auto&, auto& k) {
      stop(k);
    });
}

} // namespace eventuals {
} // namespace stout {
