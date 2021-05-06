#pragma once

#include "stout/eventual.h"

namespace stout {
namespace eventuals {

template <typename K, typename... Args>
void yes(K& k, Args&&... args)
{
  k.Yes(std::forward<Args>(args)...);
}


template <typename K, typename... Args>
void no(K& k, Args&&... args)
{
  k.No(std::forward<Args>(args)...);
}

namespace detail {

template <typename E, typename K>
struct ConditionalK;


template <typename E>
struct ConditionalK<E, Undefined> {};


template <typename E_, typename K_>
struct ConditionalK
{
  using EK_ = decltype(std::declval<E_>().template k(std::declval<K_>()));

  std::optional<EK_> ek_;

  template <typename... Args>
  void Yes(Args&&... args)
  {
    eventuals::succeed(*ek_, std::forward<Args>(args)...);
  }

  template <typename... Args>
  void No(Args&&... args)
  {
    eventuals::succeed(ek_->k_, std::forward<Args>(args)...);
  }

  template <typename... Args>
  void Fail(Args&&... args)
  {
    eventuals::fail(ek_->k_, std::forward<Args>(args)...);
  }
};


template <
  typename K_,
  typename E_,
  typename Context_,
  typename Start_,
  typename Value_,
  typename... Errors_>
struct Conditional
{
  using Value = Value_;

  Conditional(K_ k, E_ e, Context_ context, Start_ start)
    : k_(std::move(k)),
      e_(std::move(e)),
      context_(std::move(context)),
      start_(std::move(start)) {}

  template <
    typename Value,
    typename... Errors,
    typename K,
    typename E,
    typename Context,
    typename Start>
  static auto create(K k, E e, Context context, Start start)
  {
    return Conditional<K, E, Context, Start, Value, Errors...>(
        std::move(k),
        std::move(e),
        std::move(context),
        std::move(start));
  }

  template <typename K>
  auto k(K k) &&
  {
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
        std::move(e_),
        std::move(context_),
        std::move(start_));
  }

  template <typename Context>
  auto context(Context context) &&
  {
    static_assert(IsUndefined<Context_>::value, "Duplicate 'context'");
    return create<Value_, Errors_...>(
        std::move(k_),
        std::move(e_),
        std::move(context),
        std::move(start_));
  }

  template <typename Start>
  auto start(Start start) &&
  {
    static_assert(IsUndefined<Start_>::value, "Duplicate 'start'");
    return create<Value_, Errors_...>(
        std::move(k_),
        std::move(e_),
        std::move(context_),
        std::move(start));
  }

  template <typename... Args>
  void Start(Args&&... args)
  {
    conditionalk_.ek_.emplace(std::move(e_).k(std::move(k_)));

    static_assert(
        !IsUndefined<Start_>::value,
        "Undefined 'start' (and no default)");

    if constexpr (IsUndefined<Context_>::value) {
      start_(conditionalk_, std::forward<Args>(args)...);
    } else {
      start_(context_, conditionalk_, std::forward<Args>(args)...);
    }
  }

  template <typename... Args>
  void Fail(Args&&... args)
  {
    eventuals::fail(k_, std::forward<Args>(args)...);
  }

  void Stop()
  {
    eventuals::stop(k_);
  }

  K_ k_;

  E_ e_;
  Context_ context_;
  Start_ start_;

  ConditionalK<E_, K_> conditionalk_;
};

} // namespace detail {

template <
  typename K,
  typename E,
  typename Context,
  typename Start,
  typename Value,
  typename... Errors>
struct IsContinuation<
  detail::Conditional<
    K,
    E,
    Context,
    Start,
    Value,
    Errors...>> : std::true_type {};


template <
  typename K,
  typename E,
  typename Context,
  typename Start,
  typename Value,
  typename... Errors>
struct HasTerminal<
  detail::Conditional<
    K,
    E,
    Context,
    Start,
    Value,
    Errors...>> : HasTerminal<K> {};


template <typename Value, typename... Errors, typename E>
auto Conditional(E e)
{
  return detail::Conditional<
    Undefined,
    E,
    Undefined,
    Undefined,
    Value,
    Errors...>(
      Undefined(),
      std::move(e),
      Undefined(),
      Undefined());
}

} // namespace eventuals {
} // namespace stout {
