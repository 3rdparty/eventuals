#pragma once

#include <memory>
#include <optional>
#include <tuple>
#include <variant>

#include "eventuals/eventual.h"
#include "eventuals/stream.h"
#include "eventuals/terminal.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

template <typename E_, typename From_, typename To_>
struct HeapGenerator {
  struct Adaptor {
    Adaptor(
        Callback<TypeErasedStream&>* begin,
        Callback<std::exception_ptr>* fail,
        Callback<>* stop,
        std::conditional_t<
            std::is_void_v<To_>,
            Callback<>,
            Callback<To_>>* body,
        Callback<>* ended)
      : begin_(begin),
        fail_(fail),
        stop_(stop),
        body_(body),
        ended_(ended) {}

    // All functions are called as Continuation after produced Stream.
    void Begin(TypeErasedStream& stream) {
      (*begin_)(stream);
    }

    template <typename... Args>
    void Fail(Args&&... args) {
      (*fail_)(
          std::make_exception_ptr(
              std::forward<decltype(args)>(args)...));
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

    Callback<TypeErasedStream&>* begin_;
    Callback<std::exception_ptr>* fail_;
    Callback<>* stop_;
    std::conditional_t<
        std::is_void_v<To_>,
        Callback<>,
        Callback<To_>>* body_;
    Callback<>* ended_;
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
      Callback<TypeErasedStream&>&& begin,
      Callback<std::exception_ptr>&& fail,
      Callback<>&& stop,
      std::conditional_t<
          std::is_void_v<To_>,
          Callback<>,
          Callback<To_>>&& body,
      Callback<>&& ended) {
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
      Callback<TypeErasedStream&>&& begin,
      Callback<std::exception_ptr>&& fail,
      Callback<>&& stop,
      std::conditional_t<
          std::is_void_v<To_>,
          Callback<>,
          Callback<To_>>&& body,
      Callback<>&& ended) {
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
      Callback<TypeErasedStream&>&& begin,
      Callback<std::exception_ptr>&& fail,
      Callback<>&& stop,
      std::conditional_t<
          std::is_void_v<To_>,
          Callback<>,
          Callback<To_>>&& body,
      Callback<>&& ended) {
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

  Callback<TypeErasedStream&> begin_;
  Callback<std::exception_ptr> fail_;
  Callback<> stop_;
  std::conditional_t<
      std::is_void_v<To_>,
      Callback<>,
      Callback<To_>>
      body_;
  Callback<> ended_;

  using Adapted_ = decltype(std::declval<E_>().template k<From_>(
      std::declval<Adaptor>()));

  Adapted_ adapted_;
};

////////////////////////////////////////////////////////////////////////

struct _GeneratorFromToWith {
  // Since we move lambda function at 'Composable' constructor we need to
  // specify the callback that should be triggered on the produced eventual.
  // For this reason we use 'Action'.
  enum class Action {
    Start = 0,
    Stop = 1,
    Fail = 2,
  };

  template <typename K_, typename From_, typename To_, typename... Args_>
  struct Continuation {
    Continuation(
        K_ k,
        std::tuple<Args_...>&& args,
        Callback<
            Action,
            std::optional<std::exception_ptr>&&,
            Args_&&...,
            std::optional<
                std::conditional_t<
                    std::is_void_v<From_>,
                    std::monostate,
                    From_>>&&,
            std::unique_ptr<void, Callback<void*>>&,
            Interrupt&,
            Callback<TypeErasedStream&>&&,
            Callback<std::exception_ptr>&&,
            Callback<>&&,
            std::conditional_t<
                std::is_void_v<To_>,
                Callback<>,
                Callback<To_>>&&,
            Callback<>&&>&& dispatch)
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

