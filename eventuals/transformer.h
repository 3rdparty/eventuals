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
struct HeapTransformer final {
  struct Adaptor final {
    Adaptor(
        Callback<void(TypeErasedStream&)>* begin,
        Callback<void(std::exception_ptr)>* fail,
        Callback<void()>* stop,
        Callback<void(To_)>* body,
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

    template <typename... Args>
    void Body(Args&&... args) {
      (*body_)(std::forward<decltype(args)>(args)...);
    }

    template <typename Error>
    void Fail(Error&& error) {
      (*fail_)(
          make_exception_ptr_or_forward(
              std::forward<Error>(error)));
    }

    void Stop() {
      (*stop_)();
    }

    void Ended() {
      (*ended_)();
    }

    // Already registered in 'adapted_'
    void Register(Interrupt&) {}

    Callback<void(TypeErasedStream&)>* begin_;
    Callback<void(std::exception_ptr)>* fail_;
    Callback<void()>* stop_;
    Callback<void(To_)>* body_;
    Callback<void()>* ended_;
  };

  HeapTransformer(E_ e)
    : adapted_(
        std::move(e).template k<From_>(
            Adaptor{&begin_, &fail_, &stop_, &body_, &ended_})) {}

  void Body(
      From_&& arg,
      Interrupt& interrupt,
      Callback<void(TypeErasedStream&)>&& begin,
      Callback<void(std::exception_ptr)>&& fail,
      Callback<void()>&& stop,
      Callback<void(To_)>&& body,
      Callback<void()>&& ended) {
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
      Callback<void(TypeErasedStream&)>&& begin,
      Callback<void(std::exception_ptr)>&& fail,
      Callback<void()>&& stop,
      Callback<void(To_)>&& body,
      Callback<void()>&& ended) {
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
      Callback<void(TypeErasedStream&)>&& begin,
      Callback<void(std::exception_ptr)>&& fail,
      Callback<void()>&& stop,
      Callback<void(To_)>&& body,
      Callback<void()>&& ended) {
    begin_ = std::move(begin);
    fail_ = std::move(fail);
    stop_ = std::move(stop);
    body_ = std::move(body);
    ended_ = std::move(ended);

    adapted_.Register(interrupt);

    adapted_.Stop();
  }

  Callback<void(TypeErasedStream&)> begin_;
  Callback<void(std::exception_ptr)> fail_;
  Callback<void()> stop_;
  Callback<void(To_)> body_;
  Callback<void()> ended_;

  using Adapted_ = decltype(std::declval<E_>().template k<From_>(
      std::declval<Adaptor>()));

  Adapted_ adapted_;
};

////////////////////////////////////////////////////////////////////////

struct _Transformer final {
  enum class Action {
    Body = 0,
    Fail = 1,
    Stop = 2,
  };

  template <typename K_, typename From_, typename To_, typename Errors_>
  struct Continuation final {
    template <typename Dispatch>
    Continuation(K_ k, Dispatch dispatch)
      : dispatch_(std::move(dispatch)),
        k_(std::move(k)) {}

    void Begin(TypeErasedStream& stream) {
      k_.Begin(stream);
    }

    template <typename Error>
    void Fail(Error&& error) {
      auto exception = make_exception_ptr_or_forward(
          std::forward<Error>(error));

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

    Callback<void(
        Action,
        std::optional<std::exception_ptr>&&,
        std::optional<From_>&&,
        std::unique_ptr<void, Callback<void(void*)>>&,
        Interrupt&,
        Callback<void(TypeErasedStream&)>&&,
        Callback<void(To_)>&&,
        Callback<void(std::exception_ptr)>&&,
        Callback<void()>&&,
        Callback<void()>&&)>
        dispatch_;

    std::unique_ptr<void, Callback<void(void*)>> e_;
    Interrupt* interrupt_ = nullptr;

    // NOTE: we store 'k_' as the _last_ member so it will be
    // destructed _first_ and thus we won't have any use-after-delete
    // issues during destruction of 'k_' if it holds any references or
    // pointers to any (or within any) of the above members.
    K_ k_;
  };

  template <typename From_, typename To_, typename Errors_>
  struct Composable final {
    template <typename>
    using ValueFrom = To_;

    template <typename Arg, typename Errors>
    using ErrorsFrom = tuple_types_union_t<Errors, Errors_>;

    template <typename T>
    using From = std::enable_if_t<
        IsUndefined<From_>::value,
        Composable<T, To_, Errors_>>;

    template <typename T>
    using To = std::enable_if_t<
        !IsUndefined<From_>::value && IsUndefined<To_>::value,
        Composable<From_, T, Errors_>>;

    template <typename... Errors>
    using Raises = std::enable_if_t<
        std::tuple_size_v<Errors_> == 0,
        Composable<From_, To_, std::tuple<Errors...>>>;

    template <typename F>
    Composable(F f) {
      static_assert(
          std::is_invocable_v<F>,
          "'Transformer' expects a callable (e.g., a lambda) that "
          "takes no arguments");

      static_assert(
          sizeof(f) <= sizeof(void*),
          "'Transformer' expects a callable (e.g., a lambda) that "
          "can be captured in a 'Callback'");

      using E = decltype(f());

      static_assert(
          HasValueFrom<E>::value,
          "'Transformer' expects a callable (e.g., a lambda) that "
          "returns an eventual");

      using ErrorsFromE = typename E::template ErrorsFrom<From_, std::tuple<>>;

      static_assert(
          eventuals::tuple_types_subset_subtype_v<ErrorsFromE, Errors_>,
          "Specified errors can't be thrown from 'Transformer'");

      using Value = typename E::template ValueFrom<From_>;

      static_assert(
          std::is_convertible_v<Value, To_>,
          "eventual result type can not be converted "
          "into type of 'Transformer'");

      dispatch_ = [f = std::move(f)](
                      Action action,
                      std::optional<std::exception_ptr>&& exception,
                      std::optional<From_>&& from,
                      std::unique_ptr<void, Callback<void(void*)>>& e_,
                      Interrupt& interrupt,
                      Callback<void(TypeErasedStream&)>&& begin,
                      Callback<void(To_)>&& body,
                      Callback<void(std::exception_ptr)>&& fail,
                      Callback<void()>&& stop,
                      Callback<void()>&& ended) {
        if (!e_) {
          e_ = std::unique_ptr<void, Callback<void(void*)>>(
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
      return Continuation<K, From_, To_, Errors_>(
          std::move(k),
          std::move(dispatch_));
    }

    Callback<void(
        Action,
        std::optional<std::exception_ptr>&&,
        std::optional<From_>&&,
        std::unique_ptr<void, Callback<void(void*)>>&,
        Interrupt&,
        Callback<void(TypeErasedStream&)>&&,
        Callback<void(To_)>&&,
        Callback<void(std::exception_ptr)>&&,
        Callback<void()>&&,
        Callback<void()>&&)>
        dispatch_;
  };
};

////////////////////////////////////////////////////////////////////////

using Transformer = _Transformer::Composable<
    Undefined,
    Undefined,
    std::tuple<>>;

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
