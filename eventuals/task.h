#pragma once

#include <functional> // For 'std::reference_wrapper'.
#include <memory> // For 'std::unique_ptr'.
#include <optional>
#include <tuple>
#include <variant>

#include "eventuals/eventual.h"
#include "eventuals/just.h"
#include "eventuals/raise.h"
#include "eventuals/terminal.h"
#include "eventuals/type-traits.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

template <typename E_, typename From_, typename To_>
struct HeapTask {
  struct Adaptor {
    Adaptor(
        std::conditional_t<
            std::is_void_v<To_>,
            Callback<>,
            Callback<To_>>* start,
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
          make_exception_ptr_or_forward(
              std::forward<decltype(args)>(args)...));
    }

    // NOTE: overload so we don't create nested std::exception_ptr.
    void Fail(std::exception_ptr exception) {
      (*fail_)(std::move(exception));
    }

    void Stop() {
      (*stop_)();
    }

    void Register(Interrupt&) {}

    std::conditional_t<
        std::is_void_v<To_>,
        Callback<>,
        Callback<To_>>* start_;
    Callback<std::exception_ptr>* fail_;
    Callback<>* stop_;
  };

  HeapTask(E_ e)
    : adapted_(
        std::move(e).template k<From_>(
            Adaptor{&start_, &fail_, &stop_})) {}

  void Start(
      std::conditional_t<
          std::is_void_v<From_>,
          std::monostate,
          From_>&& arg,
      Interrupt& interrupt,
      std::conditional_t<
          std::is_void_v<To_>,
          Callback<>,
          Callback<To_>>&& start,
      Callback<std::exception_ptr>&& fail,
      Callback<>&& stop) {
    start_ = std::move(start);
    fail_ = std::move(fail);
    stop_ = std::move(stop);

    // TODO(benh): clarify the semantics of whether or not calling
    // 'Register()' more than once is well-defined.
    adapted_.Register(interrupt);

    if constexpr (std::is_void_v<From_>) {
      adapted_.Start();
    } else {
      adapted_.Start(std::move(arg));
    }
  }

  void Fail(
      Interrupt& interrupt,
      std::exception_ptr&& exception,
      std::conditional_t<
          std::is_void_v<To_>,
          Callback<>,
          Callback<To_>>&& start,
      Callback<std::exception_ptr>&& fail,
      Callback<>&& stop) {
    start_ = std::move(start);
    fail_ = std::move(fail);
    stop_ = std::move(stop);

    // TODO(benh): clarify the semantics of whether or not calling
    // 'Register()' more than once is well-defined.
    adapted_.Register(interrupt);

    adapted_.Fail(std::move(exception));
  }

  void Stop(
      Interrupt& interrupt,
      std::conditional_t<
          std::is_void_v<To_>,
          Callback<>,
          Callback<To_>>&& start,
      Callback<std::exception_ptr>&& fail,
      Callback<>&& stop) {
    start_ = std::move(start);
    fail_ = std::move(fail);
    stop_ = std::move(stop);

    // TODO(benh): clarify the semantics of whether or not calling
    // 'Register()' more than once is well-defined.
    adapted_.Register(interrupt);

    adapted_.Stop();
  }

  std::conditional_t<
      std::is_void_v<To_>,
      Callback<>,
      Callback<To_>>
      start_;
  Callback<std::exception_ptr> fail_;
  Callback<> stop_;

  using Adapted_ = decltype(std::declval<E_>().template k<From_>(
      std::declval<Adaptor>()));

  Adapted_ adapted_;
};

////////////////////////////////////////////////////////////////////////

// Type used to identify when using a 'Task::Failure()' in order to
// properly type-check with 'static_assert()'.
struct _TaskFailure {
  _TaskFailure() = delete;
};

////////////////////////////////////////////////////////////////////////

template <typename T>
using MonostateIfVoidOr =
    std::conditional_t<
        std::is_void_v<T>,
        std::monostate,
        T>;

template <typename T>
using MonostateIfVoidOrReferenceWrapperOr =
    std::conditional_t<
        std::is_void_v<T>,
        std::monostate,
        std::conditional_t<
            !std::is_reference_v<T>,
            T,
            std::reference_wrapper<
                std::remove_reference_t<T>>>>;

////////////////////////////////////////////////////////////////////////

