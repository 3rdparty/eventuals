#pragma once

#include "stout/interrupt.h"
#include "stout/stream.h"
#include "stout/terminal.h"

////////////////////////////////////////////////////////////////////////

namespace stout {
namespace eventuals {

////////////////////////////////////////////////////////////////////////

namespace detail {

////////////////////////////////////////////////////////////////////////

struct _Transform {
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
  struct Continuation {
    template <typename... Args>
    void Start(Args&&... args) {
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
    void Fail(Args&&... args) {
      if constexpr (IsUndefined<Start_>::value) {
        eventuals::fail(k_, std::forward<Args>(args)...);
      } else if constexpr (IsUndefined<Context_>::value) {
        fail_(k_, std::forward<Args>(args)...);
      } else {
        fail_(context_, k_, std::forward<Args>(args)...);
      }
    }

    void Stop() {
      if constexpr (IsUndefined<Start_>::value) {
        eventuals::stop(k_);
      } else if constexpr (IsUndefined<Context_>::value) {
        stop_(k_);
      } else {
        stop_(context_, k_);
      }
    }

    void Register(Interrupt& interrupt) {
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
    void Body(Args&&... args) {
      if constexpr (IsUndefined<Body_>::value) {
        eventuals::body(k_, std::forward<Args>(args)...);
      } else if constexpr (IsUndefined<Context_>::value) {
        body_(k_, std::forward<Args>(args)...);
      } else {
        body_(context_, k_, std::forward<Args>(args)...);
      }
    }

    void Ended() {
      if constexpr (IsUndefined<Ended_>::value) {
        eventuals::ended(k_);
      } else if constexpr (IsUndefined<Context_>::value) {
        ended_(k_);
      } else {
        ended_(context_, k_);
      }
    }

    K_ k_;
    Context_ context_;
    Start_ start_;
    Body_ body_;
    Ended_ ended_;
    Fail_ fail_;
    Stop_ stop_;
    Interrupt_ interrupt_;

    std::optional<Interrupt::Handler> handler_;
  };

  template <
      typename Context_,
      typename Start_,
      typename Body_,
      typename Ended_,
      typename Fail_,
      typename Stop_,
      typename Interrupt_,
      typename Value_,
      typename... Errors_>
  struct Builder {
    template <typename Arg>
    using ValueFrom = Value_;

    template <
        typename Value,
        typename... Errors,
        typename Context,
        typename Start,
        typename Body,
        typename Ended,
        typename Fail,
        typename Stop,
        typename Interrupt>
    static auto create(
        Context context,
        Start start,
        Body body,
        Ended ended,
        Fail fail,
        Stop stop,
        Interrupt interrupt) {
      return Builder<
          Context,
          Start,
          Body,
          Ended,
          Fail,
          Stop,
          Interrupt,
          Value,
          Errors...>{
          std::move(context),
          std::move(start),
          std::move(body),
          std::move(ended),
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
          Body_,
          Ended_,
          Fail_,
          Stop_,
          Interrupt_,
          Value_,
          Errors_...>{
          std::move(k),
          std::move(context_),
          std::move(start_),
          std::move(body_),
          std::move(ended_),
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
          std::move(body_),
          std::move(ended_),
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
          std::move(body_),
          std::move(ended_),
          std::move(fail_),
          std::move(stop_),
          std::move(interrupt_));
    }

    template <typename Body>
    auto body(Body body) && {
      static_assert(IsUndefined<Body_>::value, "Duplicate 'body'");
      return create<Value_, Errors_...>(
          std::move(context_),
          std::move(start_),
          std::move(body),
          std::move(ended_),
          std::move(fail_),
          std::move(stop_),
          std::move(interrupt_));
    }

    template <typename Ended>
    auto ended(Ended ended) && {
      static_assert(IsUndefined<Ended_>::value, "Duplicate 'ended'");
      return create<Value_, Errors_...>(
          std::move(context_),
          std::move(start_),
          std::move(body_),
          std::move(ended),
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
          std::move(body_),
          std::move(ended_),
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
          std::move(body_),
          std::move(ended_),
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
          std::move(body_),
          std::move(ended_),
          std::move(fail_),
          std::move(stop_),
          std::move(interrupt));
    }

    Context_ context_;
    Start_ start_;
    Body_ body_;
    Ended_ ended_;
    Fail_ fail_;
    Stop_ stop_;
    Interrupt_ interrupt_;
  };
};

////////////////////////////////////////////////////////////////////////

} // namespace detail

////////////////////////////////////////////////////////////////////////

template <typename Value, typename... Errors>
auto Transform() {
  return detail::_Transform::Builder<
      Undefined,
      Undefined,
      Undefined,
      Undefined,
      Undefined,
      Undefined,
      Undefined,
      Undefined,
      Value,
      Errors...>{};
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals
} // namespace stout

////////////////////////////////////////////////////////////////////////
