#pragma once

#include <functional> // For 'std::reference_wrapper'.
#include <memory> // For 'std::unique_ptr'.
#include <optional>
#include <tuple>
#include <variant>

#include "eventuals/eventual.h"
#include "eventuals/finally.h"
#include "eventuals/just.h"
#include "eventuals/raise.h"
#include "eventuals/terminal.h"
#include "eventuals/type-traits.h"
#include "stout/stringify.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

template <typename>
struct VariantErrorsHelper;

template <typename... Errors>
struct VariantErrorsHelper<std::variant<std::monostate, Errors...>> {
  using type = std::conditional_t<
      sizeof...(Errors) == 0,
      void,
      std::variant<Errors...>>;
};

////////////////////////////////////////////////////////////////////////

template <typename To>
using TaskStartCallback = Callback<function_type_t<void, To>>;

template <typename Raises>
using TaskFailCallback = Callback<function_type_t<
    void,
    get_rvalue_type_or_void_t<
        typename VariantErrorsHelper<
            variant_of_type_and_tuple_t<
                std::monostate,
                Raises>>::type>>>;

using TaskStopCallback = Callback<void()>;

////////////////////////////////////////////////////////////////////////

template <
    typename E_,
    typename From_,
    typename To_,
    typename Catches_,
    typename Raises_>
struct HeapTask final {
  struct Adaptor final {
    Adaptor(
        TaskStartCallback<To_>* start,
        TaskFailCallback<Raises_>* fail,
        TaskStopCallback* stop)
      : start_(start),
        fail_(fail),
        stop_(stop) {}

    template <typename... Args>
    void Start(Args&&... args) {
      (*start_)(std::forward<Args>(args)...);
    }

    template <typename Error>
    void Fail(Error&& error) {
      (*fail_)(std::move(error));
    }

    void Stop() {
      (*stop_)();
    }

    void Register(Interrupt&) {}

    TaskStartCallback<To_>* start_;
    TaskFailCallback<Raises_>* fail_;
    TaskStopCallback* stop_;
  };

  HeapTask(E_ e)
    : adapted_(
        std::move(e).template k<From_, Catches_>(
            Adaptor{&start_, &fail_, &stop_})) {}

  void Start(
      std::conditional_t<
          std::is_void_v<From_>,
          std::monostate,
          From_>&& arg,
      Interrupt& interrupt,
      TaskStartCallback<To_>&& start,
      TaskFailCallback<Raises_>&& fail,
      TaskStopCallback&& stop) {
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
      std::conditional_t<
          std::tuple_size_v<Catches_>,
          apply_tuple_types_t<std::variant, Catches_>,
          std::monostate>&& error,
      TaskStartCallback<To_>&& start,
      TaskFailCallback<Raises_>&& fail,
      TaskStopCallback&& stop) {
    start_ = std::move(start);
    fail_ = std::move(fail);
    stop_ = std::move(stop);

    // TODO(benh): clarify the semantics of whether or not calling
    // 'Register()' more than once is well-defined.
    adapted_.Register(interrupt);

    std::visit(
        [this](auto&& error) {
          adapted_.Fail(std::move(error));
        },
        std::move(error));
  }

  void Stop(
      Interrupt& interrupt,
      TaskStartCallback<To_>&& start,
      TaskFailCallback<Raises_>&& fail,
      TaskStopCallback&& stop) {
    start_ = std::move(start);
    fail_ = std::move(fail);
    stop_ = std::move(stop);

    // TODO(benh): clarify the semantics of whether or not calling
    // 'Register()' more than once is well-defined.
    adapted_.Register(interrupt);

    adapted_.Stop();
  }

  TaskStartCallback<To_> start_;
  TaskFailCallback<Raises_> fail_;
  TaskStopCallback stop_;

  using Adapted_ = decltype(std::declval<E_>().template k<From_, Catches_>(
      std::declval<Adaptor>()));

  Adapted_ adapted_;
};

////////////////////////////////////////////////////////////////////////

