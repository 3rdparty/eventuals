#pragma once

#include "stout/interrupt.h"
#include "stout/terminal.h"
#include "stout/undefined.h"

namespace stout {
namespace eventuals {

// Forward declaration to break circular dependency with stream.h.
template <typename K>
void next(K& k);

template <typename K, typename... Args>
void body(K& k, Args&&... args)
{
  static_assert(
      !std::is_same_v<K, Undefined>,
      "You're using a continuation that goes nowhere!");

  k.Body(std::forward<Args>(args)...);
}


template <typename K>
void ended(K& k)
{
  static_assert(
      !std::is_same_v<K, Undefined>,
      "You're using a continuation that goes nowhere!");

  k.Ended();
}

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
  typename Interrupt_,
  typename... Errors_>
struct Loop
{
  using Value = typename ValueFrom<K_, Value_>::type;

  K_ k_;

  Context_ context_;
  Start_ start_;
  Body_ body_;
  Ended_ ended_;
  Fail_ fail_;
  Stop_ stop_;
  Interrupt_ interrupt_;

  std::optional<Interrupt::Handler> handler_;

  Loop(const Loop& that) = default;
  Loop(Loop&& that) = default;

  Loop& operator=(const Loop& that) = default;
  Loop& operator=(Loop&& that)
  {
    // TODO(benh): Change this to use 'swap' or investigate why the
    // compiler needs us to define this in the first place and can't
    // just resolve the move assignment operators for all the fields.
    this->~Loop();
    new(this) Loop(std::move(that));
    return *this;
  }

  template <
    typename Value,
    typename... Errors,
    typename K,
    typename Context,
    typename Start,
    typename Body,
    typename Ended,
    typename Fail,
    typename Stop,
    typename Interrupt>
  static auto create(
      K k,
      Context context,
      Start start,
      Body body,
      Ended ended,
      Fail fail,
      Stop stop,
      Interrupt interrupt)
  {
    return Loop<
      Value,
      K,
      Context,
      Start,
      Body,
      Ended,
      Fail,
      Stop,
      Interrupt,
      Errors...> {
      std::move(k),
      std::move(context),
      std::move(start),
      std::move(body),
      std::move(ended),
      std::move(fail),
      std::move(stop),
      std::move(interrupt),
    };
  }

  template <typename K>
  auto k(K k) &&
  {
    static_assert(
        !IsTerminal<K>::value || !HasTerminal<K_>::value,
        "Redundant 'Terminal'");

    using Value = std::conditional_t<
      IsTerminal<K>::value,
      Value_,
      typename K::Value>;

    return create<Value, Errors_...>(
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
        std::move(stop_),
        std::move(interrupt_));
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
        std::move(stop_),
        std::move(interrupt_));
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
        std::move(stop_),
        std::move(interrupt_));
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
        std::move(stop_),
        std::move(interrupt_));
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
        std::move(stop_),
        std::move(interrupt_));
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
        std::move(stop_),
        std::move(interrupt_));
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
        std::move(stop),
        std::move(interrupt_));
  }

  template <typename Interrupt>
  auto interrupt(Interrupt interrupt) &&
  {
    static_assert(IsUndefined<Interrupt_>::value, "Duplicate 'interrupt'");
    return create<Value_, Errors_...>(
        std::move(k_),
        std::move(context_),
        std::move(start_),
        std::move(body_),
        std::move(ended_),
        std::move(fail_),
        std::move(stop_),
        std::move(interrupt));
  }

  template <typename K, typename... Args>
  void Start(K& k, Args&&... args)
  {
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
        eventuals::next(k);
      } else if constexpr (IsUndefined<Context_>::value) {
        start_(k, std::forward<Args>(args)...);
      } else {
        start_(context_, k, std::forward<Args>(args)...);
      }
    }
  }

  template <typename... Args>
  void Fail(Args&&... args)
  {
    if constexpr (IsUndefined<Start_>::value) {
      eventuals::fail(k_, std::forward<Args>(args)...);
    } else if constexpr (IsUndefined<Context_>::value) {
      fail_(k_, std::forward<Args>(args)...);
    } else {
      fail_(context_, k_, std::forward<Args>(args)...);
    }
  }

  void Stop()
  {
    if constexpr (IsUndefined<Start_>::value) {
      eventuals::stop(k_);
    } else if constexpr (IsUndefined<Context_>::value) {
      stop_(k_);
    } else {
      stop_(context_, k_);
    }
  }

  void Register(Interrupt& interrupt)
  {
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

  template <typename K, typename... Args>
  void Body(K& k, Args&&... args)
  {
    if constexpr (IsUndefined<Body_>::value) {
      eventuals::next(k);
    } else if constexpr (IsUndefined<Context_>::value) {
      body_(k, std::forward<Args>(args)...);
    } else {
      body_(context_, k, std::forward<Args>(args)...);
    }
  }

  void Ended()
  {
    static_assert(
        !IsUndefined<Ended_>::value || IsUndefined<Value_>::value,
        "Undefined 'ended' but 'Value' is _not_ 'Undefined'");

    if constexpr (IsUndefined<Ended_>::value) {
      eventuals::succeed(k_);
    } else if constexpr (IsUndefined<Context_>::value) {
      ended_(k_);
    } else {
      ended_(context_, k_);
    }
  }
};

} // namespace detail {


