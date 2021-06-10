#pragma once

#include "stout/eventual.h"
#include "stout/invoke-result-unknown-args.h"
#include "stout/stream.h"

namespace stout {
namespace eventuals {

template <typename K, typename... Args>
void repeat(K& k, Args&&... args)
{
  k.Repeat(std::forward<Args>(args)...);
}

namespace detail {

template <typename R_, typename K_>
struct RepeatK
{
  using Value = typename R_::Value;

  R_* repeated_ = nullptr;
  K_* k_ = nullptr;

  template <typename... Args>
  void Start(Args&&... args)
  {
    eventuals::body(*k_, *repeated_, std::forward<Args>(args)...);
  }

  template <typename... Args>
  void Fail(Args&&... args)
  {
    eventuals::fail(*k_, std::forward<Args>(args)...);
  }

  void Stop()
  {
    eventuals::stop(*k_);
  }

  void Register(Interrupt&) {} // Already propagated to 'k_'.
};


template <typename R_, typename K_>
struct RepeatedK
{
  R_* repeated_ = nullptr;
  K_* k_ = nullptr;

  template <typename... Args>
  void Start(Args&&... args)
  {
    eventuals::succeed(*k_, *repeated_, std::forward<Args>(args)...);
  }

  template <typename... Args>
  void Fail(Args&&... args)
  {
    eventuals::fail(*k_, std::forward<Args>(args)...);
  }

  void Stop()
  {
    eventuals::stop(*k_);
  }

  template <typename... Args>
  void Emit(Args&&... args)
  {
    eventuals::body(*k_, *repeated_, std::forward<Args>(args)...);
  }

  void Ended()
  {
    eventuals::ended(*k_);
  }

  template <typename... Args>
  void Repeat(Args&&... args)
  {
    repeated_->Repeat(std::forward<Args>(args)...);
  }
};


template <
  typename K_,
  typename E_,
  typename Context_,
  typename Start_,
  typename Next_,
  typename Done_,
  typename Value_,
  typename... Errors_>
struct Repeated
{
  using E = typename InvokeResultUnknownArgs<E_>::type;

  using Value = typename ValueFrom<K_, typename E::Value>::type;

  Repeated(K_ k, E_ e, Context_ context, Start_ start, Next_ next, Done_ done)
    : k_(std::move(k)),
      e_(std::move(e)),
      context_(std::move(context)),
      start_(std::move(start)),
      next_(std::move(next)),
      done_(std::move(done)) {}

  template <
    typename Value,
    typename... Errors,
    typename K,
    typename E,
    typename Context,
    typename Start,
    typename Next,
    typename Done>
  static auto create(
      K k,
      E e,
      Context context,
      Start start,
      Next next,
      Done done)
  {
    return Repeated<K, E, Context, Start, Next, Done, Value, Errors...>(
        std::move(k),
        std::move(e),
        std::move(context),
        std::move(start),
        std::move(next),
        std::move(done));
  }

  template <
    typename F,
    std::enable_if_t<
      !IsContinuation<F>::value, int> = 0>
  auto k(F f) &&
  {
    // Handle non-eventuals that are *invocable*.
    return std::move(*this).k(
        create<decltype(f(std::declval<Value>()))>(
            Undefined(),
            std::move(f),
            [](auto& f, auto& k, auto&&... args) {
              eventuals::succeed(k, f(std::forward<decltype(args)>(args)...));
            },
            [](auto&, auto& k, auto&&... args) {
              eventuals::fail(k, std::forward<decltype(args)>(args)...);
            },
            [](auto&, auto& k) {
              eventuals::stop(k);
            },
            Undefined()));
  }

  template <
    typename K,
    std::enable_if_t<
      IsContinuation<K>::value, int> = 0>
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
        std::move(start_),
        std::move(next_),
        std::move(done_));
  }

  template <typename Context>
  auto context(Context context) &&
  {
    static_assert(IsUndefined<Context_>::value, "Duplicate 'context'");
    return create<Value_, Errors_...>(
        std::move(k_),
        std::move(e_),
        std::move(context),
        std::move(start_),
        std::move(next_),
        std::move(done_));
  }

  template <typename Start>
  auto start(Start start) &&
  {
    static_assert(IsUndefined<Start_>::value, "Duplicate 'start'");
    return create<Value_, Errors_...>(
        std::move(k_),
        std::move(e_),
        std::move(context_),
        std::move(start),
        std::move(next_),
        std::move(done_));
  }

