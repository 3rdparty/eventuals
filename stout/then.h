#pragma once

#include "stout/eventual.h"
#include "stout/invoke-result.h"

namespace stout {
namespace eventuals {

namespace detail {

template <typename Then_, typename E_, typename K_>
struct ThenK;


template <typename Then_, typename E_>
struct ThenK<Then_, E_, Undefined> {};


template <typename Then_, typename E_, typename K_>
struct ThenK
{
  Then_* then_ = nullptr;

  using E = typename InvokeResultUnknownArgs<E_>::type;
  using EK_ = decltype(std::declval<E>().template k(std::declval<K_>()));

  std::optional<EK_> ek_;

  template <typename... Args>
  void Start(Args&&... args)
  {
    ek_.emplace(
        std::move(then_->e_)(std::forward<Args>(args)...)
        .k(std::move(then_->k_)));

    if (then_->interrupt_ != nullptr) {
      ek_->Register(*then_->interrupt_);
    }

    eventuals::start(*ek_);
  }

  template <typename... Args>
  void Fail(Args&&... args)
  {
    if (then_->interrupt_ != nullptr) {
      then_->k_.Register(*then_->interrupt_);
    }

    eventuals::fail(then_->k_, std::forward<Args>(args)...);
  }

  void Stop()
  {
    if (then_->interrupt_ != nullptr) {
      then_->k_.Register(*then_->interrupt_);
    }

    eventuals::stop(then_->k_);
  }
};


template <
  typename K_,
  typename E_,
  typename Context_,
  typename Start_,
  typename Value_,
  typename... Errors_>
struct Then
{
  using Value = typename ValueFrom<K_, Value_>::type;

  Then(K_ k, E_ e, Context_ context, Start_ start)
    : k_(std::move(k)),
      e_(std::move(e)),
      context_(std::move(context)),
      start_(std::move(start))
  {
    assert(interrupt_ == nullptr);
  }

  template <
    typename Value,
    typename... Errors,
    typename K,
    typename E,
    typename Context,
    typename Start>
  static auto create(K k, E e, Context context, Start start)
  {
    return Then<K, E, Context, Start, Value, Errors...>(
        std::move(k),
        std::move(e),
        std::move(context),
        std::move(start));
  }

  template <
    typename K,
    std::enable_if_t<
      IsContinuation<K>::value, int> = 0>
  auto k(K k) &&
  {
    return create<Value_, Errors_...>(
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

  template <
    typename F,
    std::enable_if_t<
      !IsContinuation<F>::value, int> = 0>
  auto k(F f) &&
  {
    // Handle non-eventuals that are *invocable*.
    return std::move(*this).k(
        eventuals::Eventual<decltype(f(std::declval<Value>()))>()
        .context(std::move(f))
        .start([](auto& f, auto& k, auto&&... args) {
          eventuals::succeed(k, f(std::forward<decltype(args)>(args)...));
        })
        .fail([](auto&, auto& k, auto&&... args) {
          eventuals::fail(k, std::forward<decltype(args)>(args)...);
        })
        .stop([](auto&, auto& k) {
          eventuals::stop(k);
        }));
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
    thenk_.then_ = this;

    if constexpr (IsUndefined<Start_>::value) {
      eventuals::succeed(thenk_, std::forward<decltype(args)>(args)...);
    } else if constexpr (IsUndefined<Context_>::value) {
      start_(thenk_, std::forward<Args>(args)...);
    } else {
      start_(context_, thenk_, std::forward<Args>(args)...);
    }
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

  E_ e_;
  Context_ context_;
  Start_ start_;

  ThenK<Then, E_, K_> thenk_;

  Interrupt* interrupt_ = nullptr;
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
  detail::Then<
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
  detail::Then<
    K,
    E,
    Context,
    Start,
    Value,
    Errors...>> : HasTerminal<K> {};


template <typename... Errors, typename E>
auto Then(E e)
{
  using Result = typename InvokeResultUnknownArgs<E>::type;

  using Value = typename Result::Value;

  return detail::Then<
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