template <typename>
struct IsLoop : std::false_type {};


template <
  typename Value,
  typename K,
  typename Context,
  typename Start,
  typename Body,
  typename Ended,
  typename Fail,
  typename Stop,
  typename Interrupt,
  typename... Errors>
struct IsLoop<
  detail::Loop<
    Value,
    K,
    Context,
    Start,
    Body,
    Ended,
    Fail,
    Stop,
    Interrupt,
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
  typename Interrupt,
  typename... Errors>
struct IsContinuation<
  detail::Loop<
    Value,
    K,
    Context,
    Start,
    Body,
    Ended,
    Fail,
    Stop,
    Interrupt,
    Errors...>> : std::true_type {};


template <typename>
struct HasLoop : std::false_type {};


template <
  typename Value,
  typename K,
  typename Context,
  typename Start,
  typename Body,
  typename Ended,
  typename Fail,
  typename Stop,
  typename Interrupt,
  typename... Errors>
struct HasLoop<
  detail::Loop<
    Value,
    K,
    Context,
    Start,
    Body,
    Ended,
    Fail,
    Stop,
    Interrupt,
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
  typename Interrupt,
  typename... Errors>
struct HasTerminal<
  detail::Loop<
    Value,
    K,
    Context,
    Start,
    Body,
    Ended,
    Fail,
    Stop,
    Interrupt,
    Errors...>> : HasTerminal<K> {};


template <typename Value, typename... Errors>
auto Loop()
{
  return detail::Loop<
    Value,
    Undefined,
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
    Undefined(),
    Undefined()
  };
}


inline auto Loop()
{
  return detail::Loop<
    Undefined,
    Undefined,
    Undefined,
    Undefined,
    Undefined,
    Undefined,
    Undefined,
    Undefined,
    Undefined> {
    Undefined(),
    Undefined(),
    Undefined(),
    Undefined(),
    Undefined(),
    Undefined(),
    Undefined(),
    Undefined()
  };
}


template <typename Value, typename T, typename F>
auto reduce(T t, F f)
{
  struct Context
  {
    T t;
    F f;
  };

  return Loop<Value>()
    .context(Context { std::move(t), std::move(f) })
    .start([](auto&, auto& stream) {
      next(stream);
    })
    .body([](auto& context, auto& stream, auto&& value) {
      context.t = context.f(
          std::move(context.t),
          std::forward<decltype(value)>(value));
      next(stream);
    })
    .ended([](auto& context, auto& k) {
      succeed(k, std::move(context.t));
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
