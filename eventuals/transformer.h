#pragma once

#include <memory>
#include <optional>
#include <variant>

#include "eventuals/callback.h"
#include "eventuals/finally.h"
#include "eventuals/stream.h"
#include "eventuals/terminal.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

template <typename E_, typename From_, typename To_, typename Catches_>
struct HeapTransformer final {
  struct Adaptor final {
    Adaptor(
        Callback<void(TypeErasedStream&)>* begin,
        Callback<void(
            typename VariantOfStoppedAndErrors<Catches_>::type)>* fail,
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
      // Using to avoid compile error when we do both 'Raise' and 'Catch'
      // inside a task.
      if constexpr (std::tuple_size_v<Catches_> != 0) {
        (*fail_)(std::move(error));
      }
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
    Callback<void(typename VariantOfStoppedAndErrors<Catches_>::type)>* fail_;
    Callback<void()>* stop_;
    Callback<void(To_)>* body_;
    Callback<void()>* ended_;
  };

  HeapTransformer(E_ e)
    : adapted_(
        std::move(e).template k<From_, Catches_>(
            Adaptor{&begin_, &fail_, &stop_, &body_, &ended_})) {}

  void Body(
      From_&& arg,
      Interrupt& interrupt,
      Callback<void(TypeErasedStream&)>&& begin,
      Callback<void(
          typename VariantOfStoppedAndErrors<Catches_>::type)>&& fail,
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
      typename VariantOfStoppedAndErrors<Catches_>::type&& error,
      Callback<void(TypeErasedStream&)>&& begin,
      Callback<
          void(typename VariantOfStoppedAndErrors<Catches_>::type)>&& fail,
      Callback<void()>&& stop,
      Callback<void(To_)>&& body,
      Callback<void()>&& ended) {
    begin_ = std::move(begin);
    fail_ = std::move(fail);
    stop_ = std::move(stop);
    body_ = std::move(body);
    ended_ = std::move(ended);

    adapted_.Register(interrupt);

    // Using to avoid compile error when we do both 'Raise' and 'Catch'
    // inside a task.
    if constexpr (std::tuple_size_v<Catches_> != 0) {
      adapted_.Fail(std::move(error));
    }
  }

