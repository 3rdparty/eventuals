#pragma once

#include "eventuals/eventual.h"
#include "eventuals/interrupt.h"
#include "eventuals/stream.h"
#include "eventuals/undefined.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

struct _Loop {
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

    void Begin(TypeErasedStream& stream) {
      stream_ = &stream;

      if constexpr (IsUndefined<Begin_>::value) {
        stream_->Next();
      } else {
        if constexpr (!IsUndefined<Context_>::value && Interruptible_) {
          CHECK(handler_);
          begin_(context_, *stream_, *handler_);
        } else if constexpr (!IsUndefined<Context_>::value && !Interruptible_) {
          begin_(context_, *stream_);
        } else if constexpr (IsUndefined<Context_>::value && Interruptible_) {
          CHECK(handler_);
          begin_(*stream_, *handler_);
        } else {
          begin_(*stream_);
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

      if constexpr (Interruptible_) {
        handler_.emplace(&interrupt);
      }
    }

    template <typename... Args>
    void Body(Args&&... args) {
      if constexpr (IsUndefined<Body_>::value) {
        stream_->Next();
      } else if constexpr (IsUndefined<Context_>::value) {
        body_(*stream_, std::forward<Args>(args)...);
      } else {
        body_(context_, *stream_, std::forward<Args>(args)...);
      }
    }

    void Ended() {
      static_assert(
          !IsUndefined<Ended_>::value || std::is_void_v<Value_>,
          "Undefined 'ended' but 'Value' is _not_ void");

      if constexpr (IsUndefined<Ended_>::value) {
        k_().Start();
      } else if constexpr (IsUndefined<Context_>::value) {
        ended_(k_());
      } else {
        ended_(context_, k_());
      }
    }

    Reschedulable<K_, Value_> k_;
    Context_ context_;
    Begin_ begin_;
    Body_ body_;
    Ended_ ended_;
    Fail_ fail_;
    Stop_ stop_;

    TypeErasedStream* stream_ = nullptr;

    std::optional<Interrupt::Handler> handler_;
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
      typename... Errors_>
  struct Builder {
    template <typename Arg>
    using ValueFrom = Value_;

    template <
        bool Interruptible,
        typename Value,
        typename... Errors,
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
          Errors...>{
          std::move(context),
          std::move(begin),
          std::move(body),
          std::move(ended),
          std::move(fail),
          std::move(stop)};
    }

    template <typename Arg, typename K>
    auto k(K k) && {
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
          Errors_...>{
          Reschedulable<K, Value_>{std::move(k)},
          std::move(context_),
          std::move(begin_),
          std::move(body_),
          std::move(ended_),
          std::move(fail_),
          std::move(stop_)};
    }

    template <typename Context>
    auto context(Context context) && {
      static_assert(IsUndefined<Context_>::value, "Duplicate 'context'");
      return create<Interruptible_, Value_, Errors_...>(
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
      return create<Interruptible_, Value_, Errors_...>(
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
      return create<Interruptible_, Value_, Errors_...>(
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
      return create<Interruptible_, Value_, Errors_...>(
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
      return create<Interruptible_, Value_, Errors_...>(
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
      return create<Interruptible_, Value_, Errors_...>(
          std::move(context_),
          std::move(begin_),
          std::move(body_),
          std::move(ended_),
          std::move(fail_),
          std::move(stop));
    }

    auto interruptible() && {
      static_assert(!Interruptible_, "Already 'interruptible'");
      return create<true, Value_, Errors_...>(
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

template <typename Value, typename... Errors>
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
      Errors...>{};
}

////////////////////////////////////////////////////////////////////////

inline auto Loop() {
  return Loop<void>();
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
