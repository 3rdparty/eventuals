#pragma once

// TODO(benh): infinite recursion via thread-local storage.
//
// TODO(benh): 'stop' on stream should break infinite recursion
// (figure out how to embed a std::atomic).
//
// TODO(benh): disallow calling 'next()' after calling 'done()'.
//
// TODO(benh): disallow calling 'emit()' before call to 'next()'.

#include "stout/eventuals.h"

namespace stout {
namespace eventuals {

template <typename K, typename T>
void emit(K& k, T&& t)
{
  static_assert(
      !std::is_same_v<K, Undefined>,
      "You're using a continuation that goes nowhere!");

  k.emit(std::forward<T>(t));
}


template <typename K, typename S, typename T>
void body(K& k, S& s, T&& t)
{
  static_assert(
      !std::is_same_v<K, Undefined>,
      "You're using a continuation that goes nowhere!");

  k.body(s, std::forward<T>(t));
}


template <typename K>
void ended(K& k)
{
  static_assert(
      !std::is_same_v<K, Undefined>,
      "You're using a continuation that goes nowhere!");

  k.ended();
}


template <typename K>
void next(K& k)
{
  static_assert(
      !std::is_same_v<K, Undefined>,
      "You're using a continuation that goes nowhere!");

  k.next();
}


template <typename K>
void done(K& k)
{
  static_assert(
      !std::is_same_v<K, Undefined>,
      "You're using a continuation that goes nowhere!");

  k.done();
}


// Helper that distinguishes when a stream's continuation needs to be
// invoked (versus the stream being invoked as a continuation itself).
template <typename S, typename K>
struct StreamK
{
  S* stream_ = nullptr;
  K* k_ = nullptr;

  void start()
  {
    eventuals::succeed(*k_, *stream_);
  }

  template <typename Error>
  void fail(Error&& error)
  {
    eventuals::fail(*k_, std::forward<Error>(error));
  }

  template <typename T>
  void emit(T&& t)
  {
    eventuals::body(*k_, *stream_, std::forward<T>(t));
  }

  void ended()
  {
    eventuals::ended(*k_);
  }
};


template <typename>
struct IsStreamK : std::false_type {};


template <typename S, typename K>
struct IsStreamK<StreamK<S, K>> : std::true_type {};


template <typename S, typename K>
struct HasTerminal<StreamK<S, K>> : HasTerminal<K> {};


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
struct Stream
{
  using Type = Value;

  K k_;

  Context context_;
  Start start_;
  Next next_;
  Done done_;
  Fail fail_;
  Stop stop_;

  StreamK<Stream, K> streamk_;

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

  void start()
  {
    streamk_.stream_ = this;
    streamk_.k_ = &k_;
    start_(context_, streamk_);
  }

  template <typename T>
  void succeed(T&& t)
  {
    streamk_.stream_ = this;
    streamk_.k_ = &k_;
    start_(context_, streamk_, std::forward<T>(t));
  }

  template <typename Error>
  void fail(Error&& error)
  {
    fail_(context_, k_, std::forward<Error>(error));
  }

  void stop()
  {
    stop_(context_, k_);
  }

  void next()
  {
    next_(context_, streamk_);
  }

