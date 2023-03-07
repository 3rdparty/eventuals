#pragma once

#include <tuple>

#include "eventuals/interrupt.h"
#include "eventuals/stream.h"
#include "eventuals/type-traits.h"
#include "eventuals/undefined.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

struct _Loop final {
  // Helper struct for enforcing that values and errors are only
  // propagated of the correct type.
  template <typename K_, typename Value_, typename Raises_, typename ReschedulableErrors_>
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
          "Expecting a type derived from std::exception");

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
      typename Begin_,
      typename Body_,
      typename Ended_,
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
        Begin_ begin,
        Body_ body,
        Ended_ ended,
        Fail_ fail,
        Stop_ stop)
      : context_(std::move(context)),
        begin_(std::move(begin)),
        body_(std::move(body)),
        ended_(std::move(ended)),
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

    void Begin(TypeErasedStream& stream) {
      stream_ = &stream;

      if constexpr (IsUndefined<Begin_>::value) {
        stream_->Next();
      } else {
        if constexpr (!IsUndefined<Context_>::value && Interruptible_) {
          begin_(context_, *stream_, handler_);
        } else if constexpr (!IsUndefined<Context_>::value && !Interruptible_) {
          begin_(context_, *stream_);
        } else if constexpr (IsUndefined<Context_>::value && Interruptible_) {
          begin_(*stream_, handler_);
        } else {
          begin_(*stream_);
        }
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

    template <typename... Args>
    void Body(Args&&... args) {
      if constexpr (IsUndefined<Body_>::value) {
        stream_->Next();
      } else if constexpr (IsUndefined<Context_>::value) {
        if constexpr (Interruptible_) {
          body_(*stream_, handler_, std::forward<Args>(args)...);
        } else {
          body_(*stream_, std::forward<Args>(args)...);
        }
      } else {
        if constexpr (Interruptible_) {
          body_(context_, *stream_, handler_, std::forward<Args>(args)...);
        } else {
          body_(context_, *stream_, std::forward<Args>(args)...);
        }
      }
    }

    void Ended() {
      static_assert(
          !IsUndefined<Ended_>::value || std::is_void_v<Value_>,
          "Undefined 'ended' but 'Value' is _not_ void");

      if constexpr (IsUndefined<Ended_>::value) {
        k_().Start();
      } else if constexpr (IsUndefined<Context_>::value) {
        if constexpr (Interruptible_) {
          ended_(adaptor(), handler_);
        } else {
          ended_(adaptor());
        }
      } else {
        if constexpr (Interruptible_) {
          ended_(context_, adaptor(), handler_);
        } else {
          ended_(context_, adaptor());
        }
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
    Begin_ begin_;
    Body_ body_;
    Ended_ ended_;
    Fail_ fail_;
    Stop_ stop_;

    TypeErasedStream* stream_ = nullptr;

    Adaptor<K_, Value_, Raises_, ReschedulableErrors_> adaptor_;

    std::optional<Interrupt::Handler> handler_;

    // NOTE: we store 'k_' as the _last_ member so it will be
    // destructed _first_ and thus we won't have any use-after-delete
    // issues during destruction of 'k_' if it holds any references or
    // pointers to any (or within any) of the above members.
    Reschedulable<K_, Value_, ReschedulableErrors_> k_;
  };

  template <
      typename Context_,
      typename Begin_,
      typename Body_,
      typename Ended_,
      typename Fail_,
      typename Stop_,
      bool Interruptible_,
      typename Value_,
      typename Raises_>
  struct Builder final {
    template <typename Arg, typename Errors>
    using ValueFrom = Value_;

    template <typename Arg, typename Errors>
    using ErrorsFrom = tuple_types_union_t<Errors, Raises_>;

    template <typename Downstream>
    static constexpr bool CanCompose = Downstream::ExpectsValue;

    using Expects = StreamOfValues;

    template <
        bool Interruptible,
        typename Value,
        typename Raises,
        typename Context,
        typename Begin,
        typename Body,
        typename Ended,
        typename Fail,
        typename Stop>
    static auto create(
        Context context,
        Begin begin,
        Body body,
        Ended ended,
        Fail fail,
        Stop stop) {
      return Builder<
          Context,
          Begin,
          Body,
          Ended,
          Fail,
          Stop,
          Interruptible,
          Value,
          Raises>{
          std::move(context),
          std::move(begin),
          std::move(body),
          std::move(ended),
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
          Begin_,
          Body_,
          Ended_,
          Fail_,
          Stop_,
          Interruptible_,
          Value_,
          Raises_,
          ReschedulableErrors>(
          std::move(k),
          std::move(context_),
          std::move(begin_),
          std::move(body_),
          std::move(ended_),
          std::move(fail_),
          std::move(stop_));
    }

    template <typename Context>
    auto context(Context context) && {
      static_assert(IsUndefined<Context_>::value, "Duplicate 'context'");
      return create<Interruptible_, Value_, Raises_>(
          std::move(context),
          std::move(begin_),
          std::move(body_),
          std::move(ended_),
          std::move(fail_),
          std::move(stop_));
    }

    template <typename Begin>
    auto begin(Begin begin) && {
      static_assert(IsUndefined<Begin_>::value, "Duplicate 'begin'");
      return create<Interruptible_, Value_, Raises_>(
          std::move(context_),
          std::move(begin),
          std::move(body_),
          std::move(ended_),
          std::move(fail_),
          std::move(stop_));
    }

    template <typename Body>
    auto body(Body body) && {
      static_assert(IsUndefined<Body_>::value, "Duplicate 'body'");
      return create<Interruptible_, Value_, Raises_>(
          std::move(context_),
          std::move(begin_),
          std::move(body),
          std::move(ended_),
          std::move(fail_),
          std::move(stop_));
    }

    template <typename Ended>
    auto ended(Ended ended) && {
      static_assert(IsUndefined<Ended_>::value, "Duplicate 'ended'");
      return create<Interruptible_, Value_, Raises_>(
          std::move(context_),
          std::move(begin_),
          std::move(body_),
          std::move(ended),
          std::move(fail_),
          std::move(stop_));
    }

    template <typename Fail>
    auto fail(Fail fail) && {
      static_assert(IsUndefined<Fail_>::value, "Duplicate 'fail'");
      return create<Interruptible_, Value_, Raises_>(
          std::move(context_),
          std::move(begin_),
          std::move(body_),
          std::move(ended_),
          std::move(fail),
          std::move(stop_));
    }

    template <typename Stop>
    auto stop(Stop stop) && {
      static_assert(IsUndefined<Stop_>::value, "Duplicate 'stop'");
      return create<Interruptible_, Value_, Raises_>(
          std::move(context_),
          std::move(begin_),
          std::move(body_),
          std::move(ended_),
          std::move(fail_),
          std::move(stop));
    }

    auto interruptible() && {
      static_assert(!Interruptible_, "Already 'interruptible'");
      return create<true, Value_, Raises_>(
          std::move(context_),
          std::move(begin_),
          std::move(body_),
          std::move(ended_),
          std::move(fail_),
          std::move(stop_));
    }

    template <typename... Errors>
    auto raises() && {
      static_assert(std::tuple_size_v<Raises_> == 0, "Duplicate 'raises'");
      return create<Interruptible_, Value_, std::tuple<Errors...>>(
          std::move(context_),
          std::move(begin_),
          std::move(body_),
          std::move(ended_),
          std::move(fail_),
          std::move(stop_));
    }

    Context_ context_;
    Begin_ begin_;
    Body_ body_;
    Ended_ ended_;
    Fail_ fail_;
    Stop_ stop_;
  };
};

////////////////////////////////////////////////////////////////////////

template <typename Value>
auto Loop() {
  return _Loop::Builder<
      Undefined,
      Undefined,
      Undefined,
      Undefined,
      Undefined,
      Undefined,
      false,
      Value,
      std::tuple<>>{};
}

////////////////////////////////////////////////////////////////////////

[[nodiscard]] inline auto Loop() {
  return Loop<void>();
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
