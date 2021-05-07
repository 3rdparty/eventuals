#pragma once

#include "stout/eventual.h"

#include <future>

namespace stout {
namespace eventuals {

struct StoppedException : public std::exception
{
  const char* what() const throw()
  {
    return "Eventual computation stopped (cancelled)";
  }
};


struct FailedException : public std::exception
{
  const char* what() const throw()
  {
    return "Eventual computation failed";
  }
};


template <typename E>
struct Task
{
  // NOTE: task is not copyable or moveable because of 'std::future',
  // but that's ok because once started if the task got moved that
  // would be catastrophic since computation is already underway and
  // expecting things to be at specific places in memory.

  E e;

  using Value = std::conditional_t<
    IsUndefined<typename E::Value>::value,
    void,
    typename E::Value>;

  std::future<Value> future;
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
       .fail([](auto& promise, auto&&...) {
         promise.set_exception(std::make_exception_ptr(FailedException()));
       })
       .stop([](auto& promise) {
         promise.set_exception(std::make_exception_ptr(StoppedException()));
       }));

  return Task<decltype(t)> {
    std::move(t),
    std::move(future)
  };
}


template <typename E>
Task<E>& start(Task<E>& task)
{
  start(task.e);
  return task;
}


template <typename E>
Task<E>& stop(Task<E>& task)
{
  stop(task.e);
  return task;
}


template <typename E>
auto wait(Task<E>& task)
{
  return task.future.get();
}


template <typename E>
auto run(Task<E>&& task)
{
  start(task);
  return wait(task);
}

} // namespace eventuals {
} // namespace stout {