  void Stop(
      Interrupt& interrupt,
      Callback<void(TypeErasedStream&)>&& begin,
      Callback<
          void(typename VariantOfStoppedAndErrors<Catches_>::type)>&& fail,
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
  Callback<void(typename VariantOfStoppedAndErrors<Catches_>::type)> fail_;
  Callback<void()> stop_;
  Callback<void(To_)> body_;
  Callback<void()> ended_;

  using Adapted_ = decltype(std::declval<E_>().template k<From_, Catches_>(
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

  template <
      typename K_,
      typename From_,
      typename To_,
      typename Catches_,
      typename Raises_>
  struct Continuation final {
    using ErrorTypes = tuple_types_union_t<Raises_, Catches_>;

    template <typename Dispatch>
    Continuation(K_ k, Dispatch dispatch)
      : dispatch_(std::move(dispatch)),
        k_(std::move(k)) {}

    void Begin(TypeErasedStream& stream) {
      k_.Begin(stream);
    }

    template <typename Error>
    void Fail(Error&& error) {
      static_assert(
          std::disjunction_v<
              std::is_base_of<std::exception, std::decay_t<Error>>,
              check_variant_errors<std::decay_t<Error>>>,
          "Expecting a type derived from std::exception");

      if constexpr (tuple_contains_exact_type_v<Error, ErrorTypes>) {
        Dispatch(Action::Fail, std::nullopt, std::forward<Error>(error));
      } else {
        k_.Fail(std::forward<Error>(error));
      }
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
        std::optional<
            typename VariantOfStoppedAndErrors<
                ErrorTypes>::type>&& error = std::nullopt) {
      dispatch_(
          action,
          std::move(error),
          std::move(from),
          e_,
          *interrupt_,
          [this](TypeErasedStream& stream) {
            k_.Begin(stream);
          },
          [this](To_ arg) {
            k_.Body(std::move(arg));
          },
          [this](
              typename VariantOfStoppedAndErrors<
                  ErrorTypes>::type errors) {
            std::visit(
                [this](auto&& error) {
                  k_.Fail(std::forward<decltype(error)>(error));
                },
                errors);
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
        std::optional<
            typename VariantOfStoppedAndErrors<
                ErrorTypes>::type>&&,
        std::optional<From_>&&,
        std::unique_ptr<void, Callback<void(void*)>>&,
        Interrupt&,
        Callback<void(TypeErasedStream&)>&&,
        Callback<void(To_)>&&,
        Callback<void(
            typename VariantOfStoppedAndErrors<
                ErrorTypes>::type)>&&,
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

  template <
      typename From_,
      typename To_,
      typename Catches_,
      typename Raises_>
  struct Composable final {
    template <typename Arg, typename Errors>
    using ValueFrom = To_;

    template <typename Arg, typename Errors>
    using ErrorsFrom = tuple_types_union_t<
        Raises_,
        tuple_types_subtract_t<Errors, Catches_>>;

    template <typename Downstream>
    static constexpr bool CanCompose = Downstream::ExpectsStream;

    using Expects = StreamOfValues;

    template <typename T>
    using From = std::enable_if_t<
        IsUndefined<From_>::value,
        Composable<T, To_, Catches_, Raises_>>;

    template <typename T>
    using To = std::enable_if_t<
        !IsUndefined<From_>::value && IsUndefined<To_>::value,
        Composable<From_, T, Catches_, Raises_>>;

    template <typename... Errors>
    using Catches = std::enable_if_t<
        std::tuple_size_v<Catches_> == 0,
        Composable<From_, To_, std::tuple<Errors...>, Raises_>>;

    template <typename... Errors>
    using Raises = std::enable_if_t<
        std::tuple_size_v<Raises_> == 0,
        Composable<From_, To_, Catches_, std::tuple<Errors...>>>;

    using ErrorTypes = tuple_types_union_t<Raises_, Catches_>;

    template <typename F>
    Composable(F f) {
      static_assert(
          std::is_invocable_v<F>,
          "'Transformer' expects a callable (e.g., a lambda) that "
          "takes no arguments");

      static_assert(
          sizeof(f) <= SIZEOF_CALLBACK,
          "'Transformer' expects a callable (e.g., a lambda) that "
          "can be captured in a 'Callback'");

      using E = decltype(f());

      static_assert(
          std::is_void_v<E> || HasValueFrom<E>::value,
          "'Transformer' expects a callable (e.g., a lambda) that "
          "returns an eventual but you're returning a value");

      static_assert(
          !std::is_void_v<E>,
          "'Transformer' expects a callable (e.g., a lambda) that "
          "returns an eventual but you're not returning anything");

      using ErrorsFromE = typename E::template ErrorsFrom<From_, Catches_>;

      static_assert(
          eventuals::tuple_types_subset_subtype_v<ErrorsFromE, Raises_>,
          "Specified errors can't be thrown from 'Transformer'");

      using Value = typename E::template ValueFrom<From_, Catches_>;

      static_assert(
          std::is_convertible_v<Value, To_>,
          "eventual result type can not be converted "
          "into type of 'Transformer'");

      dispatch_ = [f = std::move(f)](
                      Action action,
                      std::optional<
                          typename VariantOfStoppedAndErrors<
                              ErrorTypes>::type>&& error,
                      std::optional<From_>&& from,
                      std::unique_ptr<void, Callback<void(void*)>>& e_,
                      Interrupt& interrupt,
                      Callback<void(TypeErasedStream&)>&& begin,
                      Callback<void(To_)>&& body,
                      Callback<
                          void(typename VariantOfStoppedAndErrors<
                               ErrorTypes>::type)>&& fail,
                      Callback<void()>&& stop,
                      Callback<void()>&& ended) {
        if (!e_) {
          e_ = std::unique_ptr<void, Callback<void(void*)>>(
              new HeapTransformer<E, From_, To_, ErrorTypes>(f()),
              [](void* e) {
                delete static_cast<
                    HeapTransformer<E, From_, To_, ErrorTypes>*>(e);
              });
        }

        auto* e = static_cast<
            HeapTransformer<E, From_, To_, ErrorTypes>*>(e_.get());

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

    template <typename Arg, typename Errors, typename K>
    auto k(K k) && {
      static_assert(
          !std::disjunction_v<IsUndefined<From_>, IsUndefined<To_>>,
          "'Transformer' 'From' or 'To' type is not specified");

      return Continuation<K, From_, To_, Catches_, Raises_>(
          std::move(k),
          std::move(dispatch_));
    }

    Callback<void(
        Action,
        std::optional<
            typename VariantOfStoppedAndErrors<
                ErrorTypes>::type>&&,
        std::optional<From_>&&,
        std::unique_ptr<void, Callback<void(void*)>>&,
        Interrupt&,
        Callback<void(TypeErasedStream&)>&&,
        Callback<void(To_)>&&,
        Callback<void(
            typename VariantOfStoppedAndErrors<
                ErrorTypes>::type)>&&,
        Callback<void()>&&,
        Callback<void()>&&)>
        dispatch_;
  };
};

////////////////////////////////////////////////////////////////////////

using Transformer = _Transformer::Composable<
    Undefined,
    Undefined,
    std::tuple<>,
    std::tuple<>>;

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
