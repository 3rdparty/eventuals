#pragma once

#include <memory>
#include <optional>
#include <tuple>
#include <variant>

#include "eventuals/finally.h"
#include "eventuals/stream.h"
#include "eventuals/task.h"
#include "eventuals/terminal.h"
#include "eventuals/then.h"
#include "eventuals/type-traits.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

using GeneratorBeginCallback = Callback<void(TypeErasedStream&)>;

template <typename Raises>
using GeneratorFailCallback = Callback<function_type_t<
    void,
    get_rvalue_type_or_void_t<
        typename VariantErrorsHelper<
            variant_of_type_and_tuple_t<
                std::monostate,
                Raises>>::type>>>;

using GeneratorStopCallback = Callback<void()>;

template <typename To>
using GeneratorBodyCallback = Callback<function_type_t<void, To>>;

using GeneratorEndedCallback = Callback<void()>;

////////////////////////////////////////////////////////////////////////

template <
    typename E_,
    typename From_,
    typename To_,
    typename Catches_,
    typename Raises_>
struct HeapGenerator final {
  struct Adaptor final {
    Adaptor(
        GeneratorBeginCallback* begin,
        GeneratorFailCallback<Raises_>* fail,
        GeneratorStopCallback* stop,
        GeneratorBodyCallback<To_>* body,
        GeneratorEndedCallback* ended)
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
      (*fail_)(std::move(error));
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

    GeneratorBeginCallback* begin_;
    GeneratorFailCallback<Raises_>* fail_;
    GeneratorStopCallback* stop_;
    GeneratorBodyCallback<To_>* body_;
    GeneratorEndedCallback* ended_;
  };

  HeapGenerator(E_ e)
    : adapted_(
        std::move(e).template k<From_, Catches_>(
            Adaptor{&begin_, &fail_, &stop_, &body_, &ended_})) {}

