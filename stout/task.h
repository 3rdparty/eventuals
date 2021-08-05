#pragma once

#include "stout/eventual.h"
#include "stout/terminal.h"

////////////////////////////////////////////////////////////////////////

namespace stout {
namespace eventuals {

////////////////////////////////////////////////////////////////////////

namespace detail {

////////////////////////////////////////////////////////////////////////

template <typename E_>
struct HeapTask {
  using Value_ = typename E_::template ValueFrom<void>;

  template <typename Arg_>
  struct Adaptor {
    Adaptor(
        Callback<Arg_>* start,
        Callback<std::exception_ptr>* fail,
        Callback<>* stop)
      : start_(start),
        fail_(fail),
        stop_(stop) {}

    template <typename... Args>
    void Start(Args&&... args) {
      (*start_)(std::forward<Args>(args)...);
    }

    template <typename... Args>
    void Fail(Args&&... args) {
      (*fail_)(
          std::make_exception_ptr(
              FailedException(std::forward<decltype(args)>(args)...)));
    }

    void Stop() {
      (*stop_)();
    }

    void Register(Interrupt&) {}

    Callback<Arg_>* start_;
    Callback<std::exception_ptr>* fail_;
    Callback<>* stop_;
  };

  HeapTask(E_ e)
    : adaptor_(
        std::move(e).template k<void>(
            Adaptor<Value_>{&start_, &fail_, &stop_})) {}

  void Start(
      Interrupt& interrupt,
      Callback<Value_>&& start,
      Callback<std::exception_ptr>&& fail,
      Callback<>&& stop) {
    start_ = std::move(start);
    fail_ = std::move(fail);
    stop_ = std::move(stop);

    // TODO(benh): clarify the semantics of whether or not calling
    // 'Register()' more than once is well-defined.
    adaptor_.Register(interrupt);

    eventuals::start(adaptor_);
  }

  Callback<Value_> start_;
  Callback<std::exception_ptr> fail_;
  Callback<> stop_;

  using Adaptor_ = decltype(std::declval<E_>().template k<void>(
      std::declval<Adaptor<Value_>>()));

  Adaptor_ adaptor_;
};

////////////////////////////////////////////////////////////////////////

template <typename K_, typename Value_, typename... Args_>
struct _TaskWith {
  template <typename... Args>
  void Start(Args&&...) {
    std::apply(
        [&](auto&&... args) {
          start_(
              std::forward<decltype(args)>(args)...,
              e_,
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
        },
        std::move(args_));
  }

  template <typename... Args>
  void Fail(Args&&... args) {
    // TODO(benh): propagate through 'Task'.
    eventuals::fail(k_, std::forward<Args>(args)...);
  }

  void Stop() {
    // TODO(benh): propagate through 'Task'.
    eventuals::stop(k_);
  }

  void Register(Interrupt& interrupt) {
    interrupt_ = &interrupt;
    k_.Register(interrupt);
  }

  K_ k_;

  std::tuple<Args_...> args_;

  Callback<
      Args_&&...,
      std::unique_ptr<void, Callback<void*>>&,
      Interrupt&,
      Callback<Value_>&&,
      Callback<std::exception_ptr>&&,
      Callback<>&&>
      start_;

  std::unique_ptr<void, Callback<void*>> e_;

  Interrupt* interrupt_ = nullptr;
};

////////////////////////////////////////////////////////////////////////

} // namespace detail

////////////////////////////////////////////////////////////////////////

template <typename Value_, typename... Args_>
struct TaskWith {
  template <typename Arg>
  using ValueFrom = Value_;

  template <typename F>
  TaskWith(Args_... args, F f)
    : args_(std::move(args)...) {
    // static_assert(
    //     !IsContinuation<F>::value,
    //     "'Task' expects a callable "
    //     "NOT an eventual continuation");

    static_assert(
        sizeof...(args) > 0 || std::is_invocable_v<F>,
        "'Task' expects a callable that "
        "takes no arguments");

    static_assert(
        sizeof...(args) == 0 || std::is_invocable_v<F, Args_...>,
        "'Task' expects a callable that "
        "takes the arguments specified");

    static_assert(
        sizeof(f) <= sizeof(void*),
        "'Task' expects a callable that "
        "can be captured in a 'Callback'");

    using E = decltype(f(std::move(args)...));

    // static_assert(
    //     IsContinuation<E>::value,
    //     "'Task' expects a callable that returns "
    //     "an eventual continuation");

    using Value = typename E::template ValueFrom<void>;

    static_assert(
        std::is_convertible_v<Value, Value_>,
        "eventual result type can not be converted into type of 'Task'");

    start_ = [f = std::move(f)](
                 Args_&&... args,
                 std::unique_ptr<void, Callback<void*>>& e_,
                 Interrupt& interrupt,
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

  template <typename Arg, typename K>
  auto k(K k) && {
    // TODO(benh): ensure we haven't already called 'Start()'.
    return detail::_TaskWith<K, Value_, Args_...>{
        std::move(k),
        std::move(args_),
        std::move(start_)};
  }

  void Start(
      Interrupt& interrupt,
      Callback<Value_>&& start,
      Callback<std::exception_ptr>&& fail,
      Callback<>&& stop) {
    // TODO(benh): ensure we haven't already called 'k()'.
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

  auto operator*() && {
    auto [future, k] = Terminate(std::move(*this));

    start(k);

    return future.get();
  }

  std::tuple<Args_...> args_;

  Callback<
      Args_&&...,
      std::unique_ptr<void, Callback<void*>>&,
      Interrupt&,
      Callback<Value_>&&,
      Callback<std::exception_ptr>&&,
      Callback<>&&>
      start_;

  std::unique_ptr<void, Callback<void*>> e_;
};

////////////////////////////////////////////////////////////////////////

template <typename Value_>
class Task {
 public:
  template <typename... Args>
  using With = TaskWith<Value_, Args...>;

  template <typename Arg>
  using ValueFrom = Value_;

  template <typename F>
  Task(F f)
    : task_(std::move(f)) {}

  template <typename Arg, typename K>
  auto k(K k) && {
    return std::move(task_).template k<Arg>(std::move(k));
  }

  void Start(
      Interrupt& interrupt,
      Callback<Value_>&& start,
      Callback<std::exception_ptr>&& fail,
      Callback<>&& stop) {
    task_.Start(interrupt, std::move(start), std::move(fail), std::move(stop));
  }

  auto operator*() && {
    auto [future, k] = Terminate(std::move(*this));

    start(k);

    return future.get();
  }

 private:
  TaskWith<Value_> task_;
};

////////////////////////////////////////////////////////////////////////

} // namespace eventuals
} // namespace stout

////////////////////////////////////////////////////////////////////////
