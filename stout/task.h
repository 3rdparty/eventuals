#pragma once

#include "stout/eventual.h"

#include <future>
#include <sstream>

////////////////////////////////////////////////////////////////////////

namespace stout {
namespace eventuals {

////////////////////////////////////////////////////////////////////////

// Forward declaration.
template <typename Value>
struct Task;

////////////////////////////////////////////////////////////////////////

template <typename S, typename T, typename = void>
struct Streamable : std::false_type {};

template <typename S, typename T>
struct Streamable<
  S,
  T,
  decltype(void(std::declval<S&>() << std::declval<T const&>()))>
  : std::true_type {};

////////////////////////////////////////////////////////////////////////

struct StoppedException : public std::exception
{
  const char* what() const throw()
  {
    return "Eventual computation stopped (cancelled)";
  }
};

////////////////////////////////////////////////////////////////////////

struct FailedException : public std::exception
{
  template <typename Error>
  FailedException(const Error& error)
  {
    std::stringstream ss;
    ss << "Eventual computation failed";
    if constexpr (Streamable<std::stringstream, Error>::value) {
      ss << ": " << error;
    } else {
      ss << " (error of type " << typeid(Error).name() << ")";
    }
    message_ = ss.str();
  }

  // NOTE: this copy constructor is necessary because the compiler
  // might do a copy when doing a throw even though we have the move
  // constructor below.
  FailedException(const FailedException& that)
    : message_(that.message_) {}

  FailedException(FailedException&& that)
    : message_(std::move(that.message_)) {}

  const char* what() const throw()
  {
    return message_.c_str();
  }

  std::string message_;
};

////////////////////////////////////////////////////////////////////////

template <typename E>
auto Terminate(E e)
{
  using Value = std::conditional_t<
    IsUndefined<typename E::Value>::value,
    void,
    typename E::Value>;

  std::promise<Value> promise;
  auto future = promise.get_future();
  return std::tuple {
    std::move(future),
    std::move(e)
      | (Terminal()
         .context(std::move(promise))
         .start([](auto& promise, auto&&... values) {
           static_assert(
               sizeof...(values) == 0 || sizeof...(values) == 1,
               "Task only supports 0 or 1 value, but found > 1");
           promise.set_value(std::forward<decltype(values)>(values)...);
         })
         .fail([](auto& promise, auto&&... errors) {
           promise.set_exception(
               std::make_exception_ptr(
                   FailedException(
                       std::forward<decltype(errors)>(errors)...)));
         })
         .stop([](auto& promise) {
           promise.set_exception(
               std::make_exception_ptr(
                   StoppedException()));
         }))
  };
}

////////////////////////////////////////////////////////////////////////

namespace detail {

////////////////////////////////////////////////////////////////////////

// TODO(benh): Merge this with exisiting 'Adaptor' which only
// currently supports overriding the 'Start()' callback.
template <typename Arg_>
struct TaskAdaptor
{
  using Value = Arg_;

  TaskAdaptor(
      Callback<Arg_>* start,
      Callback<std::exception_ptr>* fail,
      Callback<>* stop)
    : start_(start),
      fail_(fail),
      stop_(stop) {}

  template <typename... Args>
  void Start(Args&&... args)
  {
    (*start_)(std::forward<Args>(args)...);
  }

  template <typename... Args>
  void Fail(Args&&... args)
  {
    (*fail_)(
        std::make_exception_ptr(
            FailedException(std::forward<decltype(args)>(args)...)));
  }

  void Stop()
  {
    (*stop_)();
  }

  void Register(Interrupt&) {}

  Callback<Arg_>* start_;
  Callback<std::exception_ptr>* fail_;
  Callback<>* stop_;
};

////////////////////////////////////////////////////////////////////////

template <typename E_>
struct Task
{
  Task(E_ e)
    : adaptor_(
        std::move(e).k(
            TaskAdaptor<typename E_::Value>(&start_, &fail_, &stop_))) {}

  void Start(
      Interrupt& interrupt,
      Callback<typename E_::Value>&& start,
      Callback<std::exception_ptr>&& fail,
      Callback<>&& stop)
  {
    start_ = std::move(start);
    fail_ = std::move(fail);
    stop_ = std::move(stop);

    adaptor_.Register(interrupt);

    eventuals::start(adaptor_);
  }

  Callback<typename E_::Value> start_;
  Callback<std::exception_ptr> fail_;
  Callback<> stop_;

