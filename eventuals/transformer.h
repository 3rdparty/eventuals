#pragma once

#include <memory>
#include <optional>

#include "eventuals/callback.h"
#include "eventuals/stream.h"
#include "eventuals/terminal.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

template <typename E_, typename From_, typename To_>
struct HeapTransformer {
  struct Adaptor {
    Adaptor(
        Callback<TypeErasedStream&>* begin,
        Callback<std::exception_ptr>* fail,
        Callback<>* stop,
        Callback<To_>* body,
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
    void Body(Args&&... args) {
      (*body_)(std::forward<decltype(args)>(args)...);
    }

    template <typename... Args>
    void Fail(Args&&... args) {
      (*fail_)(
          make_exception_ptr_or_forward(
              std::forward<decltype(args)>(args)...));
    }

    void Stop() {
      (*stop_)();
    }

    void Ended() {
      (*ended_)();
    }

    // Already registered in 'adapted_'
    void Register(Interrupt&) {}

    Callback<TypeErasedStream&>* begin_;
    Callback<std::exception_ptr>* fail_;
    Callback<>* stop_;
    Callback<To_>* body_;
    Callback<>* ended_;
  };

  HeapTransformer(E_ e)
    : adapted_(
        std::move(e).template k<From_>(
            Adaptor{&begin_, &fail_, &stop_, &body_, &ended_})) {}

  void Body(
      From_&& arg,
      Interrupt& interrupt,
      Callback<TypeErasedStream&>&& begin,
      Callback<std::exception_ptr>&& fail,
      Callback<>&& stop,
      Callback<To_>&& body,
      Callback<>&& ended) {
    begin_ = std::move(begin);
    fail_ = std::move(fail);
    stop_ = std::move(stop);
    body_ = std::move(body);
    ended_ = std::move(ended);

    adapted_.Register(interrupt);

    adapted_.Body(std::move(arg));
  }

  void Fail(
      Interrupt& interrupt,
      std::exception_ptr&& exception,
      Callback<TypeErasedStream&>&& begin,
      Callback<std::exception_ptr>&& fail,
      Callback<>&& stop,
      Callback<To_>&& body,
      Callback<>&& ended) {
    begin_ = std::move(begin);
    fail_ = std::move(fail);
    stop_ = std::move(stop);
    body_ = std::move(body);
    ended_ = std::move(ended);

    adapted_.Register(interrupt);

    adapted_.Fail(std::move(exception));
  }

  void Stop(
      Interrupt& interrupt,
      Callback<TypeErasedStream&>&& begin,
      Callback<std::exception_ptr>&& fail,
      Callback<>&& stop,
      Callback<To_>&& body,
      Callback<>&& ended) {
    begin_ = std::move(begin);
    fail_ = std::move(fail);
    stop_ = std::move(stop);
    body_ = std::move(body);
    ended_ = std::move(ended);

    adapted_.Register(interrupt);

    adapted_.Stop();
  }

  Callback<TypeErasedStream&> begin_;
  Callback<std::exception_ptr> fail_;
  Callback<> stop_;
  Callback<To_> body_;
  Callback<> ended_;

  using Adapted_ = decltype(std::declval<E_>().template k<From_>(
      std::declval<Adaptor>()));

  Adapted_ adapted_;
};

////////////////////////////////////////////////////////////////////////

struct _TransformerFromTo {
  enum class Action {
    Body = 0,
    Fail = 1,
    Stop = 2,
  };

  template <typename K_, typename From_, typename To_>
  struct Continuation {
    template <typename Dispatch>
    Continuation(K_ k, Dispatch dispatch)
      : dispatch_(std::move(dispatch)),
        k_(std::move(k)) {}

    void Begin(TypeErasedStream& stream) {
      k_.Begin(stream);
    }

    template <typename... Args>
    void Fail(Args&&... args) {
      static_assert(
          sizeof...(args) > 0,
          "Transformer expects Fail() to be called with an argument");
      auto exception = make_exception_ptr_or_forward(
          std::forward<decltype(args)>(args)...);

      Dispatch(Action::Fail, std::nullopt, std::move(exception));
    }

    void Stop() {
      Dispatch(Action::Stop);
    }

    template <typename... From>
    void Body(From&&... from) {
      Dispatch(Action::Body, std::forward<From>(from)...);
    }

