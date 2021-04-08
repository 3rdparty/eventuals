#pragma once

// TODO(benh): lifecycle management, i.e., don't let an eventual get
// started more than once or don't let an eventual get stopped if it
// hasn't been started. this is a little tricky because using
// something like a 'std::atomic' makes the Eventual neither copyable
// not moveable so we'd need some sort of wrapper around the Eventual
// (maybe an Operation, kind of like Task but without the
// std::promise/future). once we have lifecycle management then we can
// also make sure that after an eventual has been started we won't let
// it's destructor get executed until after it's terminated (i.e.,
// it's Terminal has been completed). Related: how to make sure that
// 'stopped()' is not called if 'succeeded()' is called? threre can be
// an outstanding call to 'stop()' while 'start()' is still executing,
// so is it up to the programmer to coordinate that they won't call
// both 'succeeded()' and 'stopped()'?
//
// TODO(benh): catch exceptions from 'start', 'fail', 'stop', etc.
//
// TODO(benh): create a 'then' which is a continuation that propagates
// 'fail' and 'stop' (note this is different then just using composing
// with a function because a 'then' would take a continuation 'k').
//
// TODO(benh): create a 'stream()' and 'loop()' and 'map()' and
// 'filter()'.
//
// TODO(benh): composing non-continuation eventual that doesn't have a
// 'fail' handler instead will need to propagate the failure past the
// eventual to the continuation.
//
// TODO(benh): eventual/continuation with no context should allow for
// functions that don't require a context either.
//
// TODO(benh): aggregate errors across all the eventuals.
//
// TODO(benh): lambda visitor for matching errors.

namespace stout {
namespace eventuals {

struct Empty {};

template <
  typename Value,
  typename K,
  typename Context,
  typename Start,
  typename Fail,
  typename Stop,
  typename... Errors>
struct Eventual
{
  using Type = Value;

  K k;

  Context context;
  Start start;
  Fail fail;
  Stop stop;

  Eventual(const Eventual& that) = default;
  Eventual(Eventual&& that) = default;

  Eventual& operator=(const Eventual& that) = default;
  Eventual& operator=(Eventual&& that)
  {
    // TODO(benh): Change this to use 'swap' or investigate why the
    // compiler needs us to define this in the first place and can't
    // just resolve the move assignment operators for all the fields.
    this->~Eventual();
    new(this) Eventual(std::move(that));
    return *this;
  }

  template <typename T>
  void succeeded(T&& t)
  {
    start(context, k, std::forward<T>(t));
  }

  template <typename Error>
  void failed(Error&& error)
  {
    fail(context, k, std::forward<Error>(error));
  }

  void stopped()
  {
    stop(context, k);
  }
};


template <typename>
struct IsEventual : std::false_type {};


template <
  typename Value,
  typename K,
  typename Context,
  typename Start,
  typename Fail,
  typename Stop,
  typename... Errors>
struct IsEventual<
  Eventual<
    Value,
    K,
    Context,
    Start,
    Fail,
    Stop,
    Errors...>> : std::true_type {};


template <typename>
struct HasEventualContinuation : std::false_type {};


template <
  typename Value,
  typename K,
  typename Context,
  typename Start,
  typename Fail,
  typename Stop,
  typename... Errors>
struct HasEventualContinuation<
  Eventual<
    Value,
    K,
    Context,
    Start,
    Fail,
    Stop,
    Errors...>> : std::conditional_t<
  std::is_same<K, Empty>::value,
  std::false_type,
  std::true_type> {};


template <typename>
struct IsEventualContinuation : std::false_type {};


template <
  typename Value,
  typename K,
  typename Context,
  typename Start,
  typename Fail,
  typename Stop,
  typename... Errors>
struct IsEventualContinuation<
  Eventual<
    Value,
    K,
    Context,
    Start,
    Fail,
    Stop,
    Errors...>> : std::conditional_t<
  std::is_same<Fail, Empty>::value,
  std::false_type,
  std::true_type> {};


template <
  typename Value,
  typename... Errors,
  typename Context,
  typename Start,
  typename Stop>
auto eventual(Context context, Start start, Stop stop)
{
  return Eventual<
    Value,
    Empty,
    Context,
    Start,
    Empty,
    Stop,
    Errors...> {
    Empty(),
    std::move(context),
    std::move(start),
    Empty(),
    std::move(stop)
  };
}


template <
  typename Value,
  typename... Errors,
  typename Start,
  typename Stop>
auto eventual(Start start, Stop stop)
{
  return eventual<Value, Errors...>(
      Empty(),
      std::move(start),
      std::move(stop));
}


template <
  typename Value,
  typename... Errors,
  typename Context,
  typename Start,
  typename Fail,
  typename Stop>
auto continuation(Context context, Start start, Fail fail, Stop stop)
{
  return Eventual<
    Value,
    Empty,
    Context,
    Start,
    Fail,
    Stop,
    Errors...> {
    Empty(),
    std::move(context),
    std::move(start),
    std::move(fail),
    std::move(stop)
  };
}


template <
  typename Value,
  typename... Errors,
  typename Start,
  typename Fail,
  typename Stop>
auto continuation(Start start, Fail fail, Stop stop)
{
  return continuation<Value, Errors...>(
      Empty(),
      std::move(start),
      std::move(fail),
      std::move(stop));
}


template <
  typename Context,
  typename Start,
  typename Fail,
  typename Stop>
struct Terminal
{
  using Type = Empty;

