#pragma once

#include "stout/eventual.h"
#include "stout/invoke-result.h"
#include "stout/return.h"
#include "stout/stream.h"
#include "stout/then.h"
#include "stout/transform.h"

////////////////////////////////////////////////////////////////////////

namespace stout {
namespace eventuals {

////////////////////////////////////////////////////////////////////////

namespace detail {

////////////////////////////////////////////////////////////////////////

template <typename K_, typename F_, typename Arg_>
struct Until
{
  using Value = typename ValueFrom<K_, Arg_>::type;

  Until(K_ k, F_ f)
    : k_(std::move(k)),
      f_(std::move(f)) {}

  template <typename Arg, typename K, typename F>
  static auto create(K k, F f)
  {
    return Until<K, F, Arg>(std::move(k), std::move(f));
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
    static_assert(!HasLoop<K_>::value, "Can't add *invocable* after loop");

    return std::move(*this) | eventuals::Map(eventuals::Lambda(std::move(f)));
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
    interrupt_ = &interrupt;
    k_.Register(interrupt);
  }

  template <typename K, typename... Args>
  void Body(K& k, Args&&... args)
  {
    using E = std::invoke_result_t<F_, std::add_lvalue_reference_t<Args>...>;
    if constexpr (IsContinuation<E>::value) {
      static_assert(
          sizeof...(args) == 0 || sizeof...(args) == 1,
          "Until only supports 0 or 1 value");

      static_assert(
          sizeof...(args) == 0
          || std::is_convertible_v<std::tuple<Args...>, std::tuple<Arg_>>,
          "Expecting a different type");

      if constexpr (sizeof...(args) == 1) {
        arg_.emplace(std::forward<Args>(args)...);

        // TODO(benh): differentiate between if we have a function
        // returning an eventual or if we just have an eventual
        // because in the latter we shouldn't need to
        // destruct/construct it each time but can instead just
        // save/copy the arg and pass it through via 'succeed()'.
        adaptor_.emplace(
            f_(*arg_).k(
                Adaptor<K_, bool, std::optional<Arg_>*>(
                  k_,
                  &arg_,
                  [&k](auto& k_, auto* arg_, bool done) {
                    if (done) {
                      eventuals::done(k);
                    } else {
                      eventuals::body(k_, k, std::move(**arg_));
                    }
                  })));
      } else {
        adaptor_.emplace(
            f_().k(
                Adaptor<K_, bool, std::optional<Arg_>*>(
                  k_,
                  nullptr,
                  [&k](auto& k_, auto*, bool done) {
                    if (done) {
                      eventuals::done(k);
                    } else {
                      eventuals::body(k_, k);
                    }
                  })));
      }

      if (interrupt_ != nullptr) {
        adaptor_->Register(*interrupt_);
      }

      if constexpr (sizeof...(args) == 1) {
        eventuals::succeed(*adaptor_, *arg_);
      } else {
        eventuals::succeed(*adaptor_);
      }
    } else {
      if (f_(args...)) {
        eventuals::done(k);
      } else {
        eventuals::body(k_, k, std::forward<Args>(args)...);
      }
    }
  }

  void Ended()
  {
    eventuals::ended(k_);
  }

  K_ k_;
  F_ f_;

  Interrupt* interrupt_ = nullptr;

  // NOTE: not using InvokeResultPossiblyUndefined because need to
  // qualify arg with '&' (via std::add_lvalue_reference).
  using Result_ = typename std::conditional_t<
    IsUndefined<Arg_>::value,
    std::conditional_t<
      std::is_invocable_v<F_>,
      std::invoke_result<F_>,
      type_identity<Undefined>>,
    std::invoke_result<F_, std::add_lvalue_reference_t<Arg_>>>::type;

  using E_ = std::conditional_t<
    IsContinuation<Result_>::value,
    Result_,
    Undefined>;

  using Adaptor_ = typename EKPossiblyUndefined<
    E_,
    Adaptor<K_, bool, std::optional<Arg_>*>>::type;

  std::optional<Adaptor_> adaptor_;

  std::optional<Arg_> arg_;
};

////////////////////////////////////////////////////////////////////////

} // namespace detail {

////////////////////////////////////////////////////////////////////////

template <typename K, typename F, typename Arg>
struct IsContinuation<
  detail::Until<K, F, Arg>> : std::true_type {};

////////////////////////////////////////////////////////////////////////

template <typename K, typename F, typename Arg>
struct HasTerminal<
  detail::Until<K, F, Arg>> : HasTerminal<K> {};

////////////////////////////////////////////////////////////////////////

template <typename K, typename F, typename Arg_>
struct Compose<detail::Until<K, F, Arg_>>
{
  template <typename Arg>
  static auto compose(detail::Until<K, F, Arg_> until)
  {
    auto k = eventuals::compose<Arg>(std::move(until.k_));
    return detail::Until<decltype(k), F, Arg>(
        std::move(k),
        std::move(until.f_));
  }
};

////////////////////////////////////////////////////////////////////////

template <
  typename F,
  std::enable_if_t<
    !IsContinuation<F>::value, int> = 0>
auto Until(F f)
{
  return detail::Until<Undefined, F, Undefined>(Undefined(), std::move(f));
}

template <
  typename E,
  std::enable_if_t<
    IsContinuation<E>::value, int> = 0>
auto Until(E e)
{
  return Until([e = std::move(e)](auto&&... args) mutable {
    if constexpr (sizeof...(args) > 0) {
      return eventuals::Return(std::forward<decltype(args)>(args)...)
        | std::move(e);
    } else {
      return std::move(e);
    }
  });
} 

////////////////////////////////////////////////////////////////////////

} // namespace eventuals {
} // namespace stout {

////////////////////////////////////////////////////////////////////////
