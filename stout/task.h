#pragma once

#include "stout/eventual.h"
#include "stout/lambda.h"

#include <future>
#include <sstream>

////////////////////////////////////////////////////////////////////////

namespace stout {
namespace eventuals {

////////////////////////////////////////////////////////////////////////

// Forward declaration.
template <typename Value, bool terminated, typename... Args>
struct TaskWith;

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
struct HeapTask
{
  HeapTask(E_ e)
    : adaptor_(
        std::move(e)
        | TaskAdaptor<typename E_::Value>(&start_, &fail_, &stop_)) {}

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

template <typename K_, typename Value_, bool terminated_, typename... Args_>
struct TaskContinuation
{
  using Value = typename ValueFrom<K_, Value_>::type;

  TaskContinuation(K_ k, TaskWith<Value_, terminated_, Args_...> task)
    : k_(std::move(k)), task_(std::move(task)) {}

  template <typename Value, bool terminated, typename... Args, typename K>
  static auto create(K k, TaskWith<Value, terminated, Args...> task)
  {
    return TaskContinuation<K, Value, terminated, Args...>(
        std::move(k),
        std::move(task));
  }

  template <
    typename K,
    std::enable_if_t<
      IsContinuation<K>::value, int> = 0>
  auto k(K k) &&
  {
    static_assert(
        !terminated_,
        "Task already terminated, can't compose continuation");

    return create<Value_, terminated_, Args_...>(
        [&]() {
          if constexpr (!IsUndefined<K_>::value) {
            return std::move(k_) | std::move(k);
          } else {
            return std::move(k);
          }
        }(),
        std::move(task_));
  }

  template <
    typename F,
    std::enable_if_t<
      !IsContinuation<F>::value, int> = 0>
  auto k(F f) &&
  {
    return std::move(*this) | eventuals::Lambda(std::move(f));
  }

  template <typename... Args>
  void Start(Args&&...)
  {
    static_assert(
        !IsUndefined<K_>::value,
        "Trying to start a task that doesn't have a continuation!");

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

  template <typename... Args>
  void Fail(Args&&... args)
  {
    // TODO(benh): propagate through 'Task'.
    eventuals::fail(k_, std::forward<Args>(args)...);
  }

  void Stop()
  {
    // TODO(benh): propagate through 'Task'.
    eventuals::stop(k_);
  }

  void Register(Interrupt& interrupt)
  {
    interrupt_ = &interrupt;
    k_.Register(interrupt);
  }

  K_ k_;
  TaskWith<Value_, terminated_, Args_...> task_;

  Interrupt* interrupt_ = nullptr;
};

////////////////////////////////////////////////////////////////////////

} // namespace detail {

////////////////////////////////////////////////////////////////////////

template <typename Value_, bool terminated_, typename... Args_>
struct TaskWith
{
  using Value = Value_;

  template <typename F>
  TaskWith(Args_... args, F f)
    : args_(std::move(args)...)
  {
    static_assert(
        !IsContinuation<F>::value,
        "'Task' expects a callable "
        "NOT an eventual continuation");

    static_assert(
        sizeof...(args) > 0
        || std::is_invocable_v<F>,
        "'Task' expects a callable that "
        "takes no arguments");

    static_assert(
        sizeof...(args) == 0
        || std::is_invocable_v<F, Args_...>,
        "'Task' expects a callable that "
        "takes the arguments specified");

    static_assert(
        sizeof(f) <= sizeof(void*),
        "'Task' expects a callable that "
        "can be captured in a 'Callback'");

    using E = decltype(f(std::move(args)...));

    static_assert(
        IsContinuation<E>::value,
        "'Task' expects a callable that returns "
        "an eventual continuation");

    static_assert(
        (HasTerminal<E>::value && terminated_)
        || (!HasTerminal<E>::value && !terminated_),
        "You need to add 'true' as the second template "
        "argument for eventuals that are terminated, "
        "i.e., 'Task<Value, true>'");

    using Value = std::conditional_t<
      IsUndefined<typename E::Value>::value,
      void,
      typename E::Value>;

    static_assert(
        std::is_convertible_v<Value, Value_>,
        "Type of eventual can not be converted into type specified");

    start_ = [f = std::move(f)](
        Args_&&... args,
        std::unique_ptr<void, Callback<void*>>& e_,
        class Interrupt& interrupt,
        Callback<Value>&& start,
        Callback<std::exception_ptr>&& fail,
        Callback<>&& stop) {

      if (!e_) {
        e_ = std::unique_ptr<void, Callback<void*>>(
            // TODO(benh): pass the args to 'Start()' instead so that
            // they don't have to get copied more than once in the
            // event that the eventual returned from 'f' copies them?
            new detail::HeapTask<E>(f(std::move(args)...)),
            [](void* e) {
              delete static_cast<detail::HeapTask<E>*>(e);
            });
      }

      auto* e = static_cast<detail::HeapTask<E>*>(e_.get());

      e->Start(interrupt, std::move(start), std::move(fail), std::move(stop));
    };
  }

