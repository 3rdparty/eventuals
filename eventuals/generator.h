#pragma once

#include <memory>
#include <optional>
#include <tuple>
#include <variant>

#include "eventuals/stream.h"
#include "eventuals/terminal.h"
#include "eventuals/then.h"
#include "eventuals/type-traits.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

template <typename E_, typename From_, typename To_>
struct HeapGenerator final {
  struct Adaptor final {
    Adaptor(
        Callback<void(TypeErasedStream&)>* begin,
        Callback<void(std::exception_ptr)>* fail,
        Callback<void()>* stop,
        Callback<function_type_t<void, To_>>* body,
        Callback<void()>* ended)
      : begin_(begin),
        fail_(fail),
        stop_(stop),
        body_(body),
        ended_(ended) {}

    // All functions are called as Continuation after produced Stream.
    void Begin(TypeErasedStream& stream) {
      (*begin_)(stream);
    }

    template <typename Error>
    void Fail(Error&& error) {
      (*fail_)(
          make_exception_ptr_or_forward(
              std::forward<Error>(error)));
    }

    // NOTE: overload so we don't create nested std::exception_ptr.
    void Fail(std::exception_ptr exception) {
      (*fail_)(std::move(exception));
    }

    void Stop() {
      (*stop_)();
    }

    template <typename... Args>
    void Body(Args&&... args) {
      (*body_)(std::forward<decltype(args)>(args)...);
    }

    void Ended() {
      (*ended_)();
    }

    // Already registered in 'adapted_'
    void Register(Interrupt&) {}

    Callback<void(TypeErasedStream&)>* begin_;
    Callback<void(std::exception_ptr)>* fail_;
    Callback<void()>* stop_;
    Callback<function_type_t<void, To_>>* body_;
    Callback<void()>* ended_;
  };

  HeapGenerator(E_ e)
    : adapted_(
        std::move(e).template k<From_>(
            Adaptor{&begin_, &fail_, &stop_, &body_, &ended_})) {}

  void Start(
      Interrupt& interrupt,
      std::conditional_t<
          std::is_void_v<From_>,
          std::monostate,
          From_>&& arg,
      Callback<void(TypeErasedStream&)>&& begin,
      Callback<void(std::exception_ptr)>&& fail,
      Callback<void()>&& stop,
      Callback<function_type_t<void, To_>>&& body,
      Callback<void()>&& ended) {
    begin_ = std::move(begin);
    fail_ = std::move(fail);
    stop_ = std::move(stop);
    body_ = std::move(body);
    ended_ = std::move(ended);

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
      std::exception_ptr&& fail_exception,
      Callback<void(TypeErasedStream&)>&& begin,
      Callback<void(std::exception_ptr)>&& fail,
      Callback<void()>&& stop,
      Callback<function_type_t<void, To_>>&& body,
      Callback<void()>&& ended) {
    begin_ = std::move(begin);
    fail_ = std::move(fail);
    stop_ = std::move(stop);
    body_ = std::move(body);
    ended_ = std::move(ended);

    // TODO(benh): clarify the semantics of whether or not calling
    // 'Register()' more than once is well-defined.
    adapted_.Register(interrupt);

    adapted_.Fail(std::move(fail_exception));
  }

  void Stop(
      Interrupt& interrupt,
      Callback<void(TypeErasedStream&)>&& begin,
      Callback<void(std::exception_ptr)>&& fail,
      Callback<void()>&& stop,
      Callback<function_type_t<void, To_>>&& body,
      Callback<void()>&& ended) {
    begin_ = std::move(begin);
    fail_ = std::move(fail);
    stop_ = std::move(stop);
    body_ = std::move(body);
    ended_ = std::move(ended);

    // TODO(benh): clarify the semantics of whether or not calling
    // 'Register()' more than once is well-defined.
    adapted_.Register(interrupt);

    adapted_.Stop();
  }

  Callback<void(TypeErasedStream&)> begin_;
  Callback<void(std::exception_ptr)> fail_;
  Callback<void()> stop_;
  Callback<function_type_t<void, To_>> body_;
  Callback<void()> ended_;

  using Adapted_ = decltype(std::declval<E_>().template k<From_>(
      std::declval<Adaptor>()));

  Adapted_ adapted_;
};

////////////////////////////////////////////////////////////////////////

