#pragma once

#include <memory>
#include <optional>
#include <variant>

#include "eventuals/callback.h"
#include "eventuals/finally.h"
#include "eventuals/stream.h"
#include "eventuals/task.h"
#include "eventuals/terminal.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

template <
    typename E_,
    typename From_,
    typename To_,
    typename Catches_,
    typename Raises_>
struct HeapTransformer final {
  using BeginCallback_ = Callback<void(TypeErasedStream&)>;
  using FailCallback_ = Callback<function_type_t<
      void,
      get_rvalue_type_or_void_t<
          typename VariantErrorsHelper<
              variant_of_type_and_tuple_t<
                  std::monostate,
                  Raises_>>::type>>>;
  using StopCallback_ = Callback<void()>;
  using BodyCallback_ = Callback<void(To_)>;
  using EndedCallback_ = Callback<void()>;


  struct Adaptor final {
    Adaptor(
        BeginCallback_* begin,
        FailCallback_* fail,
        StopCallback_* stop,
        BodyCallback_* body,
        EndedCallback_* ended)
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
      (*fail_)(std::move(error));
    }

    void Stop() {
      (*stop_)();
    }

    void Ended() {
      (*ended_)();
    }

    // Already registered in 'adapted_'
    void Register(Interrupt&) {}

    BeginCallback_* begin_;
    FailCallback_* fail_;
    StopCallback_* stop_;
    BodyCallback_* body_;
    EndedCallback_* ended_;
  };

  HeapTransformer(E_ e)
    : adapted_(
        std::move(e).template k<From_, Catches_>(
            Adaptor{&begin_, &fail_, &stop_, &body_, &ended_})) {}

  void Body(
      From_&& arg,
      Interrupt& interrupt,
      BeginCallback_&& begin,
      FailCallback_&& fail,
      StopCallback_&& stop,
      BodyCallback_&& body,
      EndedCallback_&& ended) {
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
      std::conditional_t<
          std::tuple_size_v<Catches_>,
          apply_tuple_types_t<std::variant, Catches_>,
          std::monostate>&& error,
      BeginCallback_&& begin,
      FailCallback_&& fail,
      StopCallback_&& stop,
      BodyCallback_&& body,
      EndedCallback_&& ended) {
    begin_ = std::move(begin);
    fail_ = std::move(fail);
    stop_ = std::move(stop);
    body_ = std::move(body);
    ended_ = std::move(ended);

    adapted_.Register(interrupt);

    std::visit(
        [this](auto&& error) {
          if constexpr (!std::is_same_v<
                            std::decay_t<decltype(error)>,
                            std::monostate>) {
            adapted_.Fail(std::move(error));
          }
        },
        std::move(error));
  }

  void Stop(
      Interrupt& interrupt,
      BeginCallback_&& begin,
      FailCallback_&& fail,
      StopCallback_&& stop,
      BodyCallback_&& body,
      EndedCallback_&& ended) {
    begin_ = std::move(begin);
    fail_ = std::move(fail);
    stop_ = std::move(stop);
    body_ = std::move(body);
    ended_ = std::move(ended);

    adapted_.Register(interrupt);

    adapted_.Stop();
  }

  BeginCallback_ begin_;
  FailCallback_ fail_;
  StopCallback_ stop_;
  BodyCallback_ body_;
  EndedCallback_ ended_;

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
    template <typename Dispatch>
    Continuation(K_ k, Dispatch dispatch)
      : dispatch_(std::move(dispatch)),
        k_(std::move(k)) {}

    void Begin(TypeErasedStream& stream) {
      k_.Begin(stream);
    }

    template <typename Error>
    void Fail(Error&& error) {
      if constexpr (tuple_contains_exact_type_v<Error, Catches_>) {
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
            std::conditional_t<
                std::tuple_size_v<Catches_>,
                apply_tuple_types_t<std::variant, Catches_>,
                std::monostate>>&& error = std::nullopt) {
      dispatch_(
          action,
          std::move(error),
          std::move(from),
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
            }
          },
          [this]() {
            k_.Stop();
          },
          [this](To_ arg) {
            k_.Body(std::move(arg));
          },
          [this]() {
            k_.Ended();
          });
    }

    using BeginCallback_ = Callback<void(TypeErasedStream&)>;
    using FailCallback_ = Callback<function_type_t<
        void,
        get_rvalue_type_or_void_t<
            typename VariantErrorsHelper<
                variant_of_type_and_tuple_t<
                    std::monostate,
                    Raises_>>::type>>>;
    using StopCallback_ = Callback<void()>;
    using BodyCallback_ = Callback<void(To_)>;
    using EndedCallback_ = Callback<void()>;

    Callback<void(
        Action,
        std::optional<
            std::conditional_t<
                std::tuple_size_v<Catches_>,
                apply_tuple_types_t<std::variant, Catches_>,
                std::monostate>>&&,
        std::optional<From_>&&,
        std::unique_ptr<void, Callback<void(void*)>>&,
        Interrupt&,
        BeginCallback_&&,
        FailCallback_&&,
        StopCallback_&&,
        BodyCallback_&&,
        EndedCallback_&&)>
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

    using BeginCallback_ = Callback<void(TypeErasedStream&)>;
    using FailCallback_ = Callback<function_type_t<
        void,
        get_rvalue_type_or_void_t<
            typename VariantErrorsHelper<
                variant_of_type_and_tuple_t<
                    std::monostate,
                    Raises_>>::type>>>;
    using StopCallback_ = Callback<void()>;
    using BodyCallback_ = Callback<void(To_)>;
    using EndedCallback_ = Callback<void()>;

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
                          std::conditional_t<
                              std::tuple_size_v<Catches_>,
                              apply_tuple_types_t<std::variant, Catches_>,
                              std::monostate>>&& error,
                      std::optional<From_>&& from,
                      std::unique_ptr<void, Callback<void(void*)>>& e_,
                      Interrupt& interrupt,
                      BeginCallback_&& begin,
                      FailCallback_&& fail,
                      StopCallback_&& stop,
                      BodyCallback_&& body,
                      EndedCallback_&& ended) {
        if (!e_) {
          e_ = std::unique_ptr<void, Callback<void(void*)>>(
              new HeapTransformer<E, From_, To_, Catches_, Raises_>(f()),
              [](void* e) {
                delete static_cast<
                    HeapTransformer<E, From_, To_, Catches_, Raises_>*>(e);
              });
        }

        auto* e = static_cast<
            HeapTransformer<E, From_, To_, Catches_, Raises_>*>(e_.get());

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
            std::conditional_t<
                std::tuple_size_v<Catches_>,
                apply_tuple_types_t<std::variant, Catches_>,
                std::monostate>>&&,
        std::optional<From_>&&,
        std::unique_ptr<void, Callback<void(void*)>>&,
        Interrupt&,
        BeginCallback_&&,
        FailCallback_&&,
        StopCallback_&&,
        BodyCallback_&&,
        EndedCallback_&&)>
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