  void done()
  {
    done_(context_, streamk_);
  }
};


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
  Stream<
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
struct HasEventualContinuation<
  Stream<
    Value,
    K,
    Context,
    Start,
    Next,
    Done,
    Fail,
    Stop,
    Errors...>> : std::conditional_t<
  std::is_same<K, Undefined>::value,
  std::false_type,
  std::true_type> {};


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
  Stream<
    Value,
    K,
    Context,
    Start,
    Next,
    Done,
    Fail,
    Stop,
    Errors...>> : HasTerminal<K> {};


template <
  typename Value,
  typename... Errors,
  typename Context,
  typename Start,
  typename Next,
  typename Done,
  typename Fail,
  typename Stop>
auto stream(
    Context context,
    Start start,
    Next next,
    Done done,
    Fail fail,
    Stop stop)
{
  return Stream<
    Value,
    Undefined,
    Context,
    Start,
    Next,
    Done,
    Fail,
    Stop,
    Errors...> {
    Undefined(),
    std::move(context),
    std::move(start),
    std::move(next),
    std::move(done),
    std::move(fail),
    std::move(stop)
  };
}


template <
  typename Value,
  typename... Errors,
  typename Context,
  typename Start,
  typename Next,
  typename Done,
  typename Stop>
auto stream(
    Context context,
    Start start,
    Next next,
    Done done,
    Stop stop)
{
  return stream<Value, Errors...>(
      std::move(context),
      std::move(start),
      std::move(next),
      std::move(done),
      Undefined(),
      std::move(stop));
}


template <
  typename Value,
  typename... Errors,
  typename Context,
  typename Next,
  typename Done,
  typename Stop>
auto stream(
    Context context,
    Next next,
    Done done,
    Stop stop)
{
  return stream<Value, Errors...>(
      std::move(context),
      [](auto&, auto& k) {
        start(k);
      },
      std::move(next),
      std::move(done),
      Undefined(),
      std::move(stop));
}



template <
  typename Value,
  typename K,
  typename Context,
  typename Start,
  typename Body,
  typename Ended,
  typename Fail,
  typename Stop,
  typename... Errors>
struct Transform
{
  using Type = Value;

  K k_;

  Context context_;
  Start start_;
  Body body_;
  Ended ended_;
  Fail fail_;
  Stop stop_;

  template <typename S>
  void succeed(S& s)
  {
    start_(context_, k_, s);
  }

  template <typename S, typename T>
  void body(S& s, T&& t)
  {
    body_(context_, k_, s, std::forward<T>(t));
  }

  void ended()
  {
    ended_(context_, k_);
  }

  template <typename Error>
  void fail(Error&& error)
  {
    fail_(context_, k_, std::forward<Error>(error));
  }

  void stop()
  {
    stop_(context_, k_);
  }
};


template <typename>
struct IsTransform : std::false_type {};

template <
  typename Value,
  typename K,
  typename Context,
  typename Start,
  typename Body,
  typename Ended,
  typename Fail,
  typename Stop,
  typename... Errors>
struct IsTransform<
  Transform<
    Value,
    K,
    Context,
    Start,
    Body,
    Ended,
    Fail,
    Stop,
    Errors...>> : std::true_type {};


template <typename T>
struct HasTransform : IsTransform<T> {};


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
struct HasTransform<
  Stream<
    Value,
    K,
    Context,
    Start,
    Next,
    Done,
    Fail,
    Stop,
    Errors...>> : HasTransform<K> {};


template <
  typename Value,
  typename K,
  typename Context,
  typename Start,
  typename Body,
  typename Ended,
  typename Fail,
  typename Stop,
  typename... Errors>
struct HasTerminal<
  Transform<
    Value,
    K,
    Context,
    Start,
    Body,
    Ended,
    Fail,
    Stop,
    Errors...>> : HasTerminal<K> {};


template <
  typename Value,
  typename... Errors,
  typename Context,
  typename Start,
  typename Body,
  typename Ended,
  typename Fail,
  typename Stop>
auto transform(
    Context context,
    Start start,
    Body body,
    Ended ended,
    Fail fail,
    Stop stop)
{
  return Transform<
    Value,
    Undefined,
    Context,
    Start,
    Body,
    Ended,
    Fail,
    Stop,
    Errors...> {
    Undefined(),
    std::move(context),
    std::move(start),
    std::move(body),
    std::move(ended),
    std::move(fail),
    std::move(stop)
  };
}


template <
  typename Value,
  typename K,
  typename Context,
  typename Start,
  typename Body,
  typename Ended,
  typename Fail,
  typename Stop,
  typename... Errors>
struct Loop
{
  using Type = Value;

  K k_;

  Context context_;
  Start start_;
  Body body_;
  Ended ended_;
  Fail fail_;
  Stop stop_;