struct _TaskFromToWith {
  // Since we move lambda function at 'Composable' constructor we need to
  // specify the callback that should be triggered on the produced eventual.
  // For this reason we use 'Action'.
  enum class Action {
    Start = 0,
    Stop = 1,
    Fail = 2,
  };

  // Using templated type name to allow using both
  // in 'Composable' and 'Continuation'.
  template <typename From, typename To, typename... Args>
  using DispatchCallback =
      Callback<
          Action,
          std::optional<std::exception_ptr>&&,
          Args&&...,
          // Can't have a 'void' argument type
          // so we are using 'std::monostate'.
          std::optional<
              std::conditional_t<
                  std::is_void_v<From>,
                  std::monostate,
                  From>>&&,
          std::unique_ptr<void, Callback<void*>>&,
          Interrupt&,
          std::conditional_t<
              std::is_void_v<To>,
              Callback<>&&,
              Callback<To>&&>,
          Callback<std::exception_ptr>&&,
          Callback<>&&>;

  template <
      typename K_,
      typename From_,
      typename To_,
      typename... Args_>
  struct Continuation {
    Continuation(
        K_ k,
        std::tuple<Args_...> args,
        std::variant<
            MonostateIfVoidOrReferenceWrapperOr<To_>,
            DispatchCallback<From_, To_, Args_...>>
            value_or_dispatch)
      : args_(std::move(args)),
        value_or_dispatch_(std::move(value_or_dispatch)),
        k_(std::move(k)) {}

    template <typename... From>
    void Start(From&&... from) {
      switch (value_or_dispatch_.index()) {
        case 0:
          if constexpr (!std::is_void_v<To_>) {
            // Need cast to 'To_' to cast to reference type, otherwise
            // it casts to non-reference.
            k_.Start(
                static_cast<To_>(
                    std::move(std::get<0>(value_or_dispatch_))));
          } else {
            k_.Start();
          }
          break;
        case 1:
          if constexpr (std::is_void_v<From_>) {
            Dispatch(Action::Start, std::monostate{});
          } else {
            static_assert(
                sizeof...(from) > 0,
                "Expecting \"from\" argument for 'Task<From, To>' "
                "but no argument passed");
            Dispatch(Action::Start, std::forward<From>(from)...);
          }
          break;
        default:
          LOG(FATAL) << "unreachable";
      }
    }

    template <typename... Args>
    void Fail(Args&&... args) {
      std::exception_ptr exception;

      if constexpr (sizeof...(args) > 0) {
        exception = make_exception_ptr_or_forward(
            std::forward<decltype(args)>(args)...);
      } else {
        exception = std::make_exception_ptr(
            std::runtime_error("empty error"));
      }

      Dispatch(Action::Fail, std::nullopt, std::move(exception));
    }

    void Stop() {
      Dispatch(Action::Stop);
    }

    void Register(Interrupt& interrupt) {
      interrupt_ = &interrupt;
      k_.Register(interrupt);
    }

    void Dispatch(
        Action action,
        std::optional<MonostateIfVoidOr<From_>>&& from = std::nullopt,
        std::optional<std::exception_ptr>&& exception = std::nullopt) {
      CHECK_EQ(value_or_dispatch_.index(), 1u);

      std::apply(
          [&](auto&&... args) {
            std::get<1>(value_or_dispatch_)(
                action,
                std::move(exception),
                std::forward<decltype(args)>(args)...,
                std::forward<decltype(from)>(from),
                e_,
                *interrupt_,
                [this](auto&&... args) {
                  k_.Start(std::forward<decltype(args)>(args)...);
                },
                [this](std::exception_ptr e) {
                  k_.Fail(std::move(e));
                },
                [this]() {
                  k_.Stop();
                });
          },
          std::move(args_));
    }

    std::tuple<Args_...> args_;

    // The 'dispatch_' is a `std::variant` because its either a function that
    // creates an eventual or the value from 'Task::Success' that should be
    // passed on to the continuation.
    std::variant<
        MonostateIfVoidOrReferenceWrapperOr<To_>,
        DispatchCallback<From_, To_, Args_...>>
        value_or_dispatch_;

    std::unique_ptr<void, Callback<void*>> e_;
    Interrupt* interrupt_ = nullptr;

    // NOTE: we store 'k_' as the _last_ member so it will be
    // destructed _first_ and thus we won't have any use-after-delete
    // issues during destruction of 'k_' if it holds any references or
    // pointers to any (or within any) of the above members.
    K_ k_;
  };

