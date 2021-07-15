#pragma once

#include "stout/adaptor.h"
#include "stout/eventual.h"
#include "stout/invoke-result.h"
#include "stout/lambda.h"

////////////////////////////////////////////////////////////////////////

namespace stout {
namespace eventuals {

////////////////////////////////////////////////////////////////////////

namespace detail {

////////////////////////////////////////////////////////////////////////

template <typename K_, typename F_>
struct Closure
{
  using E_ = typename InvokeResultPossiblyUndefined<F_>::type;

  using Value = typename ValueFrom<
    K_,
    typename ValuePossiblyUndefined<E_>::Value>::type;

  Closure(K_ k, F_ f) : k_(std::move(k)), f_(std::move(f)) {}

  template <typename K, typename F>
  static auto create(K k, F f)
  {
    return Closure<K, F>(std::move(k), std::move(f));
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
    if (!adaptor_) {
      adaptor_.emplace(
          f_()
          | Adaptor<K_, typename E_::Value>(
              k_,
              [](auto& k_, auto&&... values) {
                eventuals::succeed(k_, std::forward<decltype(values)>(values)...);
              }));

      if (interrupt_ != nullptr) {
        adaptor_->Register(*interrupt_);
      }
    }

    eventuals::succeed(*adaptor_, std::forward<Args>(args)...);
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

  template <typename... Args>
  void Body(Args&&... args)
  {
    assert(adaptor_);

    eventuals::body(*adaptor_, std::forward<Args>(args)...);
  }

  void Ended()
  {
    assert(adaptor_);

    eventuals::ended(*adaptor_);
  }

  void Register(Interrupt& interrupt)
  {
    assert(interrupt_ == nullptr);
    interrupt_ = &interrupt;
    k_.Register(interrupt);
  }

  K_ k_;
  F_ f_;

  Interrupt* interrupt_ = nullptr;

  using Adaptor_ = typename EKPossiblyUndefined<
    E_,
    Adaptor<K_, typename ValuePossiblyUndefined<E_>::Value>>::type;

  std::optional<Adaptor_> adaptor_;
};

////////////////////////////////////////////////////////////////////////

} // namespace detail {

////////////////////////////////////////////////////////////////////////

template <typename K, typename F>
struct IsContinuation<
  detail::Closure<K, F>> : std::true_type {};

////////////////////////////////////////////////////////////////////////

template <typename K, typename F>
struct HasTerminal<
  detail::Closure<K, F>> : HasTerminal<K> {};

////////////////////////////////////////////////////////////////////////

template <typename K, typename F>
struct Compose<detail::Closure<K, F>>
{
  template <typename Arg>
  static auto compose(detail::Closure<K, F> closure)
  {
    if constexpr (!IsUndefined<Arg>::value) {
      auto f = eventuals::compose<Arg>(std::move(closure.f_));

      using E = decltype(f());

      static_assert(
          IsContinuation<E>::value,
          "expecting eventual continuation as "
          "result of callable passed to 'Closure'");

      using Value = typename E::Value;

      auto k = eventuals::compose<Value>(std::move(closure.k_));
      return detail::Closure<decltype(k), decltype(f)>(
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
  return detail::Closure<Undefined, F>(Undefined(), std::move(f));
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals {
} // namespace stout {

////////////////////////////////////////////////////////////////////////
