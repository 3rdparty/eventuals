#pragma once

#include "stout/interrupt.h"
#include "stout/loop.h"
#include "stout/terminal.h"
#include "stout/undefined.h"

namespace stout {
namespace eventuals {

// Forward declaration to break circular dependency with stream.h.
template <typename K>
void next(K& k);

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
struct Transform
{
  using Value = Value_;

  K_ k_;

  Context_ context_;
  Start_ start_;
  Body_ body_;
  Ended_ ended_;
  Fail_ fail_;
  Stop_ stop_;
  Interrupt_ interrupt_;

  std::optional<Interrupt::Handler> handler_;

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
    return Transform<
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
      std::move(interrupt)
    };
  }

  template <typename K>
  auto k(K k) &&
  {
    static_assert(
        IsLoop<K>::value || IsTerminal<K>::value,
        "Expecting a 'Loop' or a 'Terminal'");

    static_assert(
        !IsLoop<K>::value || !HasLoop<K_>::value,
        "Redundant 'Loop''");

    static_assert(
        !IsTerminal<K>::value || !HasTerminal<K_>::value,
        "Redundant 'Terminal''");

    return create<Value_, Errors_...>(
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

  template <typename... Args>
  void Start(Args&&... args)
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
        eventuals::succeed(k_, std::forward<Args>(args)...);
      } else if constexpr (IsUndefined<Context_>::value) {
        start_(k_, std::forward<Args>(args)...);
      } else {
        start_(context_, k_, std::forward<Args>(args)...);
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

  template <typename... Args>
  void Body(Args&&... args)
  {
    static_assert(
        !IsUndefined<Body_>::value,
        "Undefined 'body' (and no default)");

    if constexpr (IsUndefined<Context_>::value) {
      body_(k_, std::forward<Args>(args)...);
    } else {
      body_(context_, k_, std::forward<Args>(args)...);
    }
  }

  void Ended()
  {
    static_assert(
        !IsUndefined<Ended_>::value,
        "Undefined 'ended' (and no default)");

    if constexpr (IsUndefined<Context_>::value) {
      ended_(k_);
    } else {
      ended_(context_, k_);
    }
  }
};


template <typename K_, typename E_>
struct Map
{
  using Value = typename ValueFrom<K_, typename E_::Value>::type;

  Map(K_ k, E_ e)
    : k_(std::move(k)), e_(std::move(e)) {}

  template <typename K, typename E>
  static auto create(K k, E e)
  {
    return Map<K, E>(std::move(k), std::move(e));
  }

  template <typename K>
  auto k(K k) &&
  {
    return create(
        [&]() {
          if constexpr (!IsUndefined<K_>::value) {
            return std::move(k_) | std::move(k);
          } else {
            return std::move(k);
          }
        }(),
        std::move(e_));
  }

  template <typename... Args>
  void Start(Args&&... args)
  {
    eventuals::succeed(k_, std::forward<Args>(args)...);
  }

  template <typename... Args>
  void Fail(Args&&... args)
  {
    // TODO(benh): do we need to fail via the eventual?
    eventuals::fail(k_, std::forward<Args>(args)...);
  }

  void Stop()
  {
    // TODO(benh): do we need to stop via the eventual?
    eventuals::stop(k_);
  }

  template <typename K, typename... Args>
  void Body(K& k, Args&&... args)
  {
    if (!eterminal_) {
      body_ = [&k](auto& k_, auto arg) {
        eventuals::body(k_, k, std::move(arg));
      };

      eterminal_.emplace(std::move(e_).k(terminal(&body_, &k_)));

      if (interrupt_ != nullptr) {
        eterminal_->Register(*interrupt_);
      }
    }

    eventuals::succeed(*eterminal_, std::forward<Args>(args)...);
  }

  void Ended()
  {
    eventuals::ended(k_);
  }

  void Register(Interrupt& interrupt)
  {
    interrupt_ = &interrupt;
    k_.Register(interrupt);
  }

  static auto terminal(Callback<K_&, typename E_::Value>* body_, K_* k_)
  {
    // NOTE: need to use constexpr here because compiler needs to
    // deduce function return type before K_ is fully determined.
    if constexpr (HasTerminal<K_>::value) {
      return eventuals::Terminal()
        .start([body_, k_](auto&&... values) {
          (*body_)(*k_, std::forward<decltype(values)>(values)...);
        })
        .fail([k_](auto&&... errors) {
          eventuals::fail(*k_, std::forward<decltype(errors)>(errors)...);
        })
        .stop([k_]() {
          eventuals::stop(*k_);
        });
    } else {
      return eventuals::Terminal();
    }
  }

  K_ k_;
  E_ e_;

  Callback<K_&, typename E_::Value> body_;

  using ETerminal_ = decltype(std::move(e_).k(terminal(nullptr, nullptr)));

  std::optional<ETerminal_> eterminal_;

  Interrupt* interrupt_ = nullptr;
};

} // namespace detail {


template <typename>
struct IsTransform : std::false_type {};


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
struct IsTransform<
  detail::Transform<
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
  detail::Transform<
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
struct HasLoop<
  detail::Transform<
    Value,
    K,
    Context,
    Start,
    Body,
    Ended,
    Fail,
    Stop,
    Interrupt,
    Errors...>> : HasLoop<K> {};


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
  detail::Transform<
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
auto Transform()
{
  return detail::Transform<
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


template <typename Value, typename F>
auto map(F f)
{
  return Transform<Value>()
    .context(std::move(f))
    .start([](auto&, auto& k, auto& stream) {
      succeed(k, stream);
    })
    .body([](auto& f, auto& k, auto& stream, auto&& value) {
      body(k, stream, f(std::forward<decltype(value)>(value)));
    })
    .ended([](auto&, auto& k) {
      ended(k);
    })
    .fail([](auto&, auto& k, auto&& error) {
      fail(k, std::forward<decltype(error)>(error));
    })
    .stop([](auto&, auto& k) {
      stop(k);
    });
}

////////////////////////////////////////////////////////////////////////

template <typename K, typename E>
struct IsTransform<
  detail::Map<K, E>> : std::true_type {};

////////////////////////////////////////////////////////////////////////

template <typename K, typename E>
struct IsContinuation<
  detail::Map<K, E>> : std::true_type {};

////////////////////////////////////////////////////////////////////////

template <typename K, typename E>
struct HasLoop<
  detail::Map<K, E>> : HasLoop<K> {};

////////////////////////////////////////////////////////////////////////

template <typename K, typename E>
struct HasTerminal<
  detail::Map<K, E>> : HasTerminal<K> {};

////////////////////////////////////////////////////////////////////////

template <typename K, typename E>
struct Compose<detail::Map<K, E>>
{
  template <typename Value>
  static auto compose(detail::Map<K, E> map)
  {
    auto e = eventuals::compose<Value>(std::move(map.e_));
    auto k = eventuals::compose<typename decltype(e)::Value>(std::move(map.k_));
    return detail::Map<decltype(k), decltype(e)>(std::move(k), std::move(e));
  }
};

////////////////////////////////////////////////////////////////////////

template <typename E>
auto Map(E e)
{
  return detail::Map<Undefined, E>(Undefined(), std::move(e));
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals {
} // namespace stout {
