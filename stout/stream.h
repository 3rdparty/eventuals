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

template <typename K, typename T>
void emit(K& k, T&& t)
{
  static_assert(
      !std::is_same_v<K, Undefined>,
      "You're using a continuation that goes nowhere!");

  k.Emit(std::forward<T>(t));
}


template <typename K, typename S, typename T>
void body(K& k, S& s, T&& t)
{
  static_assert(
      !std::is_same_v<K, Undefined>,
      "You're using a continuation that goes nowhere!");

  k.Body(s, std::forward<T>(t));
}


template <typename K>
void ended(K& k)
{
  static_assert(
      !std::is_same_v<K, Undefined>,
      "You're using a continuation that goes nowhere!");

  k.Ended();
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

  void Start()
  {
    eventuals::succeed(*k_, *stream_);
  }

  template <typename Error>
  void Fail(Error&& error)
  {
    eventuals::fail(*k_, std::forward<Error>(error));
  }

  template <typename T>
  void Emit(T&& t)
  {
    eventuals::body(*k_, *stream_, std::forward<T>(t));
  }

  void Ended()
  {
    eventuals::ended(*k_);
  }
};


template <
  typename Value_,
  typename K_,
  typename Context_,
  typename Start_,
  typename Next_,
  typename Done_,
  typename Fail_,
  typename Stop_,
  typename... Errors_>
struct Stream
{
  using Value = Value_;

  K_ k_;

  Context_ context_;
  Start_ start_;
  Next_ next_;
  Done_ done_;
  Fail_ fail_;
  Stop_ stop_;

  StreamK<Stream, K_> streamk_;

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
    typename Stop>
  static auto create(
      K k,
      Context context,
      Start start,
      Next next,
      Done done,
      Fail fail,
      Stop stop)
  {
    return Stream<
      Value,
      K,
      Context,
      Start,
      Next,
      Done,
      Fail,
      Stop,
      Errors...> {
      std::move(k),
      std::move(context),
      std::move(start),
      std::move(next),
      std::move(done),
      std::move(fail),
      std::move(stop)
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
        std::move(stop_));

    return Eventual<
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
            stout::eventuals::start(k);
          },
          Undefined(),
          [](auto& k) {
            stout::eventuals::stop(k);
          });
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
        std::move(context_),
        std::move(start_),
        std::move(next_),
        std::move(done_),
        std::move(fail_),
        std::move(stop_));
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
        std::move(stop_));
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
        std::move(stop_));
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
        std::move(stop_));
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
        std::move(stop_));
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
        std::move(stop_));
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
        std::move(stop));
  }

  void Start()
  {
    streamk_.stream_ = this;
    streamk_.k_ = &k_;

    if constexpr (IsUndefined<Start_>::value) {
      stout::eventuals::start(streamk_);
    } else if constexpr (IsUndefined<Context_>::value) {
      start_(streamk_);
    } else {
      start_(context_, streamk_);
    }
  }

  template <typename T>
  void Succeed(T&& t)
  {
    static_assert(
        !IsUndefined<Start_>::value,
        "Undefined 'start' (and no default)");

    streamk_.stream_ = this;
    streamk_.k_ = &k_;

    if constexpr (IsUndefined<Context_>::value) {
      start_(streamk_, std::forward<T>(t));
    } else {
      start_(context_, streamk_, std::forward<T>(t));
    }
  }

  template <typename Error>
  void Fail(Error&& error)
  {
    static_assert(
        !IsUndefined<Fail_>::value,
        "Undefined 'fail' (and no default)");

    if constexpr (IsUndefined<Context_>::value) {
      fail_(k_, std::forward<Error>(error));
    } else {
      fail_(context_, k_, std::forward<Error>(error));
    }
  }

  void Stop()
  {
    static_assert(
        !IsUndefined<Stop_>::value,
        "Undefined 'stop' (and no default)");

    if constexpr (IsUndefined<Context_>::value) {
      stop_(k_);
    } else {
      stop_(context_, k_);
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
  typename Value,
  typename K,
  typename Context,
  typename Start,
  typename Next,
  typename Done,
  typename Fail,
  typename Stop,
  typename... Errors>
struct IsStream<
  detail::Stream<
    Value,
    K,
    Context,
    Start,
    Next,
    Done,
    Fail,
    Stop,
    Errors...>> : std::true_type {};


template <
  typename Value,
  typename K,
  typename Context,
  typename Start,
  typename Next,
  typename Done,
  typename Fail,
  typename Stop,
  typename... Errors>
struct IsContinuation<
  detail::Stream<
    Value,
    K,
    Context,
    Start,
    Next,
    Done,
    Fail,
    Stop,
    Errors...>> : std::true_type {};


template <
  typename Value,
  typename K,
  typename Context,
  typename Start,
  typename Next,
  typename Done,
  typename Fail,
  typename Stop,
  typename... Errors>
struct HasLoop<
  detail::Stream<
    Value,
    K,
    Context,
    Start,
    Next,
    Done,
    Fail,
    Stop,
    Errors...>> : HasLoop<K> {};


template <
  typename Value,
  typename K,
  typename Context,
  typename Start,
  typename Next,
  typename Done,
  typename Fail,
  typename Stop,
  typename... Errors>
struct HasTerminal<
  detail::Stream<
    Value,
    K,
    Context,
    Start,
    Next,
    Done,
    Fail,
    Stop,
    Errors...>> : HasTerminal<K> {};


template <typename S, typename K>
struct HasTerminal<
  detail::StreamK<S, K>> : HasTerminal<K> {};


template <typename Value, typename... Errors>
auto Stream()
{
  return detail::Stream<
    Value,
    Undefined,
    Undefined,
    Undefined,
    Undefined,
    Undefined,
    Undefined,
    Undefined,
    Errors...> {
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