  using Adaptor_ = typename EKPossiblyUndefined<
    E_,
    TaskAdaptor<typename E_::Value>>::type;

  Adaptor_ adaptor_;
};

////////////////////////////////////////////////////////////////////////

template <typename K_, typename Value_>
struct TaskContinuation
{
  using Value = typename K_::Value;

  TaskContinuation(K_ k, eventuals::Task<Value_> task)
    : k_(std::move(k)), task_(std::move(task)) {}

  template <typename K, typename Value>
  static auto create(K k, eventuals::Task<Value> task)
  {
    return TaskContinuation<K, Value>(std::move(k), std::move(task));
  }

  template <typename K>
  auto k(K k) &&
  {
    return create(
        [&]() {
          return std::move(k_) | std::move(k);
        }(),
        std::move(task_));
  }

  void Start()
  {
    task_.Start(
        *interrupt_,
        [this](auto&&... args) {
          eventuals::succeed(k_, std::forward<decltype(args)>(args)...);
        },
        [this](std::exception_ptr e) {
          eventuals::fail(k_, std::move(e));
        },
        [this]() {
          eventuals::stop(k_);
        });
  }

  void Register(Interrupt& interrupt)
  {
    interrupt_ = &interrupt;
  }

  K_ k_;
  eventuals::Task<Value_> task_;

  Interrupt* interrupt_ = nullptr;
};

////////////////////////////////////////////////////////////////////////

} // namespace detail {

////////////////////////////////////////////////////////////////////////

template <typename Value_>
struct Task
{
  using Value = Value_;

  template <typename E>
  Task(E e)
  {
    using Value = std::conditional_t<
      IsUndefined<typename E::Value>::value,
      void,
      typename E::Value>;

    static_assert(
        std::is_convertible_v<Value, Value_>,
        "Type of eventual can not be converted into type specified");

    e_ = std::unique_ptr<void, Callback<void*>>(
        new detail::Task<E>(std::move(e)),
        [](void* e) {
          delete static_cast<detail::Task<E>*>(e);
        });

    start_ = [&e = *static_cast<detail::Task<E>*>(e_.get())](
        class Interrupt& interrupt,
        Callback<Value>&& start,
        Callback<std::exception_ptr>&& fail,
        Callback<>&& stop) {

      e.Start(interrupt, std::move(start), std::move(fail), std::move(stop));
    };
  }

  template <typename K>
  auto k(K k) &&
  {
    assert(e_);
    auto kk = compose<Value_>(std::move(k));
    return detail::TaskContinuation<decltype(kk), Value_>(
        std::move(kk),
        std::move(*this));
  }

  void Start(
      Interrupt& interrupt,
      Callback<Value_>&& start,
      Callback<std::exception_ptr>&& fail,
      Callback<>&& stop)
  {
    assert(e_);
    start_(interrupt, std::move(start), std::move(fail), std::move(stop));
  }

private:
  std::unique_ptr<void, Callback<void*>> e_;

  Callback<
    class Interrupt&,
    Callback<Value_>&&,
    Callback<std::exception_ptr>&&,
    Callback<>&&> start_;
};

////////////////////////////////////////////////////////////////////////

template <typename Arg>
struct IsContinuation<
  detail::TaskAdaptor<Arg>> : std::true_type {};

////////////////////////////////////////////////////////////////////////

template <typename Arg>
struct HasTerminal<
  detail::TaskAdaptor<Arg>> : std::true_type {};

////////////////////////////////////////////////////////////////////////

template <typename K, typename Value>
struct IsContinuation<
  detail::TaskContinuation<K, Value>> : std::true_type {};

////////////////////////////////////////////////////////////////////////

template <typename K, typename Value>
struct HasTerminal<
  detail::TaskContinuation<K, Value>> : HasTerminal<K> {};

////////////////////////////////////////////////////////////////////////

namespace detail {

////////////////////////////////////////////////////////////////////////

template <typename E>
auto operator*(E e)
{
  static_assert(
      !HasTerminal<E>::value,
      "Eventual already has terminal (maybe you want 'start()')");

  auto [future, t] = Terminate(std::move(e));

  start(t);

  return future.get();
}

////////////////////////////////////////////////////////////////////////

} // namespace detail {

////////////////////////////////////////////////////////////////////////

template <typename Value>
auto operator*(Task<Value> task)
{
  auto [future, t] = Terminate(std::move(task));

  start(t);

  return future.get();
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals {
} // namespace stout {

////////////////////////////////////////////////////////////////////////

