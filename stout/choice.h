#pragma once

#include "stout/eventual.h"
#include "stout/invoke-result.h"

namespace stout {
namespace eventuals {

namespace detail {

template <typename Choice, typename E, typename K>
struct ChoiceK;


template <typename Choice, typename E>
struct ChoiceK<Choice, E, Undefined> {};


template <typename Choice_, typename E_, typename K_>
struct ChoiceK
{
  ChoiceK() {}

  // NOTE: need move constructor when creating a tuple of these.
  ChoiceK(ChoiceK&& that)
  {
    assert(!that.ek_);
  }

  Choice_* choice_ = nullptr;

  E_* e_ = nullptr;

  using E = typename InvokeResultUnknownArgs<E_>::type;
  using EK_ = decltype(std::declval<E>().template k(std::declval<K_>()));

  std::optional<EK_> ek_;

  template <typename... Args>
  void Start(Args&&... args)
  {
    assert(choice_ != nullptr);
    assert(e_ != nullptr);

    ek_.emplace(
        std::move(*e_)(std::forward<Args>(args)...)
        .k(std::move(choice_->k_)));

    if (choice_->interrupt_ != nullptr) {
      ek_->Register(*choice_->interrupt_);
    }

    eventuals::start(*ek_);
  }
};


template <typename Choice, typename K, typename... E>
auto ChoiceKs(std::tuple<E...>& tuple)
{
  return std::apply([](auto&&... es) {
    return std::make_tuple(ChoiceK<Choice, std::decay_t<decltype(es)>, K>()...);
  },
  tuple);
}


template <
  typename K_,
  typename Es_,
  typename Context_,
  typename Start_,
  typename Value_,
  typename... Errors_>
struct Choice
{
  using Value = typename ValueFrom<K_, Value_>::type;

  Choice(K_ k, Es_ es, Context_ context, Start_ start)
    : k_(std::move(k)),
      es_(std::move(es)),
      context_(std::move(context)),
      start_(std::move(start))
  {
    assert(interrupt_ == nullptr);
  }

  template <
    typename Value,
    typename... Errors,
    typename K,
    typename Es,
    typename Context,
    typename Start>
  static auto create(K k, Es es, Context context, Start start)
  {
    return Choice<K, Es, Context, Start, Value, Errors...>(
        std::move(k),
        std::move(es),
        std::move(context),
        std::move(start));
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
        std::move(es_),
        std::move(context_),
        std::move(start_));
  }

  template <typename Context>
  auto context(Context context) &&
  {
    static_assert(IsUndefined<Context_>::value, "Duplicate 'context'");
    return create<Value_, Errors_...>(
        std::move(k_),
        std::move(es_),
        std::move(context),
        std::move(start_));
  }

  template <typename Start>
  auto start(Start start) &&
  {
    static_assert(IsUndefined<Start_>::value, "Duplicate 'start'");
    return create<Value_, Errors_...>(
        std::move(k_),
        std::move(es_),
        std::move(context_),
        std::move(start));
  }

  template <size_t i = 0, typename Es, typename ChoiceKs>
  void assign(Es& es, ChoiceKs& choiceks)
  {
    static_assert(std::tuple_size_v<Es> == std::tuple_size_v<ChoiceKs>);

    std::get<i>(choiceks).choice_ = this;
    std::get<i>(choiceks).e_ = &std::get<i>(es);

    if constexpr (i + 1 != std::tuple_size_v<Es>) {
      assign<i + 1>(es, choiceks);
    }
  }

  template <typename... Args>
  void Start(Args&&... args)
  {
    static_assert(
        !IsUndefined<Start_>::value,
        "Undefined 'start' (and no default)");

    choiceks_.emplace(ChoiceKs<Choice, K_>(es_));

    assign(es_, *choiceks_);

    std::apply([&, this](auto&... choiceks) {
      if constexpr (IsUndefined<Context_>::value) {
        start_(k_, choiceks..., std::forward<Args>(args)...);
      } else {
        start_(context_, k_, choiceks..., std::forward<Args>(args)...);
      }
    },
    *choiceks_);
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

  Es_ es_;

  Context_ context_;
  Start_ start_;

  std::optional<decltype(ChoiceKs<Choice, K_>(std::declval<Es_&>()))> choiceks_;

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
  detail::Choice<
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
  detail::Choice<
    K,
    E,
    Context,
    Start,
    Value,
    Errors...>> : HasTerminal<K> {};


template <typename Value, typename... Errors, typename... E>
auto Choice(E... e)
{
  auto es = std::make_tuple(std::forward<E>(e)...);
  return detail::Choice<
    Undefined,
    decltype(es),
    Undefined,
    Undefined,
    Value,
    Errors...>(
      Undefined(),
      std::move(es),
      Undefined(),
      Undefined());
}

} // namespace eventuals {
} // namespace stout {
