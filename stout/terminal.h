#pragma once

#include <optional>

#include "stout/continuation.h"
#include "stout/interrupt.h"
#include "stout/undefined.h"

namespace stout {
namespace eventuals {

namespace detail {
  
template <
  typename Context_,
  typename Start_,
  typename Fail_,
  typename Stop_,
  typename Interrupt_>
struct Terminal
{
  using Value = Undefined;

  Context_ context_;
  Start_ start_;
  Fail_ fail_;
  Stop_ stop_;
  Interrupt_ interrupt_;

  std::optional<Interrupt::Handler> handler_;

  template <
    typename Context,
    typename Start,
    typename Fail,
    typename Stop,
    typename Interrupt>
  static auto create(
      Context context,
      Start start,
      Fail fail,
      Stop stop,
      Interrupt interrupt)
  {
    return Terminal<
      Context,
      Start,
      Fail,
      Stop,
      Interrupt> {
      std::move(context),
      std::move(start),
      std::move(fail),
      std::move(stop),
      std::move(interrupt)
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
        std::move(stop_),
        std::move(interrupt_));
  }

  template <typename Start>
  auto start(Start start) &&
  {
    static_assert(IsUndefined<Start_>::value, "Duplicate 'start'");
    return create(
        std::move(context_),
        std::move(start),
        std::move(fail_),
        std::move(stop_),
        std::move(interrupt_));
  }

  template <typename Fail>
  auto fail(Fail fail) &&
  {
    static_assert(IsUndefined<Fail_>::value, "Duplicate 'fail'");
    return create(
        std::move(context_),
        std::move(start_),
        std::move(fail),
        std::move(stop_),
        std::move(interrupt_));
  }

  template <typename Stop>
  auto stop(Stop stop) &&
  {
    static_assert(IsUndefined<Stop_>::value, "Duplicate 'stop'");
    return create(
        std::move(context_),
        std::move(start_),
        std::move(fail_),
        std::move(stop),
        std::move(interrupt_));
  }

  template <typename Interrupt>
  auto interrupt(Interrupt interrupt) &&
  {
    static_assert(IsUndefined<Interrupt_>::value, "Duplicate 'interrupt'");
    return create(
        std::move(context_),
        std::move(start_),
        std::move(fail_),
        std::move(stop_),
        std::move(interrupt));
  }

  template <typename... Args>
  void Start(Args&&... args)
  {
    static_assert(
        !IsUndefined<Start_>::value,
        "Undefined 'start' (and no default)");

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
      if constexpr (IsUndefined<Context_>::value) {
        start_(std::forward<Args>(args)...);
      } else {
        start_(context_, std::forward<Args>(args)...);
      }
    }
  }

  template <typename... Args>
  void Fail(Args&&... args)
  {
    static_assert(
        !IsUndefined<Fail_>::value,
        "Undefined 'fail' (and no default)");

    if constexpr (IsUndefined<Context_>::value) {
      fail_(std::forward<Args>(args)...);
    } else {
      fail_(context_, std::forward<Args>(args)...);
    }
  }

  void Stop()
  {
    static_assert(
        !IsUndefined<Stop_>::value,
        "Undefined 'stop' (and no default)");

    if constexpr (IsUndefined<Context_>::value) {
      stop_();
    } else {
      stop_(context_);
    }
  }

  void Register(Interrupt& interrupt)
  {
    if constexpr (!IsUndefined<Interrupt_>::value) {
      handler_.emplace(&interrupt, [this]() {
        if constexpr (IsUndefined<Context_>::value) {
          interrupt_();
        } else {
          interrupt_(context_);
        }
      });
    }
  }
};

} // namespace detail {


template <typename>
struct IsTerminal : std::false_type {};


template <
  typename Context,
  typename Start,
  typename Fail,
  typename Stop,
  typename Interrupt>
struct IsTerminal<
  detail::Terminal<
    Context,
    Start,
    Fail,
    Stop,
    Interrupt>> : std::true_type {};


template <
  typename Context,
  typename Start,
  typename Fail,
  typename Stop,
  typename Interrupt>
struct IsContinuation<
  detail::Terminal<
    Context,
    Start,
    Fail,
    Stop,
    Interrupt>> : std::true_type {};


inline auto Terminal()
{
  return detail::Terminal<
    Undefined,
    Undefined,
    Undefined,
    Undefined,
    Undefined> {
    Undefined(),
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
  typename Stop,
  typename Interrupt>
struct HasTerminal<
  detail::Terminal<
    Context,
    Start,
    Fail,
    Stop,
    Interrupt>> : std::true_type {};


template <
  typename Context,
  typename Start,
  typename Fail,
  typename Stop,
  typename Interrupt,
  typename Value>
struct ValueFrom<
  detail::Terminal<Context, Start, Fail, Stop, Interrupt>,
  Value>
{
  using type = Value;
};

} // namespace eventuals {
} // namespace stout {
