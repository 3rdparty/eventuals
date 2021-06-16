#pragma once

// TODO(benh): infinite recursion via thread-local storage.
//
// TODO(benh): 'stop' on stream should break infinite recursion
// (figure out how to embed a std::atomic).
//
// TODO(benh): disallow calling 'next()' after calling 'done()'.
//
// TODO(benh): disallow calling 'emit()' before call to 'next()'.

#include "stout/eventual.h"
#include "stout/loop.h"
#include "stout/transform.h"

namespace stout {
namespace eventuals {

template <typename K, typename... Args>
void emit(K& k, Args&&... args)
{
  static_assert(
      !std::is_same_v<K, Undefined>,
      "You're using a continuation that goes nowhere!");

  k.Emit(std::forward<Args>(args)...);
}


template <typename K>
void next(K& k)
{
  static_assert(
      !std::is_same_v<K, Undefined>,
      "You're using a continuation that goes nowhere!");

  k.Next();
}


template <typename K>
void done(K& k)
{
  static_assert(
      !std::is_same_v<K, Undefined>,
      "You're using a continuation that goes nowhere!");

  k.Done();
}


namespace detail {

// Helper that distinguishes when a stream's continuation needs to be
// invoked (versus the stream being invoked as a continuation itself).
template <typename S_, typename K_>
struct StreamK
{
  S_* stream_ = nullptr;
  K_* k_ = nullptr;

  template <typename... Args>
  void Start(Args&&... args)
  {
    eventuals::succeed(*k_, *stream_, std::forward<Args>(args)...);
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
    eventuals::body(*k_, *stream_, std::forward<Args>(args)...);
  }

  void Ended()
  {
    eventuals::ended(*k_);
  }
};


template <
  typename K_,
  typename Context_,
  typename Start_,
  typename Next_,
  typename Done_,
  typename Fail_,
  typename Stop_,
  typename Interrupt_,
  typename Value_,
  typename... Errors_>
struct Stream
{
  using Value = typename ValueFrom<K_, Value_>::type;

  K_ k_;

  Context_ context_;
  Start_ start_;
  Next_ next_;
  Done_ done_;
  Fail_ fail_;
  Stop_ stop_;
  Interrupt_ interrupt_;

  StreamK<Stream, K_> streamk_;

  std::optional<Interrupt::Handler> handler_;

  Stream(const Stream& that) = default;
  Stream(Stream&& that) = default;

  Stream& operator=(const Stream& that) = default;
  Stream& operator=(Stream&& that)
  {
    // TODO(benh): Change this to use 'swap' or investigate why the
    // compiler needs us to define this in the first place and can't
    // just resolve the move assignment operators for all the fields.
    this->~Stream();
    new(this) Stream(std::move(that));
    return *this;
  }

  template <
    typename Value,
    typename... Errors,
    typename K,
    typename Context,
    typename Start,
    typename Next,
    typename Done,
    typename Fail,
    typename Stop,
    typename Interrupt>
  static auto create(
      K k,
      Context context,
      Start start,
      Next next,
      Done done,
      Fail fail,
      Stop stop,
      Interrupt interrupt)
  {
    return Stream<
      K,
      Context,
      Start,
      Next,
      Done,
      Fail,
      Stop,
      Interrupt,
      Value,
      Errors...> {
      std::move(k),
      std::move(context),
      std::move(start),
      std::move(next),
      std::move(done),
      std::move(fail),
      std::move(stop),
      std::move(interrupt)
    };
  }

  template <
    typename L,
    std::enable_if_t<
      IsLoop<L>::value, int> = 0>
  auto k(L l) &&
  {
    static_assert(
        !HasLoop<K_>::value,
        "Redundant 'Loop'");

    auto k = create<Value_, Errors_...>(
        [&]() {
          if constexpr (!IsUndefined<K_>::value) {
            return std::move(k_) | std::move(l);
          } else {
            return std::move(l);
          }
        }(),
        std::move(context_),
        std::move(start_),
        std::move(next_),
        std::move(done_),
        std::move(fail_),
        std::move(stop_),
        std::move(interrupt_));

    return Eventual<
      Undefined,
      Undefined,
      Undefined,
      Undefined,
      Undefined,
      Undefined,
      Undefined>::create<typename L::Value>(
          std::move(k),
          Undefined(),
          // TODO(benh): if 's.fail_' is not Undefined then assume
          // that this is an eventual *continuation* and create a
          // 'start' that takes a value and does a succeed and create
          // a 'fail' that propagates the error (instead of the
          // current 'Undefined()').
          [](auto& k) {
            eventuals::start(k);
          },
          Undefined(),
          [](auto& k) {
            eventuals::stop(k);
          },
          Undefined());
  }

