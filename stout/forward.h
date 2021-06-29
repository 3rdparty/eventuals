#pragma once

#include "stout/continuation.h"
#include "stout/lambda.h"

////////////////////////////////////////////////////////////////////////

namespace stout {
namespace eventuals {

////////////////////////////////////////////////////////////////////////

namespace detail {

////////////////////////////////////////////////////////////////////////

template <typename K_, typename Arg_>
struct Forward
{
  using Value = typename ValueFrom<K_, Arg_>::type;

  Forward(K_ k) : k_(std::move(k)) {}

  template <typename Arg, typename K>
  static auto create(K k)
  {
    return Forward<K, Arg>(std::move(k));
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
        }());
  }

  template <
    typename F,
    std::enable_if_t<
      !IsContinuation<F>::value, int> = 0>
  auto k(F f) &&
  {
    return std::move(*this) | eventuals::Lambda(std::move(f));
  }

  template <typename... Args>
  void Start(Args&&... args)
  {
    eventuals::succeed(k_, std::forward<Args>(args)...);
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
};

////////////////////////////////////////////////////////////////////////

} // namespace detail {

////////////////////////////////////////////////////////////////////////

template <typename K, typename Arg>
struct IsContinuation<
  detail::Forward<K, Arg>> : std::true_type {};

////////////////////////////////////////////////////////////////////////

template <typename K, typename Arg>
struct HasTerminal<
  detail::Forward<K, Arg>> : HasTerminal<K> {};

////////////////////////////////////////////////////////////////////////

template <typename K, typename Arg_>
struct Compose<detail::Forward<K, Arg_>>
{
  template <typename Arg>
  static auto compose(detail::Forward<K, Arg_> forward)
  {
    auto k = eventuals::compose<Arg>(std::move(forward.k_));
    return detail::Forward<decltype(k), Arg>(std::move(k));
  }
};

////////////////////////////////////////////////////////////////////////

inline auto Forward()
{
  return detail::Forward<Undefined, Undefined>(Undefined());
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals {
} // namespace stout {

////////////////////////////////////////////////////////////////////////
