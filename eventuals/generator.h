#pragma once

#include <optional>
#include <tuple>

#include "eventuals/eventual.h"
#include "eventuals/stream.h"
#include "eventuals/terminal.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

namespace detail {

////////////////////////////////////////////////////////////////////////

template <typename E_>
struct HeapGenerator {
  using Value_ = typename E_::template ValueFrom<void>;

  template <typename Arg_>
  struct Adaptor {
    Adaptor(
        Callback<TypeErasedStream&>* begin,
        Callback<std::exception_ptr>* fail,
        Callback<>* stop,
        std::conditional_t<
            std::is_void_v<Value_>,
            Callback<>,
            Callback<Value_>>* body,
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
        std::is_void_v<Value_>,
        Callback<>,
        Callback<Value_>>* body_;
    Callback<>* ended_;
  };

  HeapGenerator(E_ e)
    : adapted_(
        std::move(e).template k<void>(
            Adaptor<Value_>{&begin_, &fail_, &stop_, &body_, &ended_})) {}

  void Start(
      Interrupt& interrupt,
      Callback<TypeErasedStream&>&& begin,
      Callback<std::exception_ptr>&& fail,
      Callback<>&& stop,
      std::conditional_t<
          std::is_void_v<Value_>,
          Callback<>,
          Callback<Value_>>&& body,
      Callback<> ended) {
    begin_ = std::move(begin);
    fail_ = std::move(fail);
    stop_ = std::move(stop);
    body_ = std::move(body);
    ended_ = std::move(ended);

    // TODO(benh): clarify the semantics of whether or not calling
    // 'Register()' more than once is well-defined.
    adapted_.Register(interrupt);

    adapted_.Start();
  }