  template <
    typename K,
    std::enable_if_t<
      IsTransform<K>::value
      || IsTerminal<K>::value, int> = 0>
  auto k(K k) &&
  {
    static_assert(
        !IsTransform<K>::value || !HasLoop<K_>::value,
        "Can't add 'Transform' *after* 'Loop'");

    static_assert(
        !IsTerminal<K>::value || HasLoop<K_>::value,
        "Can't add 'Terminal' *before* 'Loop'");

    return create<Value_, Errors_...>(
        [&]() {
          if constexpr (!IsUndefined<K_>::value) {
            return std::move(k_) | std::move(k);
          } else {
            return std::move(k);
          }
        }(),
        std::move(context_),
        std::move(start_),
        std::move(next_),
        std::move(done_),
        std::move(fail_),
        std::move(stop_),
        std::move(interrupt_));
  }

  template <
    typename F,
    std::enable_if_t<
      !IsContinuation<F>::value, int> = 0>
  auto k(F f) &&
  {
    static_assert(!HasLoop<K_>::value, "Can't add *invocable* after loop");

    using Value = decltype(f(std::declval<Value_>()));

    return std::move(*this).k(map<Value>(std::move(f)));
  }

  template <typename Context>
  auto context(Context context) &&
  {
    static_assert(IsUndefined<Context_>::value, "Duplicate 'context'");
    return create<Value_, Errors_...>(
        std::move(k_),
        std::move(context),
        std::move(start_),
        std::move(next_),
        std::move(done_),
        std::move(fail_),
        std::move(stop_),
        std::move(interrupt_));
  }

  template <typename Start>
  auto start(Start start) &&
  {
    static_assert(IsUndefined<Start_>::value, "Duplicate 'start'");
    return create<Value_, Errors_...>(
        std::move(k_),
        std::move(context_),
        std::move(start),
        std::move(next_),
        std::move(done_),
        std::move(fail_),
        std::move(stop_),
        std::move(interrupt_));
  }

  template <typename Next>
  auto next(Next next) &&
  {
    static_assert(IsUndefined<Next_>::value, "Duplicate 'next'");
    return create<Value_, Errors_...>(
        std::move(k_),
        std::move(context_),
        std::move(start_),
        std::move(next),
        std::move(done_),
        std::move(fail_),
        std::move(stop_),
        std::move(interrupt_));
  }

  template <typename Done>
  auto done(Done done) &&
  {
    static_assert(IsUndefined<Done_>::value, "Duplicate 'done'");
    return create<Value_, Errors_...>(
        std::move(k_),
        std::move(context_),
        std::move(start_),
        std::move(next_),
        std::move(done),
        std::move(fail_),
        std::move(stop_),
        std::move(interrupt_));
  }

  template <typename Fail>
  auto fail(Fail fail) &&
  {
    static_assert(IsUndefined<Fail_>::value, "Duplicate 'fail'");
    return create<Value_, Errors_...>(
        std::move(k_),
        std::move(context_),
        std::move(start_),
        std::move(next_),
        std::move(done_),
        std::move(fail),
        std::move(stop_),
        std::move(interrupt_));
  }

  template <typename Stop>
  auto stop(Stop stop) &&
  {
    static_assert(IsUndefined<Stop_>::value, "Duplicate 'stop'");
    return create<Value_, Errors_...>(
        std::move(k_),
        std::move(context_),
        std::move(start_),
        std::move(next_),
        std::move(done_),
        std::move(fail_),
        std::move(stop),
        std::move(interrupt_));
  }

  template <typename Interrupt>
  auto interrupt(Interrupt interrupt) &&
  {
    static_assert(IsUndefined<Interrupt_>::value, "Duplicate 'interrupt'");
    return create<Value_, Errors_...>(
        std::move(k_),
        std::move(context_),
        std::move(start_),
        std::move(next_),
        std::move(done_),
        std::move(fail_),
        std::move(stop_),
        std::move(interrupt));
  }

