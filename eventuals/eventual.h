#pragma once

#include <optional>

#include "eventuals/compose.h"
#include "eventuals/interrupt.h"
#include "eventuals/scheduler.h"
#include "eventuals/undefined.h"

// TODO(benh): catch exceptions from 'start', 'fail', 'stop', etc.
//
// TODO(benh): aggregate errors across all the eventuals.
//
// TODO(benh): lambda visitor for matching errors.

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

namespace detail {

////////////////////////////////////////////////////////////////////////

struct _Eventual {
  template <
      typename K_,
      typename Context_,
      typename Start_,
      typename Fail_,
      typename Stop_,
      typename Interrupt_,
      typename Value_,
      typename... Errors_>
  struct Continuation {
    Continuation(Continuation&& that) = default;

    Continuation& operator=(Continuation&& that) {
      // TODO(benh): lambdas don't have an 'operator=()' until C++20 so
      // we have to effectively do a "reset" and "emplace" (as though it
      // was stored in a 'std::optional' but without the overhead of
      // optionals everywhere).
      this->~Continuation();
      new (this) Continuation(std::move(that));

      return *this;
    }

    template <typename... Args>
    void Start(Args&&... args) {
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
          start_(k_(), std::forward<Args>(args)...);
        } else {
          start_(context_, k_(), std::forward<Args>(args)...);
        }
      }
    }

    template <typename... Args>
    void Fail(Args&&... args) {
      if constexpr (IsUndefined<Fail_>::value) {
        k_().Fail(std::forward<Args>(args)...);
      } else if constexpr (IsUndefined<Context_>::value) {
        fail_(k_(), std::forward<Args>(args)...);
      } else {
        fail_(context_, k_(), std::forward<Args>(args)...);
      }
    }

    void Stop() {
      if constexpr (IsUndefined<Stop_>::value) {
        k_().Stop();
      } else if constexpr (IsUndefined<Context_>::value) {
        stop_(k_());
      } else {
        stop_(context_, k_());
      }
    }

    void Register(Interrupt& interrupt) {
      k_.Register(interrupt);

      if constexpr (!IsUndefined<Interrupt_>::value) {
        handler_.emplace(&interrupt, [this]() {
          if constexpr (IsUndefined<Context_>::value) {
            interrupt_(k_());
          } else {
            interrupt_(context_, k_());
          }
        });
      }
    }

    Reschedulable<K_, Value_> k_;
    Context_ context_;
    Start_ start_;
    Fail_ fail_;
    Stop_ stop_;
    Interrupt_ interrupt_;

    std::optional<Interrupt::Handler> handler_;
  };

  template <
      typename Context_,
      typename Start_,
      typename Fail_,
      typename Stop_,
      typename Interrupt_,
      typename Value_,
      typename... Errors_>
  struct Builder {
    template <typename>
    using ValueFrom = Value_;

    template <
        typename Value,
        typename... Errors,
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
        Interrupt interrupt) {
      return Builder<
          Context,
          Start,
          Fail,
          Stop,
          Interrupt,
          Value,
          Errors...>{
          std::move(context),
          std::move(start),
          std::move(fail),
          std::move(stop),
          std::move(interrupt)};
    }

    template <typename Arg, typename K>
    auto k(K k) && {
      return Continuation<
          K,
          Context_,
          Start_,
          Fail_,
          Stop_,
          Interrupt_,
          Value_,
          Errors_...>{
          Reschedulable<K, Value_>{std::move(k)},
          std::move(context_),
          std::move(start_),
          std::move(fail_),
          std::move(stop_),
          std::move(interrupt_)};
    }

    template <typename Context>
    auto context(Context context) && {
      static_assert(IsUndefined<Context_>::value, "Duplicate 'context'");
      return create<Value_, Errors_...>(
          std::move(context),
          std::move(start_),
          std::move(fail_),
          std::move(stop_),
          std::move(interrupt_));
    }

    template <typename Start>
    auto start(Start start) && {
      static_assert(IsUndefined<Start_>::value, "Duplicate 'start'");
      return create<Value_, Errors_...>(
          std::move(context_),
          std::move(start),
          std::move(fail_),
          std::move(stop_),
          std::move(interrupt_));
    }

    template <typename Fail>
    auto fail(Fail fail) && {
      static_assert(IsUndefined<Fail_>::value, "Duplicate 'fail'");
      return create<Value_, Errors_...>(
          std::move(context_),
          std::move(start_),
          std::move(fail),
          std::move(stop_),
          std::move(interrupt_));
    }

    template <typename Stop>
    auto stop(Stop stop) && {
      static_assert(IsUndefined<Stop_>::value, "Duplicate 'stop'");
      return create<Value_, Errors_...>(
          std::move(context_),
          std::move(start_),
          std::move(fail_),
          std::move(stop),
          std::move(interrupt_));
    }

    template <typename Interrupt>
    auto interrupt(Interrupt interrupt) && {
      static_assert(IsUndefined<Interrupt_>::value, "Duplicate 'interrupt'");
      return create<Value_, Errors_...>(
          std::move(context_),
          std::move(start_),
          std::move(fail_),
          std::move(stop_),
          std::move(interrupt));
    }

    Context_ context_;
    Start_ start_;
    Fail_ fail_;
    Stop_ stop_;
    Interrupt_ interrupt_;
  };
};

////////////////////////////////////////////////////////////////////////

} // namespace detail

////////////////////////////////////////////////////////////////////////

template <typename Value, typename... Errors>
auto Eventual() {
  return detail::_Eventual::Builder<
      Undefined,
      Undefined,
      Undefined,
      Undefined,
      Undefined,
      Value,
      Errors...>{};
}

////////////////////////////////////////////////////////////////////////

template <typename Value, typename... Errors, typename Start>
auto Eventual(Start start) {
  return detail::_Eventual::Builder<
      Undefined,
      Start,
      Undefined,
      Undefined,
      Undefined,
      Value,
      Errors...>{
      Undefined(),
      std::move(start)};
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
