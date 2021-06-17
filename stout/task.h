#pragma once

#include "stout/eventual.h"

#include <future>
#include <sstream>

////////////////////////////////////////////////////////////////////////

namespace stout {
namespace eventuals {

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

template <typename Value, typename E>
auto Terminate(E e)
{
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

enum TaskWaitable {
  Waitable,
  NotWaitable,
};

////////////////////////////////////////////////////////////////////////

template <typename Value_, TaskWaitable waitable = Waitable>
struct Task
{
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

    static_assert(
        !HasTerminal<E>::value || waitable == NotWaitable,
        "Eventual already has a terminal so need to toggle 'NotWaitable'");

    auto make = [&, this]() {
      if constexpr (!HasTerminal<E>::value) {
        auto [future, t] = Terminate<Value_>(std::move(e));
        future_ = std::move(future);
        return std::move(t);
      } else {
        return std::move(e);
      }
    };

    e_ = std::make_shared<decltype(make())>(make());

    start_ = [this]() {
      auto& e = *static_cast<decltype(make())*>(e_.get());
      e.Register(interrupt_);
      start(e);
    };
  }

  void Start()
  {
    start_();
  }

  void Interrupt()
  {
    interrupt_.Trigger();
  }

  auto Wait()
  {
    static_assert(
        waitable == Waitable,
        "Task is not waitable (already has a terminal)");

    if constexpr (waitable == Waitable) {
      return future_.get();
    }
  }

  auto Run()
  {
    start_();
    return Wait();
  }

private:
  // NOTE: using a 'shared_ptr' instead of a 'unique_ptr' so that
  // we'll get destruction given we've type erased the eventual. Even
  // though a 'shared_ptr' is copyable, the 'Callback' should enforce
  // move only semantics.
  std::shared_ptr<void> e_;
  std::conditional_t<
    waitable == Waitable,
    std::future<Value_>,
    Undefined> future_;
  Callback<> start_;
  class Interrupt interrupt_;
};

////////////////////////////////////////////////////////////////////////

template <typename E>
auto TaskFrom(E e)
{
  if constexpr (!HasTerminal<E>::value) {
    return Task<typename E::Value>(std::move(e));
  } else {
    return Task<typename E::Value, NotWaitable>(std::move(e));
  }
}

////////////////////////////////////////////////////////////////////////

namespace detail {

////////////////////////////////////////////////////////////////////////

template <typename E>
auto operator*(E e)
{
  static_assert(
      !HasTerminal<E>::value,
      "Eventual already has terminal (maybe you want 'start()')");

  using Value = std::conditional_t<
    IsUndefined<typename E::Value>::value,
    void,
    typename E::Value>;

  auto [future, t] = Terminate<Value>(std::move(e));

  start(t);

  return future.get();
  // return run(task(std::move(e)));
}

////////////////////////////////////////////////////////////////////////

} // namespace detail {

////////////////////////////////////////////////////////////////////////

} // namespace eventuals {
} // namespace stout {

////////////////////////////////////////////////////////////////////////