  template <
      typename From_,
      typename To_,
      typename... Args_>
  struct Composable {
    template <typename>
    using ValueFrom = To_;

    Composable(MonostateIfVoidOrReferenceWrapperOr<To_> value)
      : value_or_dispatch_(std::move(value)) {}

    template <typename F>
    Composable(Args_... args, F f)
      : args_(std::tuple<Args_...>(std::move(args)...)) {
      static_assert(
          std::tuple_size<decltype(args_)>{} > 0 || std::is_invocable_v<F>,
          "'Task' expects a callable that takes no arguments");

      static_assert(
          std::tuple_size<decltype(args_)>{}
              || std::is_invocable_v<F, Args_...>,
          "'Task' expects a callable that "
          "takes the arguments specified");

      static_assert(
          sizeof(f) <= sizeof(void*),
          "'Task' expects a callable that "
          "can be captured in a 'Callback'");

      using E = decltype(std::apply(f, args_));

      using Value = typename E::template ValueFrom<From_>;

      static_assert(
          std::disjunction_v<
              std::is_same<Value, _TaskFailure>,
              std::is_convertible<Value, To_>>,
          "eventual result type can not be converted into type of 'Task'");

      value_or_dispatch_ = [f = std::move(f)](
                               Action action,
                               std::optional<std::exception_ptr>&& exception,
                               Args_&&... args,
                               std::optional<MonostateIfVoidOr<From_>>&& arg,
                               std::unique_ptr<void, Callback<void*>>& e_,
                               Interrupt& interrupt,
                               std::conditional_t<
                                   std::is_void_v<To_>,
                                   Callback<>&&,
                                   Callback<To_>&&> start,
                               Callback<std::exception_ptr>&& fail,
                               Callback<>&& stop) mutable {
        if (!e_) {
          e_ = std::unique_ptr<void, Callback<void*>>(
              new HeapTask<E, From_, To_>(f(std::move(args)...)),
              [](void* e) {
                delete static_cast<HeapTask<E, From_, To_>*>(e);
              });
        }

        auto* e = static_cast<HeapTask<E, From_, To_>*>(e_.get());

        switch (action) {
          case Action::Start:
            e->Start(
                std::move(arg.value()),
                interrupt,
                std::move(start),
                std::move(fail),
                std::move(stop));
            break;
          case Action::Fail:
            e->Fail(
                interrupt,
                std::move(exception.value()),
                std::move(start),
                std::move(fail),
                std::move(stop));
            break;
          case Action::Stop:
            e->Stop(
                interrupt,
                std::move(start),
                std::move(fail),
                std::move(stop));
            break;
          default:
            LOG(FATAL) << "unreachable";
        }
      };
    }

    template <typename Arg, typename K>
    auto k(K k) && {
      return Continuation<K, From_, To_, Args_...>(
          std::move(k),
          std::move(args_),
          std::move(value_or_dispatch_.value()));
    }

    // See comment in `Continuation` for explanation of `dispatch_` member.
    // Using 'std::optional' because of implicitly deleted 'std::variant'
    // constructor.
    std::optional<
        std::variant<
            MonostateIfVoidOrReferenceWrapperOr<To_>,
            DispatchCallback<From_, To_, Args_...>>>
        value_or_dispatch_;

    std::tuple<Args_...> args_;
  };
};

////////////////////////////////////////////////////////////////////////

// A task can act BOTH as a composable or a continuation that can be
// started via 'TaskFromToWith::Start()'. If used as a continuation
// then it can't be moved after starting, just like all other
// continuations.
template <
    typename From_,
    typename To_,
    typename... Args_>
class TaskFromToWith {
 public:
  template <typename Arg>
  using ValueFrom = To_;

  TaskFromToWith(MonostateIfVoidOrReferenceWrapperOr<To_> value)
    : e_(std::move(value)) {}

  template <typename F>
  TaskFromToWith(Args_... args, F f)
    : e_(std::move(args)..., std::move(f)) {}

  TaskFromToWith(TaskFromToWith&& that)
    : e_(std::move(that.e_)) {
    CHECK(!k_.has_value()) << "moving after starting";
  }

  template <typename Arg, typename K>
  auto k(K k) && {
    return std::move(e_).template k<Arg>(std::move(k));
  }