  Context context;
  Start start;
  Fail fail;
  Stop stop;

  template <typename T>
  void succeeded(T&& t)
  {
    start(context, std::forward<T>(t));
  }

  template <typename Error>
  void failed(Error&& error)
  {
    fail(context, std::forward<Error>(error));
  }

  void stopped()
  {
    stop(context);
  }
};


template <typename>
struct IsTerminal : std::false_type {};


template <typename Context, typename Start, typename Fail, typename Stop>
struct IsTerminal<Terminal<Context, Start, Fail, Stop>> : std::true_type {};


template <
  typename Context,
  typename Start,
  typename Fail,
  typename Stop>
auto terminal(Context context, Start start, Fail fail, Stop stop)
{
  return Terminal<Context, Start, Fail, Stop> {
    std::move(context),
    std::move(start),
    std::move(fail),
    std::move(stop)
  };
}


template <typename>
struct HasTerminal : std::false_type {};


template <
  typename Value,
  typename K,
  typename Context,
  typename Start,
  typename Fail,
  typename Stop,
  typename... Errors>
struct HasTerminal<
  Eventual<
    Value,
    K,
    Context,
    Start,
    Fail,
    Stop,
    Errors...>> : std::conditional_t<
  IsTerminal<K>::value,
  std::true_type,
  HasTerminal<K>> {};


template <typename K, typename T>
void succeeded(K& k, T&& t)
{
  static_assert(
      !std::is_same_v<K, Empty>,
      "You're using a continuation that goes nowhere!");

  k.succeeded(std::forward<T>(t));
}


template <typename K, typename Error>
void failed(K& k, Error&& error)
{
  static_assert(
      !std::is_same_v<K, Empty>,
      "You're using a continuation that goes nowhere!");

  k.failed(std::forward<Error>(error));
}


template <typename K>
void stopped(K& k)
{
  static_assert(
      !std::is_same_v<K, Empty>,
      "You're using a continuation that goes nowhere!");

  k.stopped();
}


template <
  typename Value,
  typename... Errors,
  typename K,
  typename Context,
  typename Start,
  typename Fail,
  typename Stop>
auto compose(K k, Context context, Start start, Fail fail, Stop stop)
{
  return Eventual<
    Value,
    K,
    Context,
    Start,
    Fail,
    Stop,
    Errors...> {
    std::move(k),
    std::move(context),
    std::move(start),
    std::move(fail),
    std::move(stop)
  };
}


template <
  typename E,
  typename K,
  std::enable_if_t<
    IsEventual<E>::value
    && (IsEventual<K>::value
        || (IsTerminal<K>::value && !HasTerminal<E>::value)), int> = 0>
auto operator|(E e, K k)
{
  using Value = std::conditional_t<
    IsTerminal<K>::value,
    typename E::Type,
    typename K::Type>;

  if constexpr (HasEventualContinuation<E>::value) {
    return compose<Value>(
        std::move(e.k) | std::move(k),
        std::move(e.context),
        std::move(e.start),
        std::move(e.fail),
        std::move(e.stop));
  } else {
    return compose<Value>(
        std::move(k),
        std::move(e.context),
        std::move(e.start),
        std::move(e.fail),
        std::move(e.stop));
  }
}


template <
  typename E,
  typename K,
  std::enable_if_t<
    IsEventual<E>::value
    && !IsEventual<K>::value
    && !IsTerminal<K>::value, int> = 0>
auto operator|(E e, K k)
{
  return std::move(e)
    | continuation<decltype(k(std::declval<typename E::Type>()))>(
        std::move(k),
        [](auto& k, auto&& kk, auto&& value) {
          succeeded(kk, k(std::forward<decltype(value)>(value)));
        },
        [](auto&, auto&& kk, auto&& error) {
          failed(kk, std::forward<decltype(error)>(error));
        },
        [](auto&, auto&& kk) {
          stopped(kk);
        });
}


template <
  typename E,
  std::enable_if_t<
    IsEventual<E>::value && HasTerminal<E>::value, int> = 0>
void start(E& e)
{
  e.start(e.context, e.k);
}


template <
  typename E,
  std::enable_if_t<
    IsEventual<E>::value && HasTerminal<E>::value, int> = 0>
void stop(E& e)
{
  e.stop(e.context, e.k);
}

} // namespace eventuals {
} // namespace stout {