struct _Generator final {
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
      Callback<void(
          Action,
          std::optional<std::exception_ptr>&&,
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
          Callback<void(TypeErasedStream&)>&&,
          Callback<void(std::exception_ptr)>&&,
          Callback<void()>&&,
          Callback<function_type_t<void, To>>&&,
          Callback<void()>&&)>;

  template <
      typename K_,
      typename From_,
      typename To_,
      typename Errors_,
      typename... Args_>
  struct Continuation final {
    Continuation(
        K_ k,
        std::tuple<Args_...>&& args,
        DispatchCallback<From_, To_, Args_...>&& dispatch)
      : args_(std::move(args)),
        dispatch_(std::move(dispatch)),
        k_(std::move(k)) {}

    // All Continuation functions just trigger dispatch Callback,
    // that stores all callbacks for different events
    // (Start, Stop, Fail, Body, Ended). To specify the function to call
    // use Action enum state.
    template <typename... From>
    void Start(From&&... from) {
      if constexpr (std::is_void_v<From_>) {
        Dispatch(Action::Start, std::monostate{});
      } else {
        static_assert(
            sizeof...(from) > 0,
            "Expecting \"from\" argument for 'Generator<From, To>' "
            "but no argument passed");
        Dispatch(Action::Start, std::forward<From>(from)...);
      }
    }

    template <typename Error>
    void Fail(Error&& error) {
      static_assert(
          std::disjunction_v<
              std::is_same<std::exception_ptr, std::decay_t<Error>>,
              std::is_base_of<std::exception, std::decay_t<Error>>>,
          "Expecting a type derived from std::exception");

      static_assert(
          std::disjunction_v<
              std::is_same<std::exception_ptr, std::decay_t<Error>>,
              tuple_types_contains_subtype<std::decay_t<Error>, Errors_>>,
          "Error is not specified in 'Raises'");

      std::exception_ptr exception;

      exception = make_exception_ptr_or_forward(
          std::forward<Error>(error));

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
        std::optional<
            std::conditional_t<
                std::is_void_v<From_>,
                std::monostate,
                From_>>&& from = std::nullopt,
        std::optional<std::exception_ptr>&& exception = std::nullopt) {
      std::apply(
          [&](auto&... args) {
            dispatch_(
                action,
                std::move(exception),
                args...,
                std::forward<decltype(from)>(from),
                e_,
                *interrupt_,
                [this](TypeErasedStream& stream) {
                  k_.Begin(stream);
                },
                [this](std::exception_ptr e) {
                  k_.Fail(std::move(e));
                },
                [this]() {
                  k_.Stop();
                },
                [this](auto&&... args) {
                  k_.Body(std::forward<decltype(args)>(args)...);
                },
                [this]() {
                  k_.Ended();
                });
          },
          args_);
    }

    std::tuple<Args_...> args_;

    DispatchCallback<From_, To_, Args_...> dispatch_;

    std::unique_ptr<void, Callback<void(void*)>> e_;
    Interrupt* interrupt_ = nullptr;

    // NOTE: we store 'k_' as the _last_ member so it will be
    // destructed _first_ and thus we won't have any use-after-delete
    // issues during destruction of 'k_' if it holds any references or
    // pointers to any (or within any) of the above members.
    K_ k_;
  };

  template <typename From_, typename To_, typename Errors_, typename... Args_>
  struct Composable final {
    template <typename Arg>
    using ValueFrom = To_;

    template <typename Arg, typename Errors>
    using ErrorsFrom = tuple_types_union_t<Errors, Errors_>;

    template <typename T>
    using From = std::enable_if_t<
        IsUndefined<From_>::value,
        Composable<T, To_, Errors_, Args_...>>;

    template <typename T>
    using To = std::enable_if_t<
        IsUndefined<To_>::value,
        Composable<From_, T, Errors_, Args_...>>;

    template <typename... Errors>
    using Raises = std::enable_if_t<
        std::tuple_size_v<Errors_> == 0,
        Composable<From_, To_, std::tuple<Errors...>, Args_...>>;

    template <typename... Args>
    using With = std::enable_if_t<
        sizeof...(Args_) == 0,
        Composable<From_, To_, Errors_, Args...>>;

