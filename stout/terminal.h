#pragma once

#include "stout/continuation.h"
#include "stout/undefined.h"

namespace stout {
namespace eventuals {

namespace detail {
  
template <
  typename Context_,
  typename Start_,
  typename Fail_,
  typename Stop_>
struct Terminal
{
  using Value = Undefined;

  Context_ context_;
  Start_ start_;
  Fail_ fail_;
  Stop_ stop_;

  template <
    typename Context,
    typename Start,
    typename Fail,
    typename Stop>
  static auto create(
      Context context,
      Start start,
      Fail fail,
      Stop stop)
  {
    return Terminal<
      Context,
      Start,
      Fail,
      Stop> {
      std::move(context),
      std::move(start),
      std::move(fail),
      std::move(stop)
    };
  }

  template <typename Context>
  auto context(Context context) &&
  {
    static_assert(IsUndefined<Context_>::value, "Duplicate 'context'");
    return create(
        std::move(context),
        std::move(start_),
        std::move(fail_),
        std::move(stop_));
  }

  template <typename Start>
  auto start(Start start) &&
  {
    static_assert(IsUndefined<Start_>::value, "Duplicate 'start'");
    return create(
        std::move(context_),
        std::move(start),
        std::move(fail_),
        std::move(stop_));
  }

  template <typename Fail>
  auto fail(Fail fail) &&
  {
    static_assert(IsUndefined<Fail_>::value, "Duplicate 'fail'");
    return create(
        std::move(context_),
        std::move(start_),
        std::move(fail),
        std::move(stop_));
  }

  template <typename Stop>
  auto stop(Stop stop) &&
  {
    static_assert(IsUndefined<Stop_>::value, "Duplicate 'stop'");
    return create(
        std::move(context_),
        std::move(start_),
        std::move(fail_),
        std::move(stop));
  }

  template <typename T>
  void Succeed(T&& t)
  {
    static_assert(
        !IsUndefined<Start_>::value,
        "Undefined 'start' (and no default)");

    if constexpr (IsUndefined<Context_>::value) {
      start_(std::forward<T>(t));
    } else {
      start_(context_, std::forward<T>(t));
    }
  }

  template <typename Error>
  void Fail(Error&& error)
  {
    static_assert(
        !IsUndefined<Start_>::value,
        "Undefined 'fail' (and no default)");

    if constexpr (IsUndefined<Context_>::value) {
      fail_(std::forward<Error>(error));
    } else {
      fail_(context_, std::forward<Error>(error));
    }
  }

  void Stop()
  {
    static_assert(
        !IsUndefined<Start_>::value,
        "Undefined 'stop' (and no default)");

    if constexpr (IsUndefined<Context_>::value) {
        stop_();
    } else {
      stop_(context_);
    }
  }
};

} // namespace detail {


template <typename>
struct IsTerminal : std::false_type {};


template <typename Context, typename Start, typename Fail, typename Stop>
struct IsTerminal<
  detail::Terminal<Context, Start, Fail, Stop>> : std::true_type {};


template <typename Context, typename Start, typename Fail, typename Stop>
struct IsContinuation<
  detail::Terminal<Context, Start, Fail, Stop>> : std::true_type {};


auto Terminal()
{
  return detail::Terminal<Undefined, Undefined, Undefined, Undefined> {
    Undefined(),
    Undefined(),
    Undefined(),
    Undefined()
  };
}


template <typename>
struct HasTerminal : std::false_type {};


template <
  typename Context,
  typename Start,
  typename Fail,
  typename Stop>
struct HasTerminal<
  detail::Terminal<
    Context,
    Start,
    Fail,
    Stop>> : std::true_type {};

} // namespace eventuals {
} // namespace stout {