  void Start(
      Interrupt& interrupt,
      std::conditional_t<
          std::is_void_v<From_>,
          std::monostate,
          From_>&& arg,
      GeneratorBeginCallback&& begin,
      GeneratorFailCallback<Raises_>&& fail,
      GeneratorStopCallback&& stop,
      GeneratorBodyCallback<To_>&& body,
      GeneratorEndedCallback&& ended) {
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
      typename MonostateIfEmptyOrVariantOf<Catches_>::type&& error,
      GeneratorBeginCallback&& begin,
      GeneratorFailCallback<Raises_>&& fail,
      GeneratorStopCallback&& stop,
      GeneratorBodyCallback<To_>&& body,
      GeneratorEndedCallback&& ended) {
    begin_ = std::move(begin);
    fail_ = std::move(fail);
    stop_ = std::move(stop);
    body_ = std::move(body);
    ended_ = std::move(ended);

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
      GeneratorBeginCallback&& begin,
      GeneratorFailCallback<Raises_>&& fail,
      GeneratorStopCallback&& stop,
      GeneratorBodyCallback<To_>&& body,
      GeneratorEndedCallback&& ended) {
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

  GeneratorBeginCallback begin_;
  GeneratorFailCallback<Raises_> fail_;
  GeneratorStopCallback stop_;
  GeneratorBodyCallback<To_> body_;
  GeneratorEndedCallback ended_;

  using Adapted_ = decltype(std::declval<E_>().template k<From_, Catches_>(
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
              typename MonostateIfEmptyOrVariantOf<Catches>::type>&&,
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
          GeneratorBeginCallback&&,
          GeneratorFailCallback<Raises>&&,
          GeneratorStopCallback&&,
          GeneratorBodyCallback<To>&&,
          GeneratorEndedCallback&&)>;

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
        DispatchCallback<From_, To_, Catches_, Raises_, Args_...>&& dispatch)
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
        std::optional<
            std::conditional_t<
                std::is_void_v<From_>,
                std::monostate,
                From_>>&& from = std::nullopt,
        std::optional<
            typename MonostateIfEmptyOrVariantOf<
                Catches_>::type>&& error = std::nullopt) {
      std::apply(
          [&](auto&... args) {
            dispatch_(
                action,
                std::move(error),
                args...,
                std::forward<decltype(from)>(from),
                e_,
                *interrupt_,
                [this](TypeErasedStream& stream) {
                  k_.Begin(stream);
                },
                [this](auto&&... void_or_errors) {
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

    DispatchCallback<From_, To_, Catches_, Raises_, Args_...> dispatch_;

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
    static constexpr bool CanCompose = Downstream::ExpectsStream;

    using Expects = SingleValue;

    template <typename T>
    using From = std::enable_if_t<
        IsUndefined<From_>::value,
        Composable<T, To_, Catches_, Raises_, Args_...>>;

    template <typename T>
    using To = std::enable_if_t<
        IsUndefined<To_>::value,
        Composable<From_, T, Catches_, Raises_, Args_...>>;

    template <typename... Errors>
    using Catches = std::enable_if_t<
        std::tuple_size_v<Catches_> == 0,
        Composable<From_, To_, std::tuple<Errors...>, Raises_, Args_...>>;

    template <typename... Errors>
    using Raises = std::enable_if_t<
        std::tuple_size_v<Raises_> == 0,
        Composable<From_, To_, Catches_, std::tuple<Errors...>, Args_...>>;

    template <typename... Args>
    using With = std::enable_if_t<
        sizeof...(Args_) == 0,
        Composable<From_, To_, Catches_, Raises_, Args...>>;

    template <typename T>
    using Of = std::enable_if_t<
        std::conjunction_v<IsUndefined<From_>, IsUndefined<To_>>,
        Composable<void, T, Catches_, Raises_, Args_...>>;

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

      using ErrorsFromE = typename E::template ErrorsFrom<From_, Catches_>;

      static_assert(
          std::disjunction_v<
              tuple_types_subset_subtype<ErrorsFromE, Raises_>,
              tuple_contains_exact_type<TypeErasedError, Raises_>>,
          "Specified errors can't be thrown from 'Generator'");

      using Value = typename E::template ValueFrom<From_, Catches_>;

      static_assert(
          std::is_convertible_v<Value, To_>,
          "eventual result type can not be converted "
          "into type of 'Generator'");

      dispatch_ = [f = std::move(f)](
                      Action action,
                      std::optional<
                          typename MonostateIfEmptyOrVariantOf<
                              Catches_>::type>&& error,
                      Args_&... args,
                      std::optional<
                          std::conditional_t<
                              std::is_void_v<From_>,
                              std::monostate,
                              From_>>&& arg,
                      std::unique_ptr<void, Callback<void(void*)>>& e_,
                      Interrupt& interrupt,
                      GeneratorBeginCallback&& begin,
                      GeneratorFailCallback<Raises_>&& fail,
                      GeneratorStopCallback&& stop,
                      GeneratorBodyCallback<To_>&& body,
                      GeneratorEndedCallback&& ended) mutable {
        if (!e_) {
          e_ = std::unique_ptr<void, Callback<void(void*)>>(
              new HeapGenerator<
                  E,
                  From_,
                  To_,
                  Catches_,
                  Raises_>(f(args...)),
              [](void* e) {
                delete static_cast<HeapGenerator<
                    E,
                    From_,
                    To_,
                    Catches_,
                    Raises_>*>(e);
              });
        }

        auto* e = static_cast<
            HeapGenerator<
                E,
                From_,
                To_,
                Catches_,
                Raises_>*>(e_.get());

        switch (action) {
          case Action::Start:
            CHECK(arg);
            e->Start(
                interrupt,
                std::move(arg.value()),
                std::move(begin),
                std::move(fail),
                std::move(stop),
                std::move(body),
                std::move(ended));
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
                  std::move(begin),
                  std::move(fail),
                  std::move(stop),
                  std::move(body),
                  std::move(ended));
              break;
            }
          }
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

    template <typename Arg, typename Errors, typename K>
    auto k(K k) && {
      static_assert(
          !std::disjunction_v<IsUndefined<From_>, IsUndefined<To_>>,
          "'Task' 'From' or 'To' type is not specified");

      return Continuation<K, From_, To_, Catches_, Raises_, Args_...>(
          std::move(k),
          std::move(args_),
          std::move(dispatch_));
    }

    std::conditional_t<
        std::disjunction_v<IsUndefined<From_>, IsUndefined<To_>>,
        Undefined,
        DispatchCallback<From_, To_, Catches_, Raises_, Args_...>>
        dispatch_;

    std::tuple<Args_...> args_;
  };
};

////////////////////////////////////////////////////////////////////////

// Creating a type alias to improve the readability.
using Generator = _Generator::Composable<
    Undefined,
    Undefined,
    std::tuple<>,
    std::tuple<>>;

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