  void Start(
      Interrupt& interrupt,
      std::conditional_t<
          std::is_void_v<To_>,
          Callback<>&&,
          Callback<To_>&&> start,
      Callback<std::exception_ptr>&& fail,
      Callback<>&& stop) {
    k_.emplace(Build(
        std::move(e_)
        | Terminal()
              .start(std::move(start))
              .fail(std::move(fail))
              .stop(std::move(stop))));

    k_->Register(interrupt);

    k_->Start();
  }

  template <typename Arg>
  void Fail(
      Arg&& arg,
      Interrupt& interrupt,
      std::conditional_t<
          std::is_void_v<To_>,
          Callback<>&&,
          Callback<To_>&&> start,
      Callback<std::exception_ptr>&& fail,
      Callback<>&& stop) {
    k_.emplace(Build(
        std::move(e_)
        | Terminal()
              .start(std::move(start))
              .fail(std::move(fail))
              .stop(std::move(stop))));

    k_->Register(interrupt);

    k_->Fail(std::forward<Arg>(arg));
  }

  void Stop(
      Interrupt& interrupt,
      std::conditional_t<
          std::is_void_v<To_>,
          Callback<>&&,
          Callback<To_>&&> start,
      Callback<std::exception_ptr>&& fail,
      Callback<>&& stop) {
    k_.emplace(Build(
        std::move(e_)
        | Terminal()
              .start(std::move(start))
              .fail(std::move(fail))
              .stop(std::move(stop))));

    k_->Register(interrupt);

    k_->Stop();
  }

  // NOTE: should only be used in tests!
  auto operator*() && {
    auto [future, k] = Terminate(std::move(e_));

    k.Start();

    return future.get();
  }

 private:
  _TaskFromToWith::Composable<From_, To_, Args_...> e_;

  // NOTE: if 'Task::Start()' is invoked then 'Task' becomes not just
  // a composable but also a continuation which has a terminal made up
  // of the callbacks passed to 'Task::Start()'.
  using K_ = decltype(Build(
      std::move(e_)
      | Terminal()
            .start(std::declval<
                   std::conditional_t<
                       std::is_void_v<To_>,
                       Callback<>&&,
                       Callback<To_>&&>>())
            .fail(std::declval<Callback<std::exception_ptr>&&>())
            .stop(std::declval<Callback<>&&>())));

  // NOTE: we store 'k_' as the _last_ member so it will be
  // destructed _first_ and thus we won't have any use-after-delete
  // issues during destruction of 'k_' if it holds any references or
  // pointers to any (or within any) of the above members.
  std::optional<K_> k_;
};

////////////////////////////////////////////////////////////////////////

struct Task {
  template <typename From_>
  struct From : public TaskFromToWith<From_, void> {
    template <typename To_>
    struct To : public TaskFromToWith<From_, To_> {
      template <typename... Args_>
      using With = TaskFromToWith<From_, To_, Args_...>;

      To(MonostateIfVoidOr<To_> value)
        : TaskFromToWith<From_, To_>(std::move(value)) {}

      template <typename F>
      To(F f)
        : TaskFromToWith<From_, To_>(std::move(f)) {}
    };

    template <typename... Args_>
    using With = TaskFromToWith<From_, void, Args_...>;

    template <typename F>
    From(F f)
      : TaskFromToWith<From_, void>(std::move(f)) {}
  };

  template <typename To_>
  using Of = From<void>::To<To_>;

  template <typename... Args_>
  using With = From<void>::To<void>::With<Args_...>;

  // Helpers for synchronous tasks.
  template <typename Value>
  static auto Success(Value value) {
    return TaskFromToWith<void, Value>(std::move(value));
  }

  template <typename Value>
  static auto Success(std::reference_wrapper<Value> value) {
    return TaskFromToWith<void, Value&>(std::move(value));
  }

  static auto Success() {
    return TaskFromToWith<void, void>(std::monostate{});
  }

  template <typename Error>
  static auto Failure(Error error) {
    // TODO(benh): optimize away heap allocation.
    // If we store an error using 'std::exception_ptr' it is also a memory
    // allocation, otherwise we need to store one more template parameter
    // for the 'Error' type.
    return [error = std::make_unique<Error>(std::move(error))]() mutable {
      return Eventual<_TaskFailure>()
          .start([&](auto& k) mutable {
            k.Fail(Error(std::move(*error)));
          });
    };
  }
};

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