// Type used to identify when using a 'Task::Failure()' in order to
// properly type-check with 'static_assert()'.
struct _TaskFailure final {
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

struct _TaskFromToWith final {
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
  template <
      typename From,
      typename To,
      typename Catches,
      typename Raises,
      typename... Args>
  using DispatchCallback =
      Callback<void(
          Action,
          std::optional<
              std::conditional_t<
                  std::tuple_size_v<Catches>,
                  apply_tuple_types_t<std::variant, Catches>,
                  std::monostate>>&&,
          Args&...,
          // Can't have a 'void' argument type
          // so we are using 'std::monostate'.
          std::optional<
              std::conditional_t<
                  std::is_void_v<From>,
                  std::monostate,
                  From>>&&,
          std::unique_ptr<void, Callback<void(void*)>>&,
          Interrupt&,
          TaskStartCallback<To>&&,
          TaskFailCallback<Raises>&&,
          TaskStopCallback&&)>;

  template <
      typename K_,
      typename From_,
      typename To_,
      typename Catches_,
      typename Raises_,
      typename... Args_>
  struct Continuation final {
    Continuation(
        K_ k,
        std::tuple<Args_...>&& args,
        std::variant<
            MonostateIfVoidOrReferenceWrapperOr<To_>,
            DispatchCallback<From_, To_, Catches_, Raises_, Args_...>>&&
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

    template <typename Error>
    void Fail(Error&& error) {
      // NOTE: we only propagate an upstream error into our type-erased
      // eventual if the eventual catches the error, otherwise we skip it
      // all together.
      if constexpr (tuple_contains_exact_type_v<Error, Catches_>) {
        Dispatch(Action::Fail, std::nullopt, std::forward<Error>(error));
      } else {
        k_.Fail(std::forward<Error>(error));
      }
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
        std::optional<
            std::conditional_t<
                std::tuple_size_v<Catches_>,
                apply_tuple_types_t<std::variant, Catches_>,
                std::monostate>>&& error = std::nullopt) {
      CHECK_EQ(value_or_dispatch_.index(), 1u);

      std::apply(
          [&](auto&... args) {
            std::get<1>(value_or_dispatch_)(
                action,
                std::move(error),
                args...,
                std::forward<decltype(from)>(from),
                e_,
                *interrupt_,
                [this](auto&&... args) {
                  k_.Start(std::forward<decltype(args)>(args)...);
                },
                [this](auto&&... void_or_errors) {
                  // k_.Fail(std::forward<decltype(error)>(error));
                  if constexpr (sizeof...(void_or_errors)) {
                    std::visit(
                        [this](auto&& error) {
                          k_.Fail(std::forward<decltype(error)>(error));
                        },
                        std::forward<
                            decltype(void_or_errors)>(void_or_errors)...);
                  } else {
                    CHECK(std::tuple_size_v<Raises_> == 0)
                        << "Unreachable at runtime, but compiler tries to "
                           "compile this lambda as a 'Callback' with some "
                           "arguments even if 'Raises_' is empty";
                  }
                },
                [this]() {
                  k_.Stop();
                });
          },
          args_);
    }

    std::tuple<Args_...> args_;

    // The 'dispatch_' is a `std::variant` because its either a function that
    // creates an eventual or the value from 'Task::Success' that should be
    // passed on to the continuation.
    std::variant<
        MonostateIfVoidOrReferenceWrapperOr<To_>,
        DispatchCallback<From_, To_, Catches_, Raises_, Args_...>>
        value_or_dispatch_;

    std::unique_ptr<void, Callback<void(void*)>> e_;
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
      typename Catches_,
      typename Raises_,
      typename... Args_>
  struct Composable final {
    template <typename Arg, typename Errors>
    using ValueFrom = To_;

    template <typename Arg, typename Errors>
    using ErrorsFrom = tuple_types_union_t<
        Raises_,
        tuple_types_subtract_t<Errors, Catches_>>;

    template <typename Downstream>
    static constexpr bool CanCompose = Downstream::ExpectsValue;

    using Expects = SingleValue;

    Composable(MonostateIfVoidOrReferenceWrapperOr<To_> value)
      : value_or_dispatch_(std::move(value)) {}

    template <typename F>
    Composable(Args_... args, F f)
      : args_(std::tuple<Args_...>(std::move(args)...)) {
      constexpr bool HAS_ARGS = sizeof...(Args_) > 0;

      static_assert(
          // NOTE: need to use 'std::conditional_t' here because we
          // need to defer evaluation of 'std::is_invocable' unless
          // necessary.
          std::conditional_t<
              HAS_ARGS,
              std::true_type,
              std::is_invocable<F>>::value,
          "'Task' expects a callable (e.g., a lambda) that takes no arguments");

      static_assert(
          // NOTE: need to use 'std::conditional_t' here because we
          // need to defer evaluation of 'std::is_invocable' unless
          // necessary.
          std::conditional_t<
              !HAS_ARGS,
              std::true_type,
              std::is_invocable<F, Args_&...>>::value,
          "'Task' expects a callable (e.g., a lambda) that "
          "takes the arguments specified");

      static_assert(
          sizeof(f) <= SIZEOF_CALLBACK,
          "'Task' expects a callable (e.g., a lambda) that "
          "can be captured in a 'Callback'");

      using E = decltype(std::apply(f, args_));

      static_assert(
          std::is_void_v<E> || HasValueFrom<E>::value,
          "'Task' expects a callable (e.g., a lambda) that "
          "returns an eventual but you're returning a value");

      static_assert(
          !std::is_void_v<E>,
          "'Task' expects a callable (e.g., a lambda) that "
          "returns an eventual but you're not returning anything");

      using ErrorsFromE = typename E::template ErrorsFrom<From_, Catches_>;

      static_assert(
          tuple_types_subset_subtype_v<ErrorsFromE, Raises_>,
          "Specified errors can't be raised within 'Task'");

      using Value = typename E::template ValueFrom<From_, Catches_>;

      static_assert(
          std::disjunction_v<
              std::is_same<Value, _TaskFailure>,
              std::is_convertible<Value, To_>>,
          "eventual result type can not be converted into type of 'Task'");

      value_or_dispatch_ = [f = std::move(f)](
                               Action action,
                               std::optional<
                                   std::conditional_t<
                                       std::tuple_size_v<Catches_>,
                                       apply_tuple_types_t<std::variant, Catches_>,
                                       std::monostate>>&& error,
                               Args_&... args,
                               std::optional<MonostateIfVoidOr<From_>>&& arg,
                               std::unique_ptr<void, Callback<void(void*)>>& e_,
                               Interrupt& interrupt,
                               TaskStartCallback<To_>&& start,
                               TaskFailCallback<Raises_>&& fail,
                               TaskStopCallback&& stop) mutable {
        if (!e_) {
          e_ = std::unique_ptr<void, Callback<void(void*)>>(
              new HeapTask<
                  E,
                  From_,
                  To_,
                  Catches_,
                  Raises_>(f(args...)),
              [](void* e) {
                delete static_cast<
                    HeapTask<
                        E,
                        From_,
                        To_,
                        Catches_,
                        Raises_>*>(e);
              });
        }

        auto* e = static_cast<
            HeapTask<
                E,
                From_,
                To_,
                Catches_,
                Raises_>*>(e_.get());

        switch (action) {
          case Action::Start:
            CHECK(arg);
            e->Start(
                std::move(arg.value()),
                interrupt,
                std::move(start),
                std::move(fail),
                std::move(stop));
            break;
          case Action::Fail: {
            // If 'Catches_' is empty then we will never dispatch
            // with an action of 'Action::Fail' but since 'action' is a runtime
            // value the compiler will assume that it's possible that we can
            // call 'e->Fail()', with what ever type we have for 'error' so to
            // keep the compiler from trying to compile that code path we need
            // to add the following 'if constexpr'.
            if constexpr (std::tuple_size_v<Catches_> != 0) {
              CHECK(error);
              e->Fail(
                  interrupt,
                  std::move(error.value()),
                  std::move(start),
                  std::move(fail),
                  std::move(stop));
            }
            break;
          }
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

    Composable(
        std::optional<
            std::variant<
                MonostateIfVoidOrReferenceWrapperOr<To_>,
                DispatchCallback<From_, To_, Catches_, Raises_, Args_...>>>&&
            value_or_dispatch,
        std::tuple<Args_...>&& args)
      : value_or_dispatch_(std::move(value_or_dispatch)),
        args_(std::move(args)) {}

    template <typename Arg, typename Errors, typename K>
    auto k(K k) && {
      static_assert(
          !std::disjunction_v<IsUndefined<From_>, IsUndefined<To_>>,
          "'Task' 'From' or 'To' type is not specified");

      return Continuation<K, From_, To_, Catches_, Raises_, Args_...>(
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
            DispatchCallback<From_, To_, Catches_, Raises_, Args_...>>>
        value_or_dispatch_;

    std::tuple<Args_...> args_;
  };
};

////////////////////////////////////////////////////////////////////////

// A task can act BOTH as a composable or a continuation that can be
// started via '_Task::Start()'. If used as a continuation
// then it can't be moved after starting, just like all other
// continuations.
template <
    typename From_,
    typename To_,
    typename Catches_,
    typename Raises_,
    typename... Args_>
class _Task final {
 public:
  template <typename Arg, typename Errors>
  using ValueFrom = To_;

  template <typename Arg, typename Errors>
  using ErrorsFrom = tuple_types_union_t<
      Raises_,
      tuple_types_subtract_t<Errors, Catches_>>;

  template <typename Downstream>
  static constexpr bool CanCompose = Downstream::ExpectsValue;

  using Expects = SingleValue;

  template <typename T>
  using From = std::enable_if_t<
      IsUndefined<From_>::value,
      _Task<T, To_, Catches_, Raises_, Args_...>>;

  template <typename T>
  using To = std::enable_if_t<
      IsUndefined<To_>::value,
      _Task<From_, T, Catches_, Raises_, Args_...>>;

  template <typename... Errors>
  using Catches = std::enable_if_t<
      std::tuple_size_v<Catches_> == 0,
      _Task<From_, To_, std::tuple<Errors...>, Raises_, Args_...>>;

  template <typename... Errors>
  using Raises = std::enable_if_t<
      std::tuple_size_v<Raises_> == 0,
      _Task<From_, To_, Catches_, std::tuple<Errors...>, Args_...>>;

  template <typename... Args>
  using With = std::enable_if_t<
      sizeof...(Args_) == 0,
      _Task<From_, To_, Catches_, Raises_, Args...>>;

  template <typename T>
  using Of = std::enable_if_t<
      std::conjunction_v<IsUndefined<From_>, IsUndefined<To_>>,
      _Task<void, T, Catches_, Raises_, Args_...>>;

  template <typename F>
  _Task(Args_... args, F f)
    : e_(std::move(args)..., std::move(f)) {}

  _Task(MonostateIfVoidOrReferenceWrapperOr<To_> value)
    : e_(std::move(value)) {}

  _Task(_Task&& that) noexcept
    : e_(std::move(that.e_)) {
    CHECK(!that.k_.has_value()) << "moving after starting";
  }

  ~_Task() = default;

  template <typename Arg, typename Errors, typename K>
  auto k(K k) && {
    static_assert(
        !std::disjunction_v<IsUndefined<From_>, IsUndefined<To_>>,
        "'Task' 'From' or 'To' type is not specified");

    return std::move(e_).template k<Arg, Errors>(
        std::move(k));
  }

  // Treat this task as a continuation and "start" it. Every task gets
  // its own 'Scheduler::Context' that will begin executing with the
  // default scheduler, aka, the preemptive scheduler.
  //
  // NOTE: the callbacks, 'start', 'fail', and 'stop' will _not_ be
  // invoked with the same 'Scheduler::Context' as the rest of the
  // task, they will be invoked with whatever 'Scheduler::Context' was
  // used to call 'Start()'.
  void Start(
      std::string&& name,
      TaskStartCallback<To_>&& start,
      TaskFailCallback<Raises_>&& fail,
      TaskStopCallback&& stop) {
    CHECK(!context_.has_value()) << "Task already started";

    context_.emplace(Scheduler::Default(), std::move(name));

    k_.emplace(Build<void, Catches_>(
        Reschedule(context_->Borrow())
        >> std::move(e_)
        >> Terminal()
               .start(std::move(start))
               .fail(std::move(fail))
               .stop(std::move(stop))));

    k_->Register(interrupt_);

    k_->Start();
  }

  // Overloaded 'Start()' that returns a 'std::future' instead of
  // taking callbacks.
  auto Start(std::string&& name) {
    promise_.emplace();

    auto future = promise_->get_future();

    Start(
        std::move(name),
        // NOTE: not borrowing 'this' because callback is stored/used
        // from within 'this' and borrowing could create a cycle.
        [this](auto&&... value) {
          static_assert(
              sizeof...(value) == 0 || sizeof...(value) == 1,
              "Task only supports 0 or 1 value, but found > 1");

          CHECK(promise_.has_value());
          promise_->set_value(std::forward<decltype(value)>(value)...);
        },
        // NOTE: not borrowing 'this' because callback is stored/used
        // from within 'this' and borrowing could create a cycle.
        [this](auto&&... void_or_errors) {
          if constexpr (sizeof...(void_or_errors)) {
            CHECK(promise_.has_value());
            std::visit(
                [this](auto&& error) {
                  static_assert(
                      !std::is_same_v<
                          std::decay_t<decltype(error)>,
                          std::exception_ptr>,
                      "Not expecting a 'std::exception_ptr' to "
                      "propagate through a task");

                  promise_->set_exception(
                      std::make_exception_ptr(
                          std::forward<decltype(error)>(error)));
                },
                std::forward<decltype(void_or_errors)>(void_or_errors)...);
          }
        },
        // NOTE: not borrowing 'this' because callback is stored/used
        // from within 'this' and borrowing could create a cycle.
        [this]() {
          CHECK(promise_.has_value());
          promise_->set_exception(
              std::make_exception_ptr(
                  Stopped()));
        });

    return future;
  }

  // Treat this task as a continuation and "fail" it. Every task gets
  // its own 'Scheduler::Context' that will begin executing with the
  // default scheduler, aka, the preemptive scheduler.
  //
  // NOTE: the callbacks, 'start', 'fail', and 'stop' will _not_ be
  // invoked with the same 'Scheduler::Context' as the rest of the
  // task, they will be invoked with whatever 'Scheduler::Context' was
  // used to call 'Fail()'.
  //
  // TODO(benh): provide overloaded versions of 'Fail()' similar to
  // 'Start()' that return 'std::future'.
  template <typename Error>
  void Fail(
      std::string&& name,
      Error&& error,
      TaskStartCallback<To_>&& start,
      TaskFailCallback<Raises_>&& fail,
      TaskStopCallback&& stop) {
    static_assert(
        std::is_base_of_v<std::exception, std::decay_t<Error>>,
        "Expecting a type derived from std::exception");

    static_assert(
        std::tuple_size_v<Catches_>,
        "You can't 'Fail' a task without specifying 'Catches'");

    CHECK(!context_.has_value()) << "Task already started";

    context_.emplace(Scheduler::Default(), std::move(name));

    k_.emplace(Build<void, Catches_>(
        Reschedule(context_->Borrow())
        >> std::move(e_)
        >> Terminal()
               .start(std::move(start))
               .fail(std::move(fail))
               .stop(std::move(stop))));

    k_->Register(interrupt_);

    k_->Fail(std::forward<Error>(error));
  }

  // Treat this task as a continuation and "stop" it. Every task gets
  // its own 'Scheduler::Context' that will begin executing with the
  // default scheduler, aka, the preemptive scheduler.
  //
  // NOTE: the callbacks, 'start', 'fail', and 'stop' will _not_ be
  // invoked with the same 'Scheduler::Context' as the rest of the
  // task, they will be invoked with whatever 'Scheduler::Context' was
  // used to call 'Stop()'.
  //
  // TODO(benh): provide overloaded versions of 'Stop()' similar to
  // 'Start()' that return 'std::future'.
  void Stop(
      std::string&& name,
      TaskStartCallback<To_>&& start,
      TaskFailCallback<Raises_>&& fail,
      TaskStopCallback&& stop) {
    CHECK(!context_.has_value()) << "Task already started";

    context_.emplace(Scheduler::Default(), std::move(name));

    k_.emplace(Build<void, Catches_>(
        Reschedule(context_->Borrow())
        >> std::move(e_)
        >> Terminal()
               .start(std::move(start))
               .fail(std::move(fail))
               .stop(std::move(stop))));

    k_->Register(interrupt_);

    k_->Stop();
  }

  void Interrupt() {
    CHECK(context_.has_value()) << "Task not interruptible";
    interrupt_.Trigger();
  }

  // NOTE: THIS IS BLOCKING! CONSIDER YOURSELF WARNED!
  auto operator*() && {
    try {
      auto future = Start(
          // NOTE: using the current thread id in order to constuct a task
          // name because the thread blocks so this name should be unique!
          "[thread "
          + stringify(std::this_thread::get_id())
          + " blocking on dereference]");

      return future.get();
    } catch (const std::exception& e) {
      LOG(WARNING)
          << "WARNING: exception thrown while dereferencing eventual: "
          << e.what();
      throw;
    } catch (...) {
      LOG(WARNING)
          << "WARNING: exception thrown while dereferencing eventual";
      throw;
    }
  }

  // Helpers for synchronous tasks.

  template <typename Value>
  [[nodiscard]] static auto Success(Value value) {
    return _Task<
        void,
        Value,
        Catches_,
        Raises_>(std::move(value));
  }

  template <typename Value>
  [[nodiscard]] static auto Success(std::reference_wrapper<Value> value) {
    return _Task<
        void,
        Value&,
        Catches_,
        Raises_>(std::move(value));
  }

  [[nodiscard]] static auto Success() {
    return _Task<
        void,
        void,
        Catches_,
        Raises_>(std::monostate{});
  }

  template <typename Error>
  [[nodiscard]] static auto Failure(Error error) {
    static_assert(
        std::is_base_of_v<std::exception, std::decay_t<Error>>,
        "Expecting a type derived from std::exception");

    // TODO(benh): optimize away heap allocation.
    // If we store an error using 'std::exception_ptr' it is also a memory
    // allocation, otherwise we need to store one more template parameter
    // for the 'Error' type.
    return [error = std::make_unique<Error>(std::move(error))]() mutable {
      return Eventual<_TaskFailure>()
          .raises<Error>()
          .start([&](auto& k) mutable {
            k.Fail(Error(std::move(*error)));
          });
    };
  }

  [[nodiscard]] static auto Failure(const std::string& s) {
    return Failure(std::runtime_error(s));
  }

  [[nodiscard]] static auto Failure(char* s) {
    return Failure(std::runtime_error(s));
  }

  [[nodiscard]] static auto Failure(const char* s) {
    return Failure(std::runtime_error(s));
  }

 private:
  // To make possible constructing from another '_Task' with
  // the different 'std::tuple' of error types.
  template <typename, typename, typename, typename, typename...>
  friend class _Task;

  std::conditional_t<
      std::disjunction_v<IsUndefined<From_>, IsUndefined<To_>>,
      decltype(Eventual<Undefined>()),
      _TaskFromToWith::Composable<From_, To_, Catches_, Raises_, Args_...>>
      e_;

  // Optional promise if we are invoked as a continuation without any
  // callbacks.
  std::optional<
      std::promise<
          typename ReferenceWrapperTypeExtractor<To_>::type>>
      promise_;

  // Possible interrupt to use if we are invoked as a continuation.
  class Interrupt interrupt_;

  // Possible context to use if we are invoked as a continuation.
  std::optional<Scheduler::Context> context_;

  // NOTE: if 'Task::Start()' is invoked then 'Task' becomes not just
  // a composable but also a continuation which has a terminal made up
  // of the callbacks passed to 'Task::Start()'.

  using K_ = std::conditional_t<
      std::disjunction_v<IsUndefined<From_>, IsUndefined<To_>>,
      Undefined,
      decltype(Build<void, Catches_>(
          Reschedule(context_->Borrow())
          >> std::move(e_)
          >> Terminal()
                 .start(std::declval<TaskStartCallback<To_>&&>())
                 .fail(std::declval<TaskFailCallback<Raises_>&&>())
                 .stop(std::declval<TaskStopCallback&&>())))>;

  // NOTE: we store 'k_' as the _last_ member so it will be
  // destructed _first_ and thus we won't have any use-after-delete
  // issues during destruction of 'k_' if it holds any references or
  // pointers to any (or within any) of the above members.
  std::optional<K_> k_;
};

using Task = _Task<Undefined, Undefined, std::tuple<>, std::tuple<>>;

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