  template <typename... Args>
  void Start(Args&&... args)
  {
    streamk_.stream_ = this;
    streamk_.k_ = &k_;

    auto interrupted = [this]() mutable {
      if (handler_) {
        return !handler_->Install();
      } else {
        return false;
      }
    }();

    if (interrupted) {
      assert(handler_);
      handler_->Invoke();
    } else {
      if constexpr (IsUndefined<Start_>::value) {
        eventuals::start(streamk_, std::forward<Args>(args)...);
      } else if constexpr (IsUndefined<Context_>::value) {
        start_(streamk_, std::forward<Args>(args)...);
      } else {
        start_(context_, streamk_, std::forward<Args>(args)...);
      }
    }
  }

  template <typename... Args>
  void Fail(Args&&... args)
  {
    if constexpr (IsUndefined<Fail_>::value) {
      eventuals::fail(k_, std::forward<Args>(args)...);
    } else if constexpr (IsUndefined<Context_>::value) {
      fail_(k_, std::forward<Args>(args)...);
    } else {
      fail_(context_, k_, std::forward<Args>(args)...);
    }
  }

  void Stop()
  {
    if constexpr (!IsUndefined<Stop_>::value) {
      eventuals::stop(k_);
    } else if constexpr (IsUndefined<Context_>::value) {
      stop_(k_);
    } else {
      stop_(context_, k_);
    }
  }

  void Register(Interrupt& interrupt)
  {
    k_.Register(interrupt);

    if constexpr (!IsUndefined<Interrupt_>::value) {
      handler_.emplace(&interrupt, [this]() {
        if constexpr (IsUndefined<Context_>::value) {
          interrupt_(k_);
        } else {
          interrupt_(context_, k_);
        }
      });
    }
  }

  void Next()
  {
    static_assert(
        !IsUndefined<Next_>::value,
        "Undefined 'next' (and no default)");

    if constexpr (IsUndefined<Context_>::value) {
      next_(streamk_);
    } else {
      next_(context_, streamk_);
    }
  }

  void Done()
  {
    static_assert(
        !IsUndefined<Done_>::value,
        "Undefined 'done' (and no default)");

    if constexpr (IsUndefined<Context_>::value) {
      done_(streamk_);
    } else {
      done_(context_, streamk_);
    }
  }
};

} // namespace detail {


template <typename>
struct IsStream : std::false_type {};


template <
  typename K,
  typename Context,
  typename Start,
  typename Next,
  typename Done,
  typename Fail,
  typename Stop,
  typename Interrupt,
  typename Value,
  typename... Errors>
struct IsStream<
  detail::Stream<
    K,
    Context,
    Start,
    Next,
    Done,
    Fail,
    Stop,
    Interrupt,
    Value,
    Errors...>> : std::true_type {};


template <
  typename K,
  typename Context,
  typename Start,
  typename Next,
  typename Done,
  typename Fail,
  typename Stop,
  typename Interrupt,
  typename Value,
  typename... Errors>
struct IsContinuation<
  detail::Stream<
    K,
    Context,
    Start,
    Next,
    Done,
    Fail,
    Stop,
    Interrupt,
    Value,
    Errors...>> : std::true_type {};


template <
  typename K,
  typename Context,
  typename Start,
  typename Next,
  typename Done,
  typename Fail,
  typename Stop,
  typename Interrupt,
  typename Value,
  typename... Errors>
struct HasLoop<
  detail::Stream<
    K,
    Context,
    Start,
    Next,
    Done,
    Fail,
    Stop,
    Interrupt,
    Value,
    Errors...>> : HasLoop<K> {};


template <
  typename K,
  typename Context,
  typename Start,
  typename Next,
  typename Done,
  typename Fail,
  typename Stop,
  typename Interrupt,
  typename Value,
  typename... Errors>
struct HasTerminal<
  detail::Stream<
    K,
    Context,
    Start,
    Next,
    Done,
    Fail,
    Stop,
    Interrupt,
    Value,
    Errors...>> : HasTerminal<K> {};


template <typename S, typename K>
struct HasTerminal<
  detail::StreamK<S, K>> : HasTerminal<K> {};


template <typename Value, typename... Errors>
auto Stream()
{
  return detail::Stream<
    Undefined,
    Undefined,
    Undefined,
    Undefined,
    Undefined,
    Undefined,
    Undefined,
    Undefined,
    Value,
    Errors...> {
    Undefined(),
    Undefined(),
    Undefined(),
    Undefined(),
    Undefined(),
    Undefined(),
    Undefined(),
    Undefined()
  };
}

} // namespace eventuals {
} // namespace stout {