  template <
    typename K,
    std::enable_if_t<
      IsContinuation<K>::value, int> = 0>
  auto k(K k) &&
  {
    static_assert(
        !terminated_,
        "Task already terminated, can't compose continuation");

    return detail::TaskContinuation<K, Value_, terminated_, Args_...>(
        std::move(k),
        std::move(*this));
  }

  template <
    typename F,
    std::enable_if_t<
      !IsContinuation<F>::value, int> = 0>
  auto k(F f) &&
  {
    return std::move(*this) | eventuals::Lambda(std::move(f));
  }

  void Start(
      Interrupt& interrupt,
      Callback<Value_>&& start,
      Callback<std::exception_ptr>&& fail,
      Callback<>&& stop)
  {
    std::apply(
        [&](auto&&... args) {
          start_(
              std::forward<decltype(args)>(args)...,
              e_,
              interrupt,
              std::move(start),
              std::move(fail),
              std::move(stop));
        },
        std::move(args_));
  }

  auto operator*() &&
  {
    static_assert(
        !terminated_,
        "Task already has a terminal (maybe you just want to start it?)");

    auto [future, t] = Terminate(std::move(*this));

    start(t);

    return future.get();
  }

  std::tuple<Args_...> args_;

  std::unique_ptr<void, Callback<void*>> e_;

  Callback<
    Args_&&...,
    std::unique_ptr<void, Callback<void*>>&,
    class Interrupt&,
    Callback<Value_>&&,
    Callback<std::exception_ptr>&&,
    Callback<>&&> start_;
};


template <typename Value_, bool terminated_ = false>
class Task
{
public:
  using Value = Value_;

  template <typename... Args>
  using With = TaskWith<Value_, terminated_, Args...>;

  template <typename F>
  Task(F f)
    : task_(std::move(f)) {}

  template <typename K>
  auto k(K k) &&
  {
    return std::move(task_) | std::move(k);
  }

  void Start(
      Interrupt& interrupt,
      Callback<Value_>&& start,
      Callback<std::exception_ptr>&& fail,
      Callback<>&& stop)
  {
    task_.Start(interrupt, std::move(start), std::move(fail), std::move(stop));
  }

  auto operator*() &&
  {
    static_assert(
        !terminated_,
        "Task already has a terminal (maybe you just want to start it?)");

    auto [future, t] = Terminate(std::move(*this));

    start(t);

    return future.get();
  }

private:
  template <typename>
  friend struct Compose;

  TaskWith<Value_, terminated_> task_;
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

template <typename K, typename Value, bool terminated, typename... Args>
struct IsContinuation<
  detail::TaskContinuation<K, Value, terminated, Args...>> : std::true_type {};

////////////////////////////////////////////////////////////////////////

template <typename K, typename Value, bool terminated, typename... Args>
struct HasTerminal<
  detail::TaskContinuation<K, Value, terminated, Args...>> : HasTerminal<K> {};

////////////////////////////////////////////////////////////////////////

template <typename Value, bool terminated>
struct IsContinuation<
  Task<Value, terminated>> : std::true_type {};

////////////////////////////////////////////////////////////////////////

template <typename Value, bool terminated>
struct HasTerminal<
  Task<Value, terminated>> : std::bool_constant<terminated> {};

////////////////////////////////////////////////////////////////////////

template <typename Value, bool terminated>
struct Compose<Task<Value, terminated>>
{
  template <typename Arg>
  static auto compose(Task<Value, terminated> task)
  {
    return detail::TaskContinuation<Undefined, Value, terminated>(
        Undefined(),
        std::move(task.task_));
  }
};

////////////////////////////////////////////////////////////////////////

template <typename Value, bool terminated, typename... Args>
struct IsContinuation<
  TaskWith<Value, terminated, Args...>> : std::true_type {};

////////////////////////////////////////////////////////////////////////

template <typename Value, bool terminated, typename... Args>
struct HasTerminal<
  TaskWith<Value, terminated, Args...>> : std::bool_constant<terminated> {};

////////////////////////////////////////////////////////////////////////

template <typename Value, bool terminated, typename... Args>
struct Compose<TaskWith<Value, terminated, Args...>>
{
  template <typename Arg>
  static auto compose(TaskWith<Value, terminated, Args...> task)
  {
    return detail::TaskContinuation<Undefined, Value, terminated, Args...>(
        Undefined(),
        std::move(task));
  }
};

////////////////////////////////////////////////////////////////////////

namespace detail {

////////////////////////////////////////////////////////////////////////

template <typename E>
auto operator*(E e)
{
  static_assert(
      !HasTerminal<E>::value,
      "Eventual already has terminal (maybe you just want to start it?)");

  auto [future, t] = Terminate(std::move(e));

  start(t);

  return future.get();
}

////////////////////////////////////////////////////////////////////////

} // namespace detail {

////////////////////////////////////////////////////////////////////////

} // namespace eventuals {
} // namespace stout {

////////////////////////////////////////////////////////////////////////