  void Fail(
      Interrupt& interrupt,
      std::exception_ptr&& fail_exception,
      Callback<TypeErasedStream&>&& begin,
      Callback<std::exception_ptr>&& fail,
      Callback<>&& stop,
      std::conditional_t<
          std::is_void_v<Value_>,
          Callback<>,
          Callback<Value_>>&& body,
      Callback<> ended) {
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

  // All callbacks and interrupt should be registered before this call.
  void Next() {
    adapted_.Next();
  }

  void Done() {
    adapted_.Done();
  }

  void Stop(
      Interrupt& interrupt,
      Callback<TypeErasedStream&>&& begin,
      Callback<std::exception_ptr>&& fail,
      Callback<>&& stop,
      std::conditional_t<
          std::is_void_v<Value_>,
          Callback<>,
          Callback<Value_>>&& body,
      Callback<> ended) {
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
      std::is_void_v<Value_>,
      Callback<>,
      Callback<Value_>>
      body_;
  Callback<> ended_;

  using Adapted_ = decltype(std::declval<E_>().template k<void>(
      std::declval<Adaptor<Value_>>()));

  Adapted_ adapted_;
};

////////////////////////////////////////////////////////////////////////

struct _GeneratorWith {
  // Since we move lambda function at 'Composable' constructor we need to
  // specify the callback that should be triggered on the produced eventual.
  // For this reason we use 'Action'.
  enum class Action {
    Start = 0,
    Stop = 1,
    Fail = 2,
    Next = 3,
    Done = 4,
  };

  using exception = std::optional<std::exception_ptr>;

  template <typename K_, typename Value_, typename... Args_>
  struct Continuation : public TypeErasedStream {
    Continuation(
        K_ k,
        std::tuple<Args_...>&& args,
        Callback<
            Action,
            exception,
            Args_&&...,
            std::unique_ptr<void, Callback<void*>>&,
            Interrupt&,
            Callback<TypeErasedStream&>&&,
            Callback<std::exception_ptr>&&,
            Callback<>&&,
            std::conditional_t<
                std::is_void_v<Value_>,
                Callback<>,
                Callback<Value_>>&&,
            Callback<>&&>&& dispatch)
      : k_(std::move(k)),
        args_(std::move(args)),
        dispatch_(std::move(dispatch)) {}

    // All Continuation functions just trigger dispatch Callback,
    // that stores all callbacks for different events
    // (Start, Stop, Fail, Body, Ended). To specify the function to call
    // use Action enum state.
    template <typename... Args>
    void Start(Args&&...) {
      std::apply(
          [&](auto&&... args) {
            dispatch_(
                Action::Start,
                std::nullopt,
                std::forward<decltype(args)>(args)...,
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

      std::apply(
          [&](auto&&... args) {
            dispatch_(
                Action::Fail,
                std::move(exception),
                std::forward<decltype(args)>(args)...,
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

    void Stop() {
      std::apply(
          [&](auto&&... args) {
            dispatch_(
                Action::Stop,
                std::nullopt,
                std::forward<decltype(args)>(args)...,
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

    void Next() override {
      std::apply(
          [&](auto&&... args) {
            dispatch_(
                Action::Next,
                std::nullopt,
                std::forward<decltype(args)>(args)...,
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

    void Done() override {
      std::apply(
          [&](auto&&... args) {
            dispatch_(
                Action::Done,
                std::nullopt,
                std::forward<decltype(args)>(args)...,
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

    void Register(Interrupt& interrupt) {
      interrupt_ = &interrupt;
      k_.Register(interrupt);
    }

    K_ k_;
    std::tuple<Args_...> args_;

    Callback<
        Action,
        exception,
        Args_&&...,
        std::unique_ptr<void, Callback<void*>>&,
        Interrupt&,
        Callback<TypeErasedStream&>&&,
        Callback<std::exception_ptr>&&,
        Callback<>&&,
        std::conditional_t<
            std::is_void_v<Value_>,
            Callback<>,
            Callback<Value_>>&&,
        Callback<>&&>
        dispatch_;

    std::unique_ptr<void, Callback<void*>> e_;
    Interrupt* interrupt_ = nullptr;
  };

  template <typename Value_, typename... Args_>
  struct Composable {
    template <typename>
    using ValueFrom = Value_;

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

      using Value = typename E::template ValueFrom<void>;

      static_assert(
          std::is_convertible_v<Value, Value_>,
          "eventual result type can not be converted into type of 'Generator'");

      dispatch_ = [f = std::move(f)](
                      Action action,
                      exception fail_exception,
                      Args_&&... args,
                      std::unique_ptr<void, Callback<void*>>& e_,
                      Interrupt& interrupt,
                      Callback<TypeErasedStream&>&& begin,
                      Callback<std::exception_ptr>&& fail,
                      Callback<>&& stop,
                      std::conditional_t<
                          std::is_void_v<Value_>,
                          Callback<>,
                          Callback<Value_>>&& body,
                      Callback<>&& ended) {
        if (!e_) {
          e_ = std::unique_ptr<void, Callback<void*>>(
              new HeapGenerator<E>(f(std::move(args)...)),
              [](void* e) {
                delete static_cast<detail::HeapGenerator<E>*>(e);
              });
        }

        auto* e = static_cast<HeapGenerator<E>*>(e_.get());

        switch (action) {
          case Action::Start:
            e->Start(
                interrupt,
                std::move(begin),
                std::move(fail),
                std::move(stop),
                std::move(body),
                std::move(ended));
            break;
          case Action::Fail:
            e->Fail(
                interrupt,
                std::move(fail_exception.value()),
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
          case Action::Next:
            e->Next();
            break;
          case Action::Done:
            e->Done();
            break;
          default:
            LOG(FATAL) << "unreachable";
        }
      };
    }

    template <typename Arg, typename K>
    auto k(K k) && {
      return Continuation<K, Value_, Args_...>(
          std::move(k),
          std::move(args_),
          std::move(dispatch_));
    }

    Callback<
        Action,
        exception,
        Args_&&...,
        std::unique_ptr<void, Callback<void*>>&,
        Interrupt&,
        Callback<TypeErasedStream&>&&,
        Callback<std::exception_ptr>&&,
        Callback<>&&,
        std::conditional_t<
            std::is_void_v<Value_>,
            Callback<>,
            Callback<Value_>>&&,
        Callback<>&&>
        dispatch_;

    std::tuple<Args_...> args_;
  };
};

////////////////////////////////////////////////////////////////////////

} // namespace detail

////////////////////////////////////////////////////////////////////////

template <typename Value_, typename... Args_>
class GeneratorWith {
 public:
  template <typename Arg>
  using ValueFrom = Value_;

  template <typename F>
  GeneratorWith(Args_... args, F f)
    : e_(std::move(args)..., std::move(f)) {}

  template <typename Arg, typename K>
  auto k(K k) && {
    return std::move(e_).template k<Arg>(std::move(k));
  }

  auto operator*() && {
    auto [future, k] = Terminate(std::move(e_));

    k.Start();

    return future.get();
  }


 private:
  detail::_GeneratorWith::Composable<Value_, Args_...> e_;
};

template <typename Value_>
class Generator : public GeneratorWith<Value_> {
 public:
  template <typename... Args_>
  using With = GeneratorWith<Value_, Args_...>;

  template <typename F>
  Generator(F f)
    : GeneratorWith<Value_>(std::move(f)) {}
};
////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