    void Ended() {
      k_.Ended();
    }

    void Register(Interrupt& interrupt) {
      interrupt_ = &interrupt;
      k_.Register(interrupt);
    }

    void Dispatch(
        Action action,
        std::optional<From_>&& from = std::nullopt,
        std::optional<std::exception_ptr>&& exception = std::nullopt) {
      dispatch_(
          action,
          std::move(exception),
          std::move(from),
          e_,
          *interrupt_,
          [this](TypeErasedStream& stream) {
            k_.Begin(stream);
          },
          [this](To_ arg) {
            k_.Body(std::move(arg));
          },
          [this](std::exception_ptr e) {
            k_.Fail(std::move(e));
          },
          [this]() {
            k_.Stop();
          },
          [this]() {
            k_.Ended();
          });
    }

    Callback<
        Action,
        std::optional<std::exception_ptr>&&,
        std::optional<From_>&&,
        std::unique_ptr<void, Callback<void*>>&,
        Interrupt&,
        Callback<TypeErasedStream&>&&,
        Callback<To_>&&,
        Callback<std::exception_ptr>&&,
        Callback<>&&,
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

  template <typename From_, typename To_>
  struct Composable {
    template <typename>
    using ValueFrom = To_;

    template <typename F>
    Composable(F f) {
      static_assert(
          std::is_invocable_v<F>,
          "'Transformer' expects a callable that "
          "takes no arguments");

      static_assert(
          sizeof(f) <= sizeof(void*),
          "'Transformer' expects a callable that "
          "can be captured in a 'Callback'");

      using E = decltype(f());

      using Value = typename E::template ValueFrom<From_>;

      static_assert(
          std::is_convertible_v<Value, To_>,
          "eventual result type can not be converted "
          "into type of 'Transformer'");

      dispatch_ = [f = std::move(f)](
                      Action action,
                      std::optional<std::exception_ptr>&& exception,
                      std::optional<From_>&& from,
                      std::unique_ptr<void, Callback<void*>>& e_,
                      Interrupt& interrupt,
                      Callback<TypeErasedStream&>&& begin,
                      Callback<To_>&& body,
                      Callback<std::exception_ptr>&& fail,
                      Callback<>&& stop,
                      Callback<>&& ended) {
        if (!e_) {
          e_ = std::unique_ptr<void, Callback<void*>>(
              new HeapTransformer<E, From_, To_>(f()),
              [](void* e) {
                delete static_cast<HeapTransformer<E, From_, To_>*>(e);
              });
        }

        auto* e = static_cast<HeapTransformer<E, From_, To_>*>(e_.get());

        switch (action) {
          case Action::Body:
            CHECK(from);
            e->Body(
                std::move(from.value()),
                interrupt,
                std::move(begin),
                std::move(fail),
                std::move(stop),
                std::move(body),
                std::move(ended));
            break;
          case Action::Fail:
            CHECK(exception);
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
      return Continuation<K, From_, To_>(
          std::move(k),
          std::move(dispatch_));
    }

    Callback<
        Action,
        std::optional<std::exception_ptr>&&,
        std::optional<From_>&&,
        std::unique_ptr<void, Callback<void*>>&,
        Interrupt&,
        Callback<TypeErasedStream&>&&,
        Callback<To_>&&,
        Callback<std::exception_ptr>&&,
        Callback<>&&,
        Callback<>&&>
        dispatch_;
  };
};

////////////////////////////////////////////////////////////////////////

template <typename From_, typename To_>
class TransformerFromTo {
 public:
  template <typename>
  using ValueFrom = To_;

  template <typename F>
  TransformerFromTo(F f)
    : e_(std::move(f)) {}

  template <typename Arg, typename K>
  auto k(K k) && {
    return std::move(e_).template k<Arg>(std::move(k));
  }

  // NOTE: should only be used in tests!
  auto operator*() && {
    auto [future, k] = Terminate(std::move(e_));
    k.Start();
    return future.get();
  }

 private:
  _TransformerFromTo::Composable<From_, To_> e_;
};

struct Transformer {
  template <typename From_>
  struct From {
    template <typename To_>
    struct To : public TransformerFromTo<From_, To_> {
      template <typename F>
      To(F f)
        : TransformerFromTo<From_, To_>(std::move(f)) {}
    };
    template <typename F>
    From(F f) = delete;
  };
};

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