    template <typename... Args>
    void Fail(Args&&... args) {
      std::exception_ptr exception;

      if constexpr (sizeof...(args) > 0) {
        exception = std::make_exception_ptr(
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
        std::optional<
            std::conditional_t<
                std::is_void_v<From_>,
                std::monostate,
                From_>>&& from = std::nullopt,
        std::optional<std::exception_ptr>&& exception = std::nullopt) {
      std::apply(
          [&](auto&&... args) {
            dispatch_(
                action,
                std::move(exception),
                std::forward<decltype(args)>(args)...,
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
          std::move(args_));
    }

    std::tuple<Args_...> args_;

    Callback<
        Action,
        std::optional<std::exception_ptr>&&,
        Args_&&...,
        std::optional<
            std::conditional_t<
                std::is_void_v<From_>,
                std::monostate,
                From_>>&&,
        std::unique_ptr<void, Callback<void*>>&,
        Interrupt&,
        Callback<TypeErasedStream&>&&,
        Callback<std::exception_ptr>&&,
        Callback<>&&,
        std::conditional_t<
            std::is_void_v<To_>,
            Callback<>,
            Callback<To_>>&&,
        Callback<>&&>
        dispatch_;

    std::unique_ptr<void, Callback<void*>> e_;
    Interrupt* interrupt_ = nullptr;

    // NOTE: we store 'k_' as the _last_ member so it will be
    // destructed _first_ and thus we won't have any use-after-delete
    // issues during destruction of 'k_' if it holds any references or
    // pointers to any (or within any) of the above members.
    K_ k_;
  };

  template <typename From_, typename To_, typename... Args_>
  struct Composable {
    template <typename Arg>
    using ValueFrom = To_;

    template <typename F>
    Composable(Args_... args, F f)
      : args_(std::tuple<Args_...>(std::move(args)...)) {
      static_assert(
          std::tuple_size<decltype(args_)>{} > 0 || std::is_invocable_v<F>,
          "'Generator' expects a callable that "
          "takes no arguments");

      static_assert(
          std::tuple_size<decltype(args_)>{}
              || std::is_invocable_v<F, Args_...>,
          "'Generator' expects a callable that "
          "takes the arguments specified");

      static_assert(
          sizeof(f) <= sizeof(void*),
          "'Generator' expects a callable that "
          "can be captured in a 'Callback'");

      using E = decltype(std::apply(f, args_));

      using Value = typename E::template ValueFrom<From_>;

      static_assert(
          std::is_convertible_v<Value, To_>,
          "eventual result type can not be converted into type of 'Generator'");

      dispatch_ = [f = std::move(f)](
                      Action action,
                      std::optional<std::exception_ptr>&& exception,
                      Args_&&... args,
                      std::optional<
                          std::conditional_t<
                              std::is_void_v<From_>,
                              std::monostate,
                              From_>>&& arg,
                      std::unique_ptr<void, Callback<void*>>& e_,
                      Interrupt& interrupt,
                      Callback<TypeErasedStream&>&& begin,
                      Callback<std::exception_ptr>&& fail,
                      Callback<>&& stop,
                      std::conditional_t<
                          std::is_void_v<To_>,
                          Callback<>,
                          Callback<To_>>&& body,
                      Callback<>&& ended) mutable {
        if (!e_) {
          e_ = std::unique_ptr<void, Callback<void*>>(
              new HeapGenerator<E, From_, To_>(f(std::move(args)...)),
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

    template <typename Arg, typename K>
    auto k(K k) && {
      return Continuation<K, From_, To_, Args_...>(
          std::move(k),
          std::move(args_),
          std::move(dispatch_));
    }

    Callback<
        Action,
        std::optional<std::exception_ptr>&&,
        Args_&&...,
        // Can't have a 'void' argument type so we are using 'std::monostate'.
        std::optional<
            std::conditional_t<
                std::is_void_v<From_>,
                std::monostate,
                From_>>&&,
        std::unique_ptr<void, Callback<void*>>&,
        Interrupt&,
        Callback<TypeErasedStream&>&&,
        Callback<std::exception_ptr>&&,
        Callback<>&&,
        std::conditional_t<
            std::is_void_v<To_>,
            Callback<>,
            Callback<To_>>&&,
        Callback<>&&>
        dispatch_;

    std::tuple<Args_...> args_;
  };
};

////////////////////////////////////////////////////////////////////////

// Creating a type alias to improve the readability.

template <typename Value, typename To, typename... Args>
using GeneratorFromToWith = _GeneratorFromToWith::Composable<
    Value,
    To,
    Args...>;

struct Generator {
  template <typename From_>
  struct From {
    template <typename To_>
    struct To : public GeneratorFromToWith<From_, To_> {
      template <typename... Args_>
      using With = GeneratorFromToWith<From_, To_, Args_...>;

      template <typename F>
      To(F f)
        : GeneratorFromToWith<From_, To_>(std::move(f)){};
    };

    template <typename F>
    From(F f) = delete;
  };

  template <typename To_>
  using Of = From<void>::To<To_>;
};

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
