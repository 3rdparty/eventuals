#pragma once

#include "stout/adaptor.h"
#include "stout/loop.h"
#include "stout/then.h"
#include "stout/transform.h"

////////////////////////////////////////////////////////////////////////

namespace stout {
namespace eventuals {

////////////////////////////////////////////////////////////////////////

namespace detail {

////////////////////////////////////////////////////////////////////////

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

  template <
    typename K,
    std::enable_if_t<
      IsContinuation<K>::value, int> = 0>
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

  template <
    typename F,
    std::enable_if_t<
      !IsContinuation<F>::value, int> = 0>
  auto k(F f) &&
  {
    if constexpr (HasLoop<K_>::value) {
      return std::move(*this)
        | eventuals::Lambda(std::move(f));
    } else {
      return std::move(*this)
        | create(Undefined(), eventuals::Lambda(std::move(f)));
    }
  }

  template <typename... Args>
  void Start(Args&&... args)
  {
    eventuals::succeed(k_, std::forward<Args>(args)...);
  }

  template <typename... Args>
  void Fail(Args&&... args)
  {
    // TODO(benh): do we need to fail via the adaptor?
    eventuals::fail(k_, std::forward<Args>(args)...);
  }

  void Stop()
  {
    // TODO(benh): do we need to stop via the adaptor?
    eventuals::stop(k_);
  }

  template <typename K, typename... Args>
  void Body(K& k, Args&&... args)
  {
    if (!adaptor_) {
      // NOTE: in order to have a single function signature for
      // 'body_' we assume we'll have an rvalue reference '&&'. If we
      // wanted to support lvalue references '&' we'd need to modify
      // 'Adaptor'. One way to do this would be to have 'Adaptor'
      // always std::move() so that the function signature can always
      // take an rvalue reference '&&' and then also an extra boolean
      // that specifies whether or not the it should be moved again.
        adaptor_.emplace(
            std::move(e_).k(
                Adaptor<K_, typename E_::Value>(
                    k_,
                    [&k](auto& k_, auto&&... values) {
                      eventuals::body(k_, k, std::forward<decltype(values)>(values)...);
                    })));

      if (interrupt_ != nullptr) {
        adaptor_->Register(*interrupt_);
      }
    }

    eventuals::succeed(*adaptor_, std::forward<Args>(args)...);
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

  K_ k_;
  E_ e_;

  using Adaptor_ = typename EKPossiblyUndefined<
    E_,
    Adaptor<K_, typename E_::Value>>::type;

  std::optional<Adaptor_> adaptor_;

  Interrupt* interrupt_ = nullptr;
};

////////////////////////////////////////////////////////////////////////

} // namespace detail {

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
  template <typename Arg>
  static auto compose(detail::Map<K, E> map)
  {
    auto e = eventuals::compose<Arg>(std::move(map.e_));
    auto k = eventuals::compose<typename decltype(e)::Value>(std::move(map.k_));
    return detail::Map<decltype(k), decltype(e)>(std::move(k), std::move(e));
  }
};

////////////////////////////////////////////////////////////////////////

template <
  typename E,
  std::enable_if_t<
    IsContinuation<E>::value, int> = 0>
auto Map(E e)
{
  return detail::Map<Undefined, E>(Undefined(), std::move(e));
}

template <
  typename F,
  std::enable_if_t<
    !IsContinuation<F>::value, int> = 0>
auto Map(F f)
{
  return Map(eventuals::Then(std::move(f)));
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals {
} // namespace stout {

////////////////////////////////////////////////////////////////////////
