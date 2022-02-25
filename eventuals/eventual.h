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

struct _Eventual {
  template <
      typename K_,
      typename Context_,
      typename Start_,
      typename Fail_,
      typename Stop_,
      bool Interruptible_,
      typename Value_,
      typename... Errors_>
  struct Continuation final {
    Continuation(
        Reschedulable<K_, Value_> k,
        Context_ context,
        Start_ start,
        Fail_ fail,
        Stop_ stop)
      : context_(std::move(context)),
        start_(std::move(start)),
        fail_(std::move(fail)),
        stop_(std::move(stop)),
        k_(std::move(k)) {}


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

      if constexpr (!IsUndefined<Context_>::value && Interruptible_) {
        CHECK(handler_);
        start_(context_, k_(), *handler_, std::forward<Args>(args)...);
      } else if constexpr (!IsUndefined<Context_>::value && !Interruptible_) {
        start_(context_, k_(), std::forward<Args>(args)...);
      } else if constexpr (IsUndefined<Context_>::value && Interruptible_) {
        CHECK(handler_);
        start_(k_(), *handler_, std::forward<Args>(args)...);
      } else {
        start_(k_(), std::forward<Args>(args)...);
      }
    }

    template <typename Error>
    void Fail(Error&& error) {
      if constexpr (IsUndefined<Fail_>::value) {
        k_().Fail(std::move(error));
      } else if constexpr (IsUndefined<Context_>::value) {
        fail_(k_(), std::move(error));
      } else {
        fail_(context_, k_(), std::move(error));
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

      if constexpr (Interruptible_) {
        handler_.emplace(&interrupt);
      }
    }

    Context_ context_;
    Start_ start_;
    Fail_ fail_;
    Stop_ stop_;

    std::optional<Interrupt::Handler> handler_;

    // NOTE: we store 'k_' as the _last_ member so it will be
    // destructed _first_ and thus we won't have any use-after-delete
    // issues during destruction of 'k_' if it holds any references or
    // pointers to any (or within any) of the above members.
    Reschedulable<K_, Value_> k_;
  };

  template <
      typename Context_,
      typename Start_,
      typename Fail_,
      typename Stop_,
      bool Interruptible_,
      typename Value_,
      typename... Errors_>
  struct Builder final {
    template <typename>
    using ValueFrom = Value_;

    template <
        bool Interruptible,
        typename Value,
        typename... Errors,
        typename Context,
        typename Start,
        typename Fail,
        typename Stop>
    static auto create(
        Context context,
        Start start,
        Fail fail,
        Stop stop) {
      return Builder<
          Context,
          Start,
          Fail,
          Stop,
          Interruptible,
          Value,
          Errors...>{
          std::move(context),
          std::move(start),
          std::move(fail),
          std::move(stop)};
    }

    template <typename Arg, typename K>
    auto k(K k) && {
      return Continuation<
          K,
          Context_,
          Start_,
          Fail_,
          Stop_,
          Interruptible_,
          Value_,
          Errors_...>(
          Reschedulable<K, Value_>{std::move(k)},
          std::move(context_),
          std::move(start_),
          std::move(fail_),
          std::move(stop_));
    }

    template <typename Context>
    auto context(Context context) && {
      static_assert(IsUndefined<Context_>::value, "Duplicate 'context'");
      return create<Interruptible_, Value_, Errors_...>(
          std::move(context),
          std::move(start_),
          std::move(fail_),
          std::move(stop_));
    }

    template <typename Start>
    auto start(Start start) && {
      static_assert(IsUndefined<Start_>::value, "Duplicate 'start'");
      return create<Interruptible_, Value_, Errors_...>(
          std::move(context_),
          std::move(start),
          std::move(fail_),
          std::move(stop_));
    }

    template <typename Fail>
    auto fail(Fail fail) && {
      static_assert(IsUndefined<Fail_>::value, "Duplicate 'fail'");
      return create<Interruptible_, Value_, Errors_...>(
          std::move(context_),
          std::move(start_),
          std::move(fail),
          std::move(stop_));
    }

    template <typename Stop>
    auto stop(Stop stop) && {
      static_assert(IsUndefined<Stop_>::value, "Duplicate 'stop'");
      return create<Interruptible_, Value_, Errors_...>(
          std::move(context_),
          std::move(start_),
          std::move(fail_),
          std::move(stop));
    }

    auto interruptible() && {
      static_assert(!Interruptible_, "Already 'interruptible'");
      return create<true, Value_, Errors_...>(
          std::move(context_),
          std::move(start_),
          std::move(fail_),
          std::move(stop_));
    }

    Context_ context_;
    Start_ start_;
    Fail_ fail_;
    Stop_ stop_;
  };
};

////////////////////////////////////////////////////////////////////////

template <typename Value, typename... Errors>
auto Eventual() {
  return _Eventual::Builder<
      Undefined,
      Undefined,
      Undefined,
      Undefined,
      false,
      Value,
      Errors...>{};
}

////////////////////////////////////////////////////////////////////////

template <typename Value, typename... Errors, typename Start>
auto Eventual(Start start) {
  return _Eventual::Builder<
      Undefined,
      Start,
      Undefined,
      Undefined,
      false,
      Value,
      Errors...>{
      Undefined(),
      std::move(start)};
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
