#pragma once

#include "stout/eventuals.h"

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

  std::future<typename E::Type> future;
};


template <
  typename E,
  std::enable_if_t<
    IsEventual<E>::value
    && !IsEventualContinuation<E>::value
    && !HasTerminal<E>::value, int> = 0>
auto task(E e)
{
  std::promise<typename E::Type> promise;

  auto future = promise.get_future();

  auto t = std::move(e)
    | terminal(
        std::move(promise),
        [](auto& promise, auto&& value) {
          promise.set_value(std::forward<decltype(value)>(value));
        },
        [](auto& promise, auto&&...) {
          promise.set_exception(std::make_exception_ptr(FailedException()));
        },
        [](auto& promise) {
          promise.set_exception(std::make_exception_ptr(StoppedException()));
        });

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

