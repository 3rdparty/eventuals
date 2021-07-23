#pragma once

#include "stout/adaptor.h"
#include "stout/eventual.h"
#include "stout/invoke-result.h"
#include "stout/lambda.h"
#include "stout/map.h"

////////////////////////////////////////////////////////////////////////

namespace stout {
namespace eventuals {

////////////////////////////////////////////////////////////////////////

namespace detail {

////////////////////////////////////////////////////////////////////////

template <typename K_, typename F_, typename Arg_>
struct Closure
{
  using E_ = decltype(eventuals::compose<Arg_>(std::declval<F_>()()));

  using Value = typename ValueFrom<K_, typename E_::Value>::type;

  Closure(K_ k, F_ f) : k_(std::move(k)), f_(std::move(f)) {}

  template <typename Arg, typename K, typename F>
  static auto create(K k, F f)
  {
    return Closure<K, F, Arg>(std::move(k), std::move(f));
  }

  template <
    typename K,
    std::enable_if_t<
      IsContinuation<K>::value, int> = 0>
  auto k(K k) &&
  {
    return create<Arg_>(
        [&]() {
          if constexpr (!IsUndefined<K_>::value) {
            return std::move(k_) | std::move(k);
          } else {
            return std::move(k);
          }
        }(),
        std::move(f_));
  }

  template <
    typename F,
    std::enable_if_t<
      !IsContinuation<F>::value, int> = 0>
  auto k(F f) &&
  {
    auto lambda = []() {
      return std::declval<E_>() | eventuals::Lambda(std::declval<F>());
    };

    auto map = []() {
      return std::declval<E_>() | eventuals::Map(std::declval<F>());
    };

    static_assert(
        !std::is_invocable_v<decltype(lambda)>
        || std::is_invocable_v<decltype(map)>,
        "'Closure' can't determine how to compose non-continuation, "
        "couldn't use either 'Lambda' or 'Map'");

    if constexpr (std::is_invocable_v<decltype(lambda)>) {
      return std::move(*this) | eventuals::Lambda(std::move(f));
    } else {
      return std::move(*this) | eventuals::Map(std::move(f));
    }
  }

  template <typename... Args>
  void Start(Args&&... args)
  {
    eventuals::succeed(continuation(), std::forward<Args>(args)...);
  }

  template <typename... Args>
  void Fail(Args&&... args)
  {
    eventuals::fail(continuation(), std::forward<Args>(args)...);
  }

  void Stop()
  {
    eventuals::stop(continuation());
  }

  template <typename... Args>
  void Body(Args&&... args)
  {
    eventuals::body(continuation(), std::forward<Args>(args)...);
  }

  void Ended()
  {
    eventuals::ended(continuation());
  }

  void Register(Interrupt& interrupt)
  {
    assert(interrupt_ == nullptr);
    interrupt_ = &interrupt;
  }

  auto& continuation()
  {
    if (!continuation_) {
      continuation_.emplace(eventuals::compose<Arg_>(f_()) | std::move(k_));

      if (interrupt_ != nullptr) {
        continuation_->Register(*interrupt_);
      }
    }

    return *continuation_;
  }

  K_ k_;
  F_ f_;

  Interrupt* interrupt_ = nullptr;

  using Continuation_ = typename EKPossiblyUndefined<E_, K_>::type;

  std::optional<Continuation_> continuation_;
};

////////////////////////////////////////////////////////////////////////

} // namespace detail {

////////////////////////////////////////////////////////////////////////

template <typename K, typename F, typename Arg>
struct IsContinuation<
  detail::Closure<K, F, Arg>> : std::true_type {};

////////////////////////////////////////////////////////////////////////

template <typename K, typename F, typename Arg>
struct HasTerminal<
  detail::Closure<K, F, Arg>> : HasTerminal<K> {};

////////////////////////////////////////////////////////////////////////

template <typename K, typename F, typename Arg_>
struct Compose<detail::Closure<K, F, Arg_>>
{
  template <typename Arg>
  static auto compose(detail::Closure<K, F, Arg_> closure)
  {
    if constexpr (!IsUndefined<Arg>::value) {
      // NOTE: we do a 'compose()' with the closure's callable in
      // order to propagate type information to the callable.
      auto f = eventuals::compose<Arg>(std::move(closure.f_));

      using E = decltype(f());

      using Value = typename E::Value;

      auto k = eventuals::compose<Value>(std::move(closure.k_));
      return detail::Closure<decltype(k), decltype(f), Arg>(
          std::move(k),
          std::move(f));
    } else {
      return std::move(closure);
    }
  }
};

////////////////////////////////////////////////////////////////////////

template <typename F>
auto Closure(F f)
{
  static_assert(
      std::is_invocable_v<F>,
      "'Closure' expects a *callable* (e.g., a lambda or functor) "
      "that doesn't expect any arguments");

  using E = decltype(f());

  static_assert(
      IsContinuation<E>::value,
      "Expecting an eventual continuation as the "
      "result of the callable passed to 'Closure'");

  return detail::Closure<Undefined, F, Undefined>(Undefined(), std::move(f));
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals {
} // namespace stout {

////////////////////////////////////////////////////////////////////////
