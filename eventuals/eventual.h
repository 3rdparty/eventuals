#pragma once

#include <optional>

#include "eventuals/compose.h"
#include "eventuals/interrupt.h"
#include "eventuals/scheduler.h"
#include "eventuals/undefined.h"

// TODO(benh): catch exceptions from 'start', 'fail', 'stop', etc.

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

struct _Eventual {
  // Helper struct for enforcing that values and errors are only
  // propagated of the correct type.
  template <
      typename K_,
      typename Value_,
      typename Raises_,
      typename ReschedulableErrors_>
  struct Adaptor final {
    template <typename... Args>
    void Start(Args&&... args) {
      // TODO(benh): ensure 'Args' and 'Value_' are compatible.
      (*k_)().Start(std::forward<Args>(args)...);
    }

    template <typename Error>
    void Fail(Error&& error) {
      static_assert(
          check_errors_v<Error>,
          "Expecting a type derived from eventuals::Error");

      static_assert(
          tuple_types_contains_subtype_v<std::decay_t<Error>, Raises_>,
          "Error is not specified in 'raises<...>()'");

      (*k_)().Fail(std::forward<Error>(error));
    }

    void Stop() {
      (*k_)().Stop();
    }

    void Register(Interrupt& interrupt) {
      (*k_)().Register(interrupt);
    }

    Reschedulable<K_, Value_, ReschedulableErrors_>* k_ = nullptr;
  };

  template <
      typename K_,
      typename Context_,
      typename Start_,
      typename Fail_,
      typename Stop_,
      bool Interruptible_,
      typename Value_,
      typename Raises_,
      typename ReschedulableErrors_>
  struct Continuation final {
    Continuation(
        Reschedulable<K_, Value_, ReschedulableErrors_> k,
        Context_ context,
        Start_ start,
        Fail_ fail,
        Stop_ stop)
      : context_(std::move(context)),
        start_(std::move(start)),
        fail_(std::move(fail)),
        stop_(std::move(stop)),
        k_(std::move(k)) {}

    Continuation(Continuation&& that) noexcept = default;

    Continuation& operator=(Continuation&& that) noexcept {
      if (this == &that) {
        return *this;
      }

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
        start_(context_, adaptor(), handler_, std::forward<Args>(args)...);
      } else if constexpr (!IsUndefined<Context_>::value && !Interruptible_) {
        start_(context_, adaptor(), std::forward<Args>(args)...);
      } else if constexpr (IsUndefined<Context_>::value && Interruptible_) {
        start_(adaptor(), handler_, std::forward<Args>(args)...);
      } else {
        start_(adaptor(), std::forward<Args>(args)...);
      }
    }

    template <typename Error>
    void Fail(Error&& error) {
      if constexpr (IsUndefined<Fail_>::value) {
        k_().Fail(std::forward<Error>(error));
      } else if constexpr (IsUndefined<Context_>::value) {
        if constexpr (Interruptible_) {
          fail_(adaptor(), handler_, std::forward<Error>(error));
        } else {
          fail_(adaptor(), std::forward<Error>(error));
        }
      } else {
        if constexpr (Interruptible_) {
          fail_(context_, adaptor(), handler_, std::forward<Error>(error));
        } else {
          fail_(context_, adaptor(), std::forward<Error>(error));
        }
      }
    }

    void Stop() {
      if constexpr (IsUndefined<Stop_>::value) {
        k_().Stop();
      } else if constexpr (IsUndefined<Context_>::value) {
        if constexpr (Interruptible_) {
          stop_(adaptor(), handler_);
        } else {
          stop_(adaptor());
        }
      } else {
        if constexpr (Interruptible_) {
          stop_(context_, adaptor(), handler_);
        } else {
          stop_(context_, adaptor());
        }
      }
    }

    void Register(Interrupt& interrupt) {
      k_.Register(interrupt);

      if constexpr (Interruptible_) {
        handler_.emplace(&interrupt);
      }
    }

    Adaptor<K_, Value_, Raises_, ReschedulableErrors_>& adaptor() {
      // Note: needed to delay doing this until now because this
      // eventual might have been moved before being started.
      adaptor_.k_ = &k_;

      // And also need to capture any reschedulable context!
      k_();

      return adaptor_;
    }

    Context_ context_;
    Start_ start_;
    Fail_ fail_;
    Stop_ stop_;

    std::optional<Interrupt::Handler> handler_;