    template <typename T>
    using Of = std::enable_if_t<
        std::conjunction_v<IsUndefined<From_>, IsUndefined<To_>>,
        Composable<void, T, Errors_, Args_...>>;

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
          "'Generator' expects a callable (e.g., a lambda) "
          "that takes no arguments");

      static_assert(
          // NOTE: need to use 'std::conditional_t' here because we
          // need to defer evaluation of 'std::is_invocable' unless
          // necessary.
          std::conditional_t<
              !HAS_ARGS,
              std::true_type,
              std::is_invocable<F, Args_&...>>::value,
          "'Generator' expects a callable (e.g., a lambda) that "
          "takes the arguments specified");

      static_assert(
          sizeof(f) <= SIZEOF_CALLBACK,
          "'Generator' expects a callable (e.g., a lambda) that "
          "can be captured in a 'Callback'");

      using E = decltype(std::apply(f, args_));

      static_assert(
          std::is_void_v<E> || HasValueFrom<E>::value,
          "'Generator' expects a callable (e.g., a lambda) that "
          "returns an eventual but you're returning a value");

      static_assert(
          !std::is_void_v<E>,
          "'Generator' expects a callable (e.g., a lambda) that "
          "returns an eventual but you're not returning anything");

      using ErrorsFromE = typename E::template ErrorsFrom<From_, std::tuple<>>;

      static_assert(
          tuple_types_subset_subtype_v<ErrorsFromE, Errors_>,
          "Specified errors can't be thrown from 'Generator'");

      using Value = typename E::template ValueFrom<From_>;

      static_assert(
          std::is_convertible_v<Value, To_>,
          "eventual result type can not be converted into type of 'Generator'");

      dispatch_ = [f = std::move(f)](
                      Action action,
                      std::optional<std::exception_ptr>&& exception,
                      Args_&... args,
                      std::optional<
                          std::conditional_t<
                              std::is_void_v<From_>,
                              std::monostate,
                              From_>>&& arg,
                      std::unique_ptr<void, Callback<void(void*)>>& e_,
                      Interrupt& interrupt,
                      Callback<void(TypeErasedStream&)>&& begin,
                      Callback<void(std::exception_ptr)>&& fail,
                      Callback<void()>&& stop,
                      Callback<function_type_t<void, To_>>&& body,
                      Callback<void()>&& ended) mutable {
        if (!e_) {
          e_ = std::unique_ptr<void, Callback<void(void*)>>(
              new HeapGenerator<E, From_, To_>(f(args...)),
              [](void* e) {
                delete static_cast<HeapGenerator<E, From_, To_>*>(e);
              });
        }

        auto* e = static_cast<HeapGenerator<E, From_, To_>*>(e_.get());

        switch (action) {
          case Action::Start:
            e->Start(
                interrupt,
                std::move(arg.value()),
                std::move(begin),
                std::move(fail),
                std::move(stop),
                std::move(body),
                std::move(ended));
            break;
          case Action::Fail:
            e->Fail(
                interrupt,
                std::move(exception.value()),
                std::move(begin),
                std::move(fail),
                std::move(stop),
                std::move(body),
                std::move(ended));
            break;
          case Action::Stop:
            e->Stop(
                interrupt,
                std::move(begin),
                std::move(fail),
                std::move(stop),
                std::move(body),
                std::move(ended));
            break;
          default:
            LOG(FATAL) << "unreachable";
        }
      };
    }

    ~Composable() = default;

    Composable(Composable&& that) noexcept = default;

    template <typename Arg, typename K>
    auto k(K k) && {
      static_assert(
          !std::disjunction_v<IsUndefined<From_>, IsUndefined<To_>>,
          "'Task' 'From' or 'To' type is not specified");

      return Continuation<K, From_, To_, Errors_, Args_...>(
          std::move(k),
          std::move(args_),
          std::move(dispatch_));
    }

    template <typename Downstream>
    static constexpr bool CanCompose = true;

    using Expects = SingleValue;

    std::conditional_t<
        std::disjunction_v<IsUndefined<From_>, IsUndefined<To_>>,
        Undefined,
        DispatchCallback<From_, To_, Args_...>>
        dispatch_;

    std::tuple<Args_...> args_;
  };
};

////////////////////////////////////////////////////////////////////////

// Creating a type alias to improve the readability.
using Generator = _Generator::Composable<
    Undefined,
    Undefined,
    std::tuple<>>;

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