  Loop(const Loop& that) = default;
  Loop(Loop&& that) = default;

  Loop& operator=(const Loop& that) = default;
  Loop& operator=(Loop&& that)
  {
    // TODO(benh): Change this to use 'swap' or investigate why the
    // compiler needs us to define this in the first place and can't
    // just resolve the move assignment operators for all the fields.
    this->~Loop();
    new(this) Loop(std::move(that));
    return *this;
  }

  template <typename S>
  void succeed(S& s)
  {
    start_(context_, s);
  }

  template <typename S, typename T>
  void body(S& s, T&& t)
  {
    body_(context_, s, std::forward<T>(t));
  }

  void ended()
  {
    ended_(context_, k_);
  }

  template <typename Error>
  void fail(Error&& error)
  {
    fail_(context_, k_, std::forward<Error>(error));
  }

  void stop()
  {
    stop_(context_, k_);
  }
};


template <typename>
struct IsLoop : std::false_type {};


template <
  typename Value,
  typename K,
  typename Context,
  typename Start,
  typename Body,
  typename Ended,
  typename Fail,
  typename Stop,
  typename... Errors>
struct IsLoop<
  Loop<
    Value,
    K,
    Context,
    Start,
    Body,
    Ended,
    Fail,
    Stop,
    Errors...>> : std::true_type {};


template <typename>
struct HasLoop : std::false_type {};


template <
  typename Value,
  typename K,
  typename Context,
  typename Start,
  typename Body,
  typename Ended,
  typename Fail,
  typename Stop,
  typename... Errors>
struct HasLoop<
  Loop<
    Value,
    K,
    Context,
    Start,
    Body,
    Ended,
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
  Stream<
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
  typename Body,
  typename Ended,
  typename Fail,
  typename Stop,
  typename... Errors>
struct HasLoop<
  Transform<
    Value,
    K,
    Context,
    Start,
    Body,
    Ended,
    Fail,
    Stop,
    Errors...>> : HasLoop<K> {};


template <
  typename Value,
  typename K,
  typename Context,
  typename Start,
  typename Body,
  typename Ended,
  typename Fail,
  typename Stop,
  typename... Errors>
struct HasTerminal<
  Loop<
    Value,
    K,
    Context,
    Start,
    Body,
    Ended,
    Fail,
    Stop,
    Errors...>> : HasTerminal<K> {};


template <
  typename Value,
  typename... Errors,
  typename Context,
  typename Start,
  typename Body,
  typename Ended,
  typename Fail,
  typename Stop>
auto loop(
    Context context,
    Start start,
    Body body,
    Ended ended,
    Fail fail,
    Stop stop)
{
  return Loop<
    Value,
    Undefined,
    Context,
    Start,
    Body,
    Ended,
    Fail,
    Stop,
    Errors...> {
    Undefined(),
    std::move(context),
    std::move(start),
    std::move(body),
    std::move(ended),
    std::move(fail),
    std::move(stop)
  };
}


template <
  typename Value,
  typename... Errors,
  typename Context,
  typename Body,
  typename Ended,
  typename Fail,
  typename Stop>
auto loop(
    Context context,
    Body body,
    Ended ended,
    Fail fail,
    Stop stop)
{
  return loop<Value, Errors...>(
      std::move(context),
      [](auto&, auto& stream) {
        next(stream);
      },
      std::move(body),
      std::move(ended),
      std::move(fail),
      std::move(stop));
}


namespace streams {

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
auto compose(
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

} // namespace streams {


template <
  typename S,
  typename L,
  std::enable_if_t<
    IsStream<S>::value
    && !HasLoop<S>::value
    && IsLoop<L>::value, int> = 0>
auto operator|(S s, L l)
{
  auto k = [&]() {
    if constexpr (HasTransform<S>::value) {
      return std::move(s.k_) | std::move(l);
    } else {
      return std::move(l);
    }
  }();

  return compose<typename L::Type>(
      streams::compose<typename S::Type>(
          std::move(k),
          std::move(s.context_),
          std::move(s.start_),
          std::move(s.next_),
          std::move(s.done_),
          std::move(s.fail_),
          std::move(s.stop_)),
      Undefined(),
      // TODO(benh): if 's.fail_' is not Undefined then assume that
      // this is an eventual *continuation* and create a 'start' that
      // takes a value and does a succeed and create a 'fail' that
      // propagates the error (instead of the current 'Undefined()').
      [](auto&, auto& k) {
        start(k);
      },
      Undefined(),
      [](auto&, auto& k) {
        stop(k);
      });
}


template <
  typename S,
  typename T,
  std::enable_if_t<
    IsStream<S>::value
    && !HasLoop<S>::value
    && IsTransform<T>::value, int> = 0>
auto operator|(S s, T t)
{
  if constexpr (HasTransform<S>::value) {
    return streams::compose<typename T::Type>(
        std::move(s.k_) | std::move(t),
        std::move(s.context_),
        std::move(s.start_),
        std::move(s.next_),
        std::move(s.done_),
        std::move(s.fail_),
        std::move(s.stop_));
  } else {
    return streams::compose<typename T::Type>(
        std::move(t),
        std::move(s.context_),
        std::move(s.start_),
        std::move(s.next_),
        std::move(s.done_),
        std::move(s.fail_),
        std::move(s.stop_));
  }
}


template <
  typename S,
  typename K,
  std::enable_if_t<
    IsStream<S>::value
    && HasLoop<S>::value
    && (IsEventual<K>::value
        || (IsTerminal<K>::value && !HasTerminal<S>::value)), int> = 0>
auto operator|(S s, K k)
{
  return streams::compose<typename S::Type>(
      std::move(s.k_) | std::move(k),
      std::move(s.context_),
      std::move(s.start_),
      std::move(s.next_),
      std::move(s.done_),
      std::move(s.fail_),
      std::move(s.stop_));
}


template <
  typename S,
  typename F,
  std::enable_if_t<
    IsStream<S>::value
    && !HasLoop<S>::value
    && !IsLoop<F>::value
    && !IsTransform<F>::value, int> = 0>
auto operator|(S s, F f)
{
  return std::move(s)
    | transform<decltype(f(std::declval<typename S::Type>()))>(
        std::move(f),
        [](auto&, auto& k, auto& stream) {
          succeed(k, stream);
        },
        [](auto& f, auto& k, auto& stream, auto&& value) {
          body(k, stream, f(std::forward<decltype(value)>(value)));
        },
        [](auto&, auto& k) {
          ended(k);
        },
        [](auto&, auto& k, auto&& error) {
          fail(k, std::forward<decltype(error)>(error));
        },
        [](auto&, auto& k) {
          stop(k);
        });
}


namespace transforms {

template <
  typename Value,
  typename... Errors,
  typename K,
  typename Context,
  typename Start,
  typename Body,
  typename Ended,
  typename Fail,
  typename Stop>
auto compose(
    K k,
    Context context,
    Start start,
    Body body,
    Ended ended,
    Fail fail,
    Stop stop)
{
  return Transform<
    Value,
    K,
    Context,
    Start,
    Body,
    Ended,
    Fail,
    Stop,
    Errors...> {
    std::move(k),
    std::move(context),
    std::move(start),
    std::move(body),
    std::move(ended),
    std::move(fail),
    std::move(stop)
  };
}

} // namespace transforms {


template <
  typename T,
  typename L,
  std::enable_if_t<
    IsTransform<T>::value
    && IsLoop<L>::value, int> = 0>
auto operator|(T t, L l)
{
  if constexpr (!IsUndefined<decltype(t.k_)>::value) {
    return transforms::compose<typename L::Type>(
        std::move(t.k_) | std::move(l),
        std::move(t.context_),
        std::move(t.start_),
        std::move(t.body_),
        std::move(t.ended_),
        std::move(t.fail_),
        std::move(t.stop_));
  } else {
    return transforms::compose<typename L::Type>(
        std::move(l),
        std::move(t.context_),
        std::move(t.start_),
        std::move(t.body_),
        std::move(t.ended_),
        std::move(t.fail_),
        std::move(t.stop_));
  }
}


template <
  typename T,
  typename K,
  std::enable_if_t<
    IsTransform<T>::value
    && IsTerminal<K>::value, int> = 0>
auto operator|(T t, K k)
{
  if constexpr (!IsUndefined<decltype(t.k_)>::value) {
    return transforms::compose<typename K::Type>(
        std::move(t.k_) | std::move(k),
        std::move(t.context_),
        std::move(t.start_),
        std::move(t.body_),
        std::move(t.ended_),
        std::move(t.fail_),
        std::move(t.stop_));
  } else {
    return transforms::compose<typename K::Type>(
        std::move(k),
        std::move(t.context_),
        std::move(t.start_),
        std::move(t.body_),
        std::move(t.ended_),
        std::move(t.fail_),
        std::move(t.stop_));
  }
}


namespace loops {

template <
  typename Value,
  typename... Errors,
  typename K,
  typename Context,
  typename Start,
  typename Body,
  typename Ended,
  typename Fail,
  typename Stop>
auto compose(
    K k,
    Context context,
    Start start,
    Body body,
    Ended ended,
    Fail fail,
    Stop stop)    
{
  return Loop<
    Value,
    K,
    Context,
    Start,
    Body,
    Ended,
    Fail,
    Stop,
    Errors...> {
    std::move(k),
    std::move(context),
    std::move(start),
    std::move(body),
    std::move(ended),
    std::move(fail),
    std::move(stop)
  };
}

} // namespace loops {


template <
  typename L,
  typename K,
  std::enable_if_t<
    IsLoop<L>::value
    && (IsEventual<K>::value
        || (IsTerminal<K>::value && !HasTerminal<L>::value)), int> = 0>
auto operator|(L l, K k)
{
  using Value = std::conditional_t<
    IsTerminal<K>::value,
    typename L::Type,
    typename K::Type>;

  if constexpr (HasEventualContinuation<L>::value) {
    return loops::compose<Value>(
        std::move(l.k_) | std::move(k),
        std::move(l.context_),
        std::move(l.start_),
        std::move(l.body_),
        std::move(l.ended_),
        std::move(l.fail_),
        std::move(l.stop_));
  } else {
    return loops::compose<Value>(
        std::move(k),
        std::move(l.context_),
        std::move(l.start_),
        std::move(l.body_),
        std::move(l.ended_),
        std::move(l.fail_),
        std::move(l.stop_));
  }
}


template <typename Value, typename F>
auto map(F f)
{
  return transform<Value>(
      std::move(f),
      [](auto&, auto& k, auto& stream) {
        succeed(k, stream);
      },
      [](auto& f, auto& k, auto& stream, auto&& value) {
        body(k, stream, f(std::forward<decltype(value)>(value)));
      },
      [](auto&, auto& k) {
        ended(k);
      },
      [](auto&, auto& k, auto&& error) {
        fail(k, std::forward<decltype(error)>(error));
      },
      [](auto&, auto& k) {
        stop(k);
      });
}


template <typename Value, typename T, typename F>
auto reduce(T t, F f)
{
  struct Context
  {
    T t;
    F f;
  };

  return loop<Value>(
      Context { std::move(t), std::move(f) },
      [](auto&, auto& stream) {
        next(stream);
      },
      [](auto& context, auto& stream, auto&& value) {
        context.t = context.f(
            std::move(context.t),
            std::forward<decltype(value)>(value));
        next(stream);
      },
      [](auto& context, auto& k) {
        succeed(k, std::move(context.t));
      },
      [](auto&, auto& k, auto&& error) {
        fail(k, std::forward<decltype(error)>(error));
      },
      [](auto&, auto& k) {
        stop(k);
      });
}

} // namespace eventuals {
} // namespace stout {
