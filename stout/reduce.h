#pragma once

#include "stout/adaptor.h"
#include "stout/eventual.h"
#include "stout/stream.h"

////////////////////////////////////////////////////////////////////////

namespace stout {
namespace eventuals {

////////////////////////////////////////////////////////////////////////

namespace detail {

////////////////////////////////////////////////////////////////////////

template <typename K_, typename T_, typename F_, typename Arg_>
struct Reduce
{
  using Value = typename ValueFrom<K_, T_>::type;

  Reduce(K_ k, T_ t, F_ f)
    : k_(std::move(k)),
      t_(std::move(t)),
      f_(std::move(f)) {}

  template <typename Arg, typename K, typename T, typename F>
  static auto create(K k, T t, F f)
  {
    return Reduce<K, T, F, Arg>(std::move(k), std::move(t), std::move(f));
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
        std::move(t_),
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

  template <typename K, typename... Args>
  void Start(K& k, Args&&... args)
  {
    eventuals::next(k);
  }

  template <typename... Args>
  void Fail(Args&&... args)
  {
    // TODO(benh): do we need to stop via the adaptor?
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
    auto e = [&](auto e) {
      if constexpr (sizeof...(args) > 0) {
        return eventuals::compose<Args...>(std::move(e));
      } else {
        return std::move(e);
      }
    }([this]() {
      auto result = f_(t_);
      if constexpr (!IsContinuation<decltype(result)>::value) {
        return eventuals::Lambda(std::move(result));
      } else {
        return std::move(result);
      }
    }());

    using E = decltype(e);

    using Value = typename E::Value;

    static_assert(
        IsUndefined<Value>::value
        || std::is_same_v<bool, Value>,
        "Expecting 'Reduce' to eventually return a bool");

    adaptor_.emplace(
        std::move(e)
        | Adaptor<K_, bool>(
            k_,
            [&k](auto& k_, bool next) {
              if (next) {
                eventuals::next(k);
              } else {
                eventuals::done(k);
              }
            }));

    if (interrupt_ != nullptr) {
      adaptor_->Register(*interrupt_);
    }

    eventuals::succeed(*adaptor_, std::forward<Args>(args)...);
  }

  void Ended()
  {
    eventuals::succeed(k_, std::move(t_));
  }

  void Register(Interrupt& interrupt)
  {
    interrupt_ = &interrupt;
    k_.Register(interrupt);
  }

  K_ k_;
  T_ t_;
  F_ f_;

  Interrupt* interrupt_ = nullptr;

  using Result_ = std::invoke_result_t<F_, std::add_lvalue_reference_t<T_>>;

  using E_ = std::conditional_t<
    IsContinuation<Result_>::value,
    Result_,
    Lambda<Undefined, Result_, Arg_>>;

  using Adaptor_ = typename EKPossiblyUndefined<
    decltype(eventuals::compose<Arg_>(std::declval<E_>())),
    Adaptor<K_, bool>>::type;

  std::optional<Adaptor_> adaptor_;
};

////////////////////////////////////////////////////////////////////////

} // namespace detail {

////////////////////////////////////////////////////////////////////////

template <typename K, typename T, typename F, typename Arg>
struct IsContinuation<
  detail::Reduce<K, T, F, Arg>> : std::true_type {};

////////////////////////////////////////////////////////////////////////


template <typename K, typename T, typename F, typename Arg>
struct HasTerminal<
  detail::Reduce<K, T, F, Arg>> : HasTerminal<K> {};

////////////////////////////////////////////////////////////////////////

template <typename K, typename T, typename F, typename Arg>
struct IsLoop<
  detail::Reduce<K, T, F, Arg>> : std::true_type {};

////////////////////////////////////////////////////////////////////////

template <typename K, typename T, typename F, typename Arg>
struct HasLoop<
  detail::Reduce<K, T, F, Arg>> : std::true_type {};

////////////////////////////////////////////////////////////////////////

template <typename K, typename T, typename F, typename Arg_>
struct Compose<detail::Reduce<K, T, F, Arg_>>
{
  template <typename Arg>
  static auto compose(detail::Reduce<K, T, F, Arg_> reduce)
  {
    using Result = std::invoke_result_t<F, std::add_lvalue_reference_t<T>>;

    static_assert(
        IsContinuation<Result>::value
        || std::is_invocable_v<Result, Arg>,
        "'Reduce' expects either a callable or an eventual continuation "
        "as the result of invoking the initial callable");

    return detail::Reduce<K, T, F, Arg>(
        std::move(reduce.k_),
        std::move(reduce.t_),
        std::move(reduce.f_));
  }
};

////////////////////////////////////////////////////////////////////////

template <typename T, typename F>
auto Reduce(T t, F f)
{
  static_assert(
      !IsContinuation<F>::value
      && std::is_invocable_v<F, std::add_lvalue_reference_t<T>>,
      "'Reduce' expects a callable in order to provide "
      "a reference to the initial accumulator and you can "
      "return an eventual from the callable");

  return detail::Reduce<Undefined, T, F, Undefined>(
      Undefined(),
      std::move(t),
      std::move(f));
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals {
} // namespace stout {

////////////////////////////////////////////////////////////////////////
