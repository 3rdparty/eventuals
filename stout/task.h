#pragma once

#include "stout/eventual.h"

#include <future>

namespace stout {
namespace eventuals {

template <typename S, typename T, typename = void>
struct Streamable : std::false_type {};

template <typename S, typename T>
struct Streamable<
  S,
  T,
  decltype(void(std::declval<S&>() << std::declval<T const&>()))>
  : std::true_type {};


struct StoppedException : public std::exception
{
  const char* what() const throw()
  {
    return "Eventual computation stopped (cancelled)";
  }
};


struct FailedException : public std::exception
{
  template <
    typename Error,
    std::enable_if_t<
      !std::is_same_v<Error, FailedException>, int> = 0>
  FailedException(Error&& error)
  {
    static_assert(
        !std::is_same_v<Error, FailedException>,
        "Why is compiler choosing this constructor?");

    std::stringstream ss;
    ss << "Eventual computation failed";
    if constexpr (Streamable<std::stringstream, Error>::value) {
      ss << ": " << error;
    } else {
      ss << " (error of type " << typeid(Error).name() << ")";
    }
    message_ = ss.str();
  }

  FailedException(FailedException&& that)
    : message_(std::move(that.message_)) {}

  const char* what() const throw()
  {
    return message_.c_str();
  }

  std::string message_;
};


template <typename E_>
struct Task
{
  using Value = std::conditional_t<
    IsUndefined<typename E_::Value>::value,
    void,
    typename E_::Value>;

  // NOTE: task is not copyable or moveable because of 'std::future',
  // but that's ok because once started if the task got moved that
  // would be catastrophic since computation is already underway and
  // expecting things to be at specific places in memory.

  E_ e_;

  std::future<Value> future_;

  Interrupt interrupt_;
};


template <
  typename E,
  std::enable_if_t<
    !HasTerminal<E>::value, int> = 0>
auto task(E e)
{
  using Value = std::conditional_t<
    IsUndefined<typename E::Value>::value,
    void,
    typename E::Value>;

  std::promise<Value> promise;

  auto future = promise.get_future();

  auto t = std::move(e)
    | (Terminal()
       .context(std::move(promise))
       .start([](auto& promise, auto&&... values) {
         static_assert(sizeof...(values) == 0 || sizeof...(values) == 1,
                       "Task only supports 0 or 1 value, but found > 1");
         promise.set_value(std::forward<decltype(values)>(values)...);
       })
       .fail([](auto& promise, auto&&... errors) {
         promise.set_exception(
             std::make_exception_ptr(
                 FailedException(std::forward<decltype(errors)>(errors)...)));
       })
       .stop([](auto& promise) {
         promise.set_exception(
             std::make_exception_ptr(
                 StoppedException()));
       }));

  return Task<decltype(t)> {
    std::move(t),
    std::move(future)
  };
}


template <typename E>
Task<E>& start(Task<E>& task)
{
  task.e_.Register(task.interrupt_);
  start(task.e_);
  return task;
}


template <typename E>
Task<E>& interrupt(Task<E>& task)
{
  task.interrupt_.Trigger();
  return task;
}


template <typename E>
auto wait(Task<E>& task)
{
  return task.future_.get();
}


template <typename E>
auto run(Task<E>& task)
{
  return wait(start(task));
}


template <typename E>
auto run(Task<E>&& task)
{
  return run(task);
}

} // namespace eventuals {
} // namespace stout {