    Adaptor<K_, Value_, Raises_, ReschedulableErrors_> adaptor_;

    // NOTE: we store 'k_' as the _last_ member so it will be
    // destructed _first_ and thus we won't have any use-after-delete
    // issues during destruction of 'k_' if it holds any references or
    // pointers to any (or within any) of the above members.
    Reschedulable<K_, Value_, ReschedulableErrors_> k_;
  };

  template <
      typename Context_,
      typename Start_,
      typename Fail_,
      typename Stop_,
      bool Interruptible_,
      typename Value_,
      typename Raises_ = std::tuple<>>
  struct Builder final {
    template <typename Arg, typename Errors>
    using ValueFrom = Value_;

    template <typename Arg, typename Errors>
    using ErrorsFrom = tuple_types_union_t<Raises_, Errors>;

    template <typename Downstream>
    static constexpr bool CanCompose = Downstream::ExpectsValue;

    using Expects = SingleValue;

    template <
        bool Interruptible,
        typename Value,
        typename Raises,
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
          Raises>{
          std::move(context),
          std::move(start),
          std::move(fail),
          std::move(stop)};
    }

    template <typename Arg, typename Errors, typename K>
    auto k(K k) && {
      using ReschedulableErrors =
          std::conditional_t<
              IsUndefined<Fail_>::value,
              tuple_types_union_t<Raises_, Errors>,
              Raises_>;

      return Continuation<
          K,
          Context_,
          Start_,
          Fail_,
          Stop_,
          Interruptible_,
          Value_,
          Raises_,
          ReschedulableErrors>(
          std::move(k),
          std::move(context_),
          std::move(start_),
          std::move(fail_),
          std::move(stop_));
    }

    template <typename Context>
    auto context(Context context) && {
      static_assert(IsUndefined<Context_>::value, "Duplicate 'context'");
      return create<Interruptible_, Value_, Raises_>(
          std::move(context),
          std::move(start_),
          std::move(fail_),
          std::move(stop_));
    }

    template <typename Start>
    auto start(Start start) && {
      static_assert(IsUndefined<Start_>::value, "Duplicate 'start'");
      return create<Interruptible_, Value_, Raises_>(
          std::move(context_),
          std::move(start),
          std::move(fail_),
          std::move(stop_));
    }

    template <typename Fail>
    auto fail(Fail fail) && {
      static_assert(IsUndefined<Fail_>::value, "Duplicate 'fail'");
      return create<Interruptible_, Value_, Raises_>(
          std::move(context_),
          std::move(start_),
          std::move(fail),
          std::move(stop_));
    }

    template <typename Stop>
    auto stop(Stop stop) && {
      static_assert(IsUndefined<Stop_>::value, "Duplicate 'stop'");
      return create<Interruptible_, Value_, Raises_>(
          std::move(context_),
          std::move(start_),
          std::move(fail_),
          std::move(stop));
    }

    auto interruptible() && {
      static_assert(!Interruptible_, "Already 'interruptible'");
      return create<true, Value_, Raises_>(
          std::move(context_),
          std::move(start_),
          std::move(fail_),
          std::move(stop_));
    }

    template <typename... Errors>
    auto raises() && {
      static_assert(std::tuple_size_v<Raises_> == 0, "Duplicate 'raises'");

      if constexpr (is_tuple_v<Errors...>) {
        static_assert(
            sizeof...(Errors) == 1,
            "'raises' with tuple doesn't support other types");
        return create<Interruptible_, Value_, Errors...>(
            std::move(context_),
            std::move(start_),
            std::move(fail_),
            std::move(stop_));
      } else {
        return create<Interruptible_, Value_, std::tuple<Errors...>>(
            std::move(context_),
            std::move(start_),
            std::move(fail_),
            std::move(stop_));
      }
    }

    Context_ context_;
    Start_ start_;
    Fail_ fail_;
    Stop_ stop_;
  };
};

////////////////////////////////////////////////////////////////////////

template <typename Value>
[[nodiscard]] auto Eventual() {
  return _Eventual::Builder<
      Undefined,
      Undefined,
      Undefined,
      Undefined,
      false,
      Value>{};
}

////////////////////////////////////////////////////////////////////////

template <typename Value, typename Start>
[[nodiscard]] auto Eventual(Start start) {
  return _Eventual::Builder<
      Undefined,
      Start,
      Undefined,
      Undefined,
      false,
      Value>{
      Undefined(),
      std::move(start)};
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
