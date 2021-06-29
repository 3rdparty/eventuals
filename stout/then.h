#pragma once

#include "stout/eventual.h"
#include "stout/invoke-result.h"
#include "stout/lambda.h"

////////////////////////////////////////////////////////////////////////

namespace stout {
namespace eventuals {

////////////////////////////////////////////////////////////////////////

namespace detail {

////////////////////////////////////////////////////////////////////////

template <typename K_, typename F_, typename Arg_>
struct Then
{
  using E_ = typename InvokeResultPossiblyUndefined<F_, Arg_>::type;
  using EK_ = typename EKPossiblyUndefined<E_, K_>::type;

  using Value =
    typename ValueFrom<EK_, typename ValuePossiblyUndefined<E_>::Value>::type;

  Then(K_ k, F_ f) : k_(std::move(k)), f_(std::move(f)) {}

  template <typename Arg, typename K, typename F>
  static auto create(K k, F f)
  {
    return Then<K, F, Arg>(std::move(k), std::move(f));
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
    return std::move(*this) | eventuals::Lambda(std::move(f));
  }

  template <typename... Args>
  void Start(Args&&... args)
  {
    ek_.emplace(
        f_(std::forward<Args>(args)...)
        .k(std::move(k_)));

    if (interrupt_ != nullptr) {
      ek_->Register(*interrupt_);
    }

    eventuals::start(*ek_);
  }

  template <typename... Args>
  void Fail(Args&&... args)
  {
    if (interrupt_ != nullptr) {
      k_.Register(*interrupt_);
    }
    eventuals::fail(k_, std::forward<Args>(args)...);
  }

  void Stop()
  {
    if (interrupt_ != nullptr) {
      k_.Register(*interrupt_);
    }
    eventuals::stop(k_);
  }

  void Register(Interrupt& interrupt)
  {
    assert(interrupt_ == nullptr);
    interrupt_ = &interrupt;
  }

  K_ k_;
  F_ f_;

  Interrupt* interrupt_ = nullptr;

  std::optional<EK_> ek_;
};

////////////////////////////////////////////////////////////////////////

} // namespace detail {

////////////////////////////////////////////////////////////////////////

template <typename K, typename F, typename Arg>
struct IsContinuation<
  detail::Then<K, F, Arg>> : std::true_type {};

////////////////////////////////////////////////////////////////////////

template <typename K, typename F, typename Arg>
struct HasTerminal<
  detail::Then<K, F, Arg>> : HasTerminal<K> {};

////////////////////////////////////////////////////////////////////////

template <typename K, typename F, typename Arg_>
struct Compose<detail::Then<K, F, Arg_>>
{
  template <typename Arg>
  static auto compose(detail::Then<K, F, Arg_> then)
  {
    if constexpr (!IsUndefined<Arg>::value) {
      using E = decltype(std::declval<F>()(std::declval<Arg>()));

      static_assert(
          IsContinuation<E>::value,
          "expecting eventual continuation as "
          "result of invocable passed to Then");

      using Value = typename E::Value;

      auto k = eventuals::compose<Value>(std::move(then.k_));
      return detail::Then<decltype(k), F, Arg>(std::move(k), std::move(then.f_));
    } else {
      return std::move(then);
    }
  }
};

////////////////////////////////////////////////////////////////////////

template <typename F>
auto Then(F f)
{
  return detail::Then<Undefined, F, Undefined>(Undefined(), std::move(f));
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals {
} // namespace stout {

////////////////////////////////////////////////////////////////////////