  template <typename Next>
  auto next(Next next) &&
  {
    static_assert(IsUndefined<Next_>::value, "Duplicate 'next'");
    return create<Value_, Errors_...>(
        std::move(k_),
        std::move(e_),
        std::move(context_),
        std::move(start_),
        std::move(next),
        std::move(done_));
  }

  template <typename Done>
  auto done(Done done) &&
  {
    static_assert(IsUndefined<Done_>::value, "Duplicate 'done'");
    return create<Value_, Errors_...>(
        std::move(k_),
        std::move(e_),
        std::move(context_),
        std::move(start_),
        std::move(done_),
        std::move(done));
  }

  template <typename... Args>
  void Start(Args&&... args)
  {
    repeatedk_.repeated_ = this;
    repeatedk_.k_ = &k_;

    if constexpr (IsUndefined<Start_>::value) {
      eventuals::succeed(repeatedk_);
    } else if constexpr (IsUndefined<Context_>::value) {
      start_(repeatedk_, std::forward<Args>(args)...);
    } else {
      start_(context_, repeatedk_, std::forward<Args>(args)...);
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

  void Register(Interrupt& interrupt)
  {
    k_.Register(interrupt);

    interrupt_ = &interrupt;
  }

  void Next()
  {
    if constexpr (IsUndefined<Next_>::value) {
      Repeat();
    } else if constexpr (IsUndefined<Context_>::value) {
      next_(repeatedk_);
    } else {
      next_(context_, repeatedk_);
    }
  }

  void Done()
  {
    if constexpr (IsUndefined<Done_>::value) {
      ended(k_);
    } else if constexpr (IsUndefined<Context_>::value) {
      done_(repeatedk_);
    } else {
      done_(context_, repeatedk_);
    }
  }

  template <typename... Args>
  void Repeat(Args&&... args)
  {
    repeat_.emplace(
        e_(std::forward<Args>(args)...)
        .k(RepeatK<Repeated, K_> { this, &k_ }));

    repeat_->Register(*interrupt_);

    eventuals::start(*repeat_);
  }

  K_ k_;

  E_ e_;
  Context_ context_;
  Start_ start_;
  Next_ next_;
  Done_ done_;

  RepeatedK<Repeated, K_> repeatedk_;

  std::optional<
    decltype(std::declval<E>().k(
                 std::declval<RepeatK<Repeated, K_>>()))> repeat_;

  Interrupt* interrupt_ = nullptr;
};

} // namespace detail {

template <
  typename K,
  typename E,
  typename Context,
  typename Start,
  typename Next,
  typename Done,
  typename Value,
  typename... Errors>
struct IsContinuation<
  detail::Repeated<
    K,
    E,
    Context,
    Start,
    Next,
    Done,
    Value,
    Errors...>> : std::true_type {};


template <typename R, typename K>
struct IsContinuation<
  detail::RepeatK<R, K>> : std::true_type {};


template <
  typename K,
  typename E,
  typename Context,
  typename Start,
  typename Next,
  typename Done,
  typename Value,
  typename... Errors>
struct HasTerminal<
  detail::Repeated<
    K,
    E,
    Context,
    Start,
    Next,
    Done,
    Value,
    Errors...>> : HasTerminal<K> {};


template <typename R, typename K>
struct HasTerminal<
  detail::RepeatK<R, K>> : HasTerminal<K> {};


template <typename R, typename K>
struct HasTerminal<
  detail::RepeatedK<R, K>> : HasTerminal<K> {};


template <typename... Errors, typename E>
auto Repeated(E e)
{
  using Result = typename InvokeResultUnknownArgs<E>::type;

  using Value = typename Result::Value;

  return detail::Repeated<
    Undefined,
    E,
    Undefined,
    Undefined,
    Undefined,
    Undefined,
    Value,
    Errors...>(
      Undefined(),
      std::move(e),
      Undefined(),
      Undefined(),
      Undefined(),
      Undefined());
}


inline auto Repeated()
{
  return Repeated([]() {
    return Eventual<uint64_t>()
      .context(uint64_t(0))
      .start([](auto& i, auto& k) {
        succeed(k, ++i);
      });
  });
}

} // namespace eventuals {
} // namespace stout {
