#pragma once

#include "stout/eventual.h"
#include "stout/invoke-result.h"

namespace stout {
namespace eventuals {

namespace detail {

template <typename K_, typename F_, typename Arg_>
struct Lambda
{
  using Value_ = typename InvokeResultPossiblyUndefined<F_, Arg_>::type;
  using Value = typename ValueFrom<K_, Value_>::type;

  Lambda(K_ k, F_ f) : k_(std::move(k)), f_(std::move(f)) {}

  template <typename Arg, typename K, typename F>
  static auto create(K k, F f)
  {
    return Lambda<K, F, Arg>(std::move(k), std::move(f));
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

  template <typename... Args>
  void Start(Args&&... args)
  {
    eventuals::succeed(k_, f_(std::forward<Args>(args)...));
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

  void Register(Interrupt& interrupt)
  {
    k_.Register(interrupt);
  }

  K_ k_;

  F_ f_;
};

} // namespace detail {

////////////////////////////////////////////////////////////////////////

template <typename K, typename F, typename Arg>
struct IsContinuation<
  detail::Lambda<K, F, Arg>> : std::true_type {};

////////////////////////////////////////////////////////////////////////

template <typename K, typename F, typename Arg>
struct HasTerminal<
  detail::Lambda<K, F, Arg>> : HasTerminal<K> {};

////////////////////////////////////////////////////////////////////////

template <typename K, typename F, typename Arg_>
struct Compose<detail::Lambda<K, F, Arg_>>
{
  template <typename Value>
  static auto compose(detail::Lambda<K, F, Arg_> lambda)
  {
    using Result = typename InvokeResultPossiblyUndefined<F, Value>::type;
    auto k = eventuals::compose<Result>(std::move(lambda.k_));
    return detail::Lambda<decltype(k), F, Value>(
        std::move(k),
        std::move(lambda.f_));
  }
};

////////////////////////////////////////////////////////////////////////

template <typename F>
auto Lambda(F f)
{
  return detail::Lambda<Undefined, F, Undefined>(Undefined(), std::move(f));
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals {
} // namespace stout {
