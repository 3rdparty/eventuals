#pragma once

#include "stout/eventual.h"

////////////////////////////////////////////////////////////////////////

namespace stout {
namespace eventuals {
namespace grpc {
namespace detail {

////////////////////////////////////////////////////////////////////////

using stout::eventuals::detail::operator|;

////////////////////////////////////////////////////////////////////////

struct _Handler {
  template <
      typename K_,
      typename Context_,
      typename Prepare_,
      typename Ready_,
      typename Body_,
      typename Finished_,
      typename Stop_,
      typename Interrupt_,
      typename Value_,
      typename... Errors_>
  struct Continuation {
    template <typename... Args>
    void Prepare(Args&&... args) {
      auto interrupted = [this]() mutable {
        if constexpr (!IsUndefined<Interrupt_>::value) {
          return !handler_->Install();
        } else {
          (void) this; // Eschew warning that 'this' isn't used.
          return false;
        }
      }();

      if (interrupted) {
        handler_->Invoke();
      } else {
        if constexpr (IsUndefined<Prepare_>::value) {
          return;
        } else if constexpr (IsUndefined<Context_>::value) {
          prepare_(std::forward<Args>(args)...);
        } else {
          prepare_(context_, std::forward<Args>(args)...);
        }
      }
    }

    template <typename... Args>
    void Ready(Args&&... args) {
      if constexpr (IsUndefined<Ready_>::value) {
        return;
      } else if constexpr (IsUndefined<Context_>::value) {
        ready_(std::forward<Args>(args)...);
      } else {
        ready_(context_, std::forward<Args>(args)...);
      }
    }

    template <typename... Args>
    void Body(Args&&... args) {
      if constexpr (IsUndefined<Body_>::value) {
        return;
      } else if constexpr (IsUndefined<Context_>::value) {
        body_(std::forward<Args>(args)...);
      } else {
        body_(context_, std::forward<Args>(args)...);
      }
    }

    template <typename... Args>
    void Finished(Args&&... args) {
      if constexpr (IsUndefined<Finished_>::value) {
        stout::eventuals::succeed(k_, std::forward<Args>(args)...);
      } else if constexpr (IsUndefined<Context_>::value) {
        finished_(k_, std::forward<Args>(args)...);
      } else {
        finished_(context_, k_, std::forward<Args>(args)...);
      }
    }

    void Stop() {
      static_assert(
          !IsUndefined<Stop_>::value,
          "Undefined 'stop' (and no default)");

      if constexpr (IsUndefined<Context_>::value) {
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

    K_ k_;
    Context_ context_;
    Prepare_ prepare_;
    Ready_ ready_;
    Body_ body_;
    Finished_ finished_;
    Stop_ stop_;
    Interrupt_ interrupt_;

    std::optional<Interrupt::Handler> handler_;
  };

  template <
      typename Context_,
      typename Prepare_,
      typename Ready_,
      typename Body_,
      typename Finished_,
      typename Stop_,
      typename Interrupt_,
      typename Value_,
      typename... Errors_>
  struct Composable {
    template <typename Arg>
    using ValueFrom = Value_;

    template <
        typename Value,
        typename... Errors,
        typename Context,
        typename Prepare,
        typename Ready,
        typename Body,
        typename Finished,
        typename Stop,
        typename Interrupt>
    static auto create(
        Context context,
        Prepare prepare,
        Ready ready,
        Body body,
        Finished finished,
        Stop stop,
        Interrupt interrupt) {
      return Composable<
          Context,
          Prepare,
          Ready,
          Body,
          Finished,
          Stop,
          Interrupt,
          Value,
          Errors...>{
          std::move(context),
          std::move(prepare),
          std::move(ready),
          std::move(body),
          std::move(finished),
          std::move(stop),
          std::move(interrupt)};
    }

    template <typename Arg, typename K>
    auto k(K k) && {
      return Continuation<
          K,
          Context_,
          Prepare_,
          Ready_,
          Body_,
          Finished_,
          Stop_,
          Interrupt_,
          Value_,
          Errors_...>{
          std::move(k),
          std::move(context_),
          std::move(prepare_),
          std::move(ready_),
          std::move(body_),
          std::move(finished_),
          std::move(stop_),
          std::move(interrupt_)};
    }

    template <typename Context>
    auto context(Context context) && {
      static_assert(IsUndefined<Context_>::value, "Duplicate 'context'");
      return create<Value_, Errors_...>(
          std::move(context),
          std::move(prepare_),
          std::move(ready_),
          std::move(body_),
          std::move(finished_),
          std::move(stop_),
          std::move(interrupt_));
    }

    template <typename Prepare>
    auto prepare(Prepare prepare) && {
      static_assert(IsUndefined<Prepare_>::value, "Duplicate 'prepare'");
      return create<Value_, Errors_...>(
          std::move(context_),
          std::move(prepare),
          std::move(ready_),
          std::move(body_),
          std::move(finished_),
          std::move(stop_),
          std::move(interrupt_));
    }

    template <typename Ready>
    auto ready(Ready ready) && {
      static_assert(IsUndefined<Ready_>::value, "Duplicate 'ready'");
      return create<Value_, Errors_...>(
          std::move(context_),
          std::move(prepare_),
          std::move(ready),
          std::move(body_),
          std::move(finished_),
          std::move(stop_),
          std::move(interrupt_));
    }

    template <typename Body>
    auto body(Body body) && {
      static_assert(IsUndefined<Body_>::value, "Duplicate 'body'");
      return create<Value_, Errors_...>(
          std::move(context_),
          std::move(prepare_),
          std::move(ready_),
          std::move(body),
          std::move(finished_),
          std::move(stop_),
          std::move(interrupt_));
    }

    template <typename Finished>
    auto finished(Finished finished) && {
      static_assert(IsUndefined<Finished_>::value, "Duplicate 'finished'");
      return create<Value_, Errors_...>(
          std::move(context_),
          std::move(prepare_),
          std::move(ready_),
          std::move(body_),
          std::move(finished),
          std::move(stop_),
          std::move(interrupt_));
    }

    template <typename Stop>
    auto stop(Stop stop) && {
      static_assert(IsUndefined<Stop_>::value, "Duplicate 'stop'");
      return create<Value_, Errors_...>(
          std::move(context_),
          std::move(prepare_),
          std::move(ready_),
          std::move(body_),
          std::move(finished_),
          std::move(stop),
          std::move(interrupt_));
    }

    template <typename Interrupt>
    auto interrupt(Interrupt interrupt) && {
      static_assert(IsUndefined<Interrupt_>::value, "Duplicate 'interrupt'");
      return create<Value_, Errors_...>(
          std::move(context_),
          std::move(prepare_),
          std::move(ready_),
          std::move(body_),
          std::move(finished_),
          std::move(stop_),
          std::move(interrupt));
    }

    Context_ context_;
    Prepare_ prepare_;
    Ready_ ready_;
    Body_ body_;
    Finished_ finished_;
    Stop_ stop_;
    Interrupt_ interrupt_;
  };
};

////////////////////////////////////////////////////////////////////////

} // namespace detail

////////////////////////////////////////////////////////////////////////

template <typename Value, typename... Errors>
auto Handler() {
  return detail::_Handler::Composable<
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

} // namespace grpc
} // namespace eventuals
} // namespace stout

////////////////////////////////////////////////////////////////////////
