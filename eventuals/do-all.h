#pragma once

#include <atomic>
#include <optional>
#include <tuple>
#include <variant>

#include "eventuals/compose.h"
#include "eventuals/finally.h"
#include "eventuals/scheduler.h"
#include "eventuals/terminal.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

struct _DoAll final {
  // We need to run every eventual passed to 'DoAll()' with it's own
  // 'Scheduler::Context' so that it can be blocked, e.g., on
  // synchronization, get interrupted, etc. We abstract that into a
  // "fiber" similar to other constructs that require separate and
  // independent contexts.
  template <typename K_>
  struct Fiber {
    Fiber(K_ k)
      : k(std::move(k)) {}

    Fiber(Fiber&& that)
      : k(std::move(that.k)) {
      CHECK(!context) << "moving after starting";
    }

    std::optional<Scheduler::Context> context;
    Interrupt interrupt;
    K_ k;
  };

  template <typename K_, typename... Eventuals_>
  struct Adaptor final {
    using UnionOfErrorsTuple = tuple_types_union_all_t<
        typename Eventuals_::template ErrorsFrom<void, std::tuple<>>...>;

    using StoppedOrError = variant_of_type_and_tuple_t<Stopped, UnionOfErrorsTuple>;

    Adaptor(
        K_& k,
        stout::borrowed_ref<Scheduler::Context>&& previous,
        Callback<void()>& interrupter)
      : k_(k),
        previous_(std::move(previous)),
        interrupter_(interrupter) {}

    // NOTE: need to define move constructor but it shouldn't be
    // executed at runtime.
    Adaptor(Adaptor&& that) {
      LOG(FATAL) << "moving after starting";
    }

    K_& k_;
    stout::borrowed_ref<Scheduler::Context> previous_;
    Callback<void()>& interrupter_;

    std::tuple<std::variant<
        // NOTE: we use a dummy 'Undefined' in order to default
        // initialize the tuple. We don't use 'std::monostate'
        // because that's what we use for 'void' return types.
        Undefined,
        std::conditional_t<
            std::is_void_v<
                typename Eventuals_::template ValueFrom<void, std::tuple<>>>,
            std::monostate,
            typename Eventuals_::template ValueFrom<void, std::tuple<>>>,
        StoppedOrError>...>
        values_;

    static constexpr int UNDEFINED_INDEX = 0;
    static constexpr int VALUE_OR_VOID_INDEX = 1;
    static constexpr int STOPPED_OR_ERROR_INDEX = 2;

    std::atomic<size_t> counter_ = sizeof...(Eventuals_);

    template <size_t index, typename Eventual>
    [[nodiscard]] auto BuildFiber(Eventual eventual) {
      auto k = Build(
          std::move(eventual)
          // NOTE: need to reschedule to previous context before we
          // call into continuation!
          >> Reschedule(previous_.reborrow())
          >> Terminal()
                 .start([this](auto&&... value) {
                   using Value = typename Eventual::template ValueFrom<
                       void,
                       std::tuple<>>;
                   static_assert(
                       std::is_void_v<Value> || sizeof...(value) == 1);
                   if constexpr (!std::is_void_v<Value>) {
                     std::get<index>(values_)
                         .template emplace<std::decay_t<decltype(value)>...>(
                             std::forward<decltype(value)>(value)...);
                   } else {
                     std::get<index>(values_)
                         .template emplace<std::monostate>();
                   }
                   if (counter_.fetch_sub(1) == 1) {
                     // You're the last eventual so call the continuation.
                     std::optional<StoppedOrError> stopped_or_error =
                         GetStoppedOrErrorIfExists();

                     if (stopped_or_error) {
                       if constexpr (std::is_same_v<StoppedOrError, Stopped>) {
                         k_.Stop();
                       } else {
                         std::visit(
                             [this](auto&& error) {
                               k_.Fail(std::forward<decltype(error)>(error));
                             },
                             std::move(stopped_or_error.value()));
                       }
                     } else {
                       k_.Start(GetTupleOfValues());
                     }
                   }
                 })
                 .fail([this](auto&&... errors) {
                   std::get<index>(values_)
                       .template emplace<STOPPED_OR_ERROR_INDEX>(std::forward<decltype(errors)>(errors)...);
                   if (counter_.fetch_sub(1) == 1) {
                     // You're the last eventual so call the continuation.
                     std::optional<StoppedOrError> stopped_or_error =
                         GetStoppedOrErrorIfExists();

                     CHECK(stopped_or_error);
                     if constexpr (std::is_same_v<StoppedOrError, Stopped>) {
                       k_.Stop();
                     } else {
                       std::visit(
                           [this](auto&& error) {
                             k_.Fail(std::forward<decltype(error)>(error));
                           },
                           std::move(stopped_or_error.value()));
                     }
                   } else {
                     // Interrupt the remaining eventuals so we can
                     // propagate the failure.
                     interrupter_();
                   }
                 })
                 .stop([this]() {
                   std::get<index>(values_).template emplace<STOPPED_OR_ERROR_INDEX>(Stopped());
                   if (counter_.fetch_sub(1) == 1) {
                     // You're the last eventual so call the continuation.
                     std::optional<StoppedOrError> stopped_or_error =
                         GetStoppedOrErrorIfExists();

                     CHECK(stopped_or_error);
                     if constexpr (std::is_same_v<StoppedOrError, Stopped>) {
                       k_.Stop();
                     } else {
                       std::visit(
                           [this](auto&& error) {
                             k_.Fail(std::forward<decltype(error)>(error));
                           },
                           std::move(stopped_or_error.value()));
                     }
                   } else {
                     // Interrupt the remaining eventuals so we can
                     // propagate the stop.
                     interrupter_();
                   }
                 }));
      return Fiber<decltype(k)>(std::move(k));
    }

    std::tuple<
        std::conditional_t<
            std::is_void_v<
                typename Eventuals_::template ValueFrom<void, std::tuple<>>>,
            std::monostate,
            typename Eventuals_::template ValueFrom<void, std::tuple<>>>...>
    GetTupleOfValues() {
      return std::apply(
          [](auto&&... value) {
            // NOTE: not using `CHECK_EQ()` here because compiler
            // messages out the error: expected expression
            //    (CHECK_EQ(1, value.index()), ...);
            //     ^
            // That's why we use CHECK instead.
            ((CHECK(value.index() == 1)), ...);
            return std::make_tuple(std::get<1>(std::move(value))...);
          },
          std::move(values_));
    }

    std::optional<StoppedOrError> GetStoppedOrErrorIfExists() {
      std::optional<StoppedOrError> stopped_or_error;

      auto extract_stopped_or_error = [&stopped_or_error](auto& value) {
        if (value.index() == STOPPED_OR_ERROR_INDEX) {
          // NOTE: we'll arbitrarily propagate the last error that gets folded over
          // by just overwriting 'stopped_or_error' unless we've observed a 'Stopped'
          // because we always want to propagate 'Stopped'.
          if (!stopped_or_error.has_value() || !std::holds_alternative<Stopped>(stopped_or_error.value())) {
            stopped_or_error.emplace(std::get<STOPPED_OR_ERROR_INDEX>(value));
          }
        }
      };

      std::apply(
          [&extract_stopped_or_error](auto&... value) {
            (extract_stopped_or_error(value), ...);
          },
          values_);

      return stopped_or_error;
    }

    template <size_t... index>
    [[nodiscard]] auto BuildFibers(
        std::tuple<Eventuals_...>&& eventuals,
        std::index_sequence<index...>) {
      return std::make_tuple(
          BuildFiber<index>(std::move(std::get<index>(eventuals)))...);
    }
  };

  template <typename K_, typename... Eventuals_>
  struct Continuation final {
    Continuation(K_ k, std::tuple<Eventuals_...>&& eventuals)
      : eventuals_(std::move(eventuals)),
        k_(std::move(k)) {}

    Continuation(Continuation&& that)
      : eventuals_(std::move(that.eventuals_)),
        k_(std::move(that.k_)) {
      CHECK(!adaptor_) << "moving after starting";
    }

    template <typename... Args>
    void Start(Args&&...) {
      if (handler_.has_value() && !handler_->Install()) {
        // TODO: consider propagating through each eventual?
        k_.Stop();
      } else {
        adaptor_.emplace(
            k_,
            Scheduler::Context::Get().reborrow(),
            interrupter_);

        fibers_.emplace(adaptor_->BuildFibers(
            std::move(eventuals_),
            std::make_index_sequence<sizeof...(Eventuals_)>{}));

        std::apply(
            [](auto&... fiber) {
              static std::atomic<int> i = 0;

              // Clone the current scheduler context for running the eventual.
              (fiber.context.emplace(
                   Scheduler::Context::Get()->name()
                   + " [DoAll - " + std::to_string(i++) + "]"),
               ...);

              (fiber.context->scheduler()->Submit(
                   [&]() {
                     CHECK_EQ(
                         &fiber.context.value(),
                         Scheduler::Context::Get().get());
                     fiber.k.Register(fiber.interrupt);
                     fiber.k.Start();
                   },
                   fiber.context.value()),
               ...);
            },
            fibers_.value());
      }
    }

    template <typename Error>
    void Fail(Error&& error) {
      k_.Fail(std::forward<Error>(error));
    }

    void Stop() {
      // TODO: consider propagating through each eventual?
      k_.Stop();
    }

    void Register(Interrupt& interrupt) {
      k_.Register(interrupt);

      handler_.emplace(&interrupt, [this]() {
        interrupter_();
      });
    }

    // NOTE: need to destruct the fibers LAST since they have a
    // Scheduler::Context which may get borrowed in 'adaptor_' and
    // it's continuations so those need to be destructed first.
    std::optional<
        decltype(std::declval<
                     Adaptor<K_, Eventuals_...>>()
                     .BuildFibers(
                         std::declval<std::tuple<Eventuals_...>>(),
                         std::make_index_sequence<sizeof...(Eventuals_)>{}))>
        fibers_;

    std::tuple<Eventuals_...> eventuals_;

    std::optional<Adaptor<K_, Eventuals_...>> adaptor_;

    Callback<void()> interrupter_ = [this]() {
      // Trigger inner interrupt for each eventual.
      std::apply(
          [](auto&... fiber) {
            (fiber.interrupt.Trigger(), ...);
          },
          fibers_.value());
    };

    std::optional<Interrupt::Handler> handler_;

    // NOTE: we store 'k_' as the _last_ member so it will be
    // destructed _first_ and thus we won't have any use-after-delete
    // issues during destruction of 'k_' if it holds any references or
    // pointers to any (or within any) of the above members.
    K_ k_;
  };

  template <typename... Eventuals_>
  struct Composable final {
    template <typename Arg, typename Errors>
    using ValueFrom = std::tuple<
        std::conditional_t<
            std::is_void_v<
                typename Eventuals_::template ValueFrom<void, std::tuple<>>>,
            std::monostate,
            typename Eventuals_::template ValueFrom<void, std::tuple<>>>...>;

    using Errors_ = tuple_types_union_all_t<
        typename Eventuals_::template ErrorsFrom<void, std::tuple<>>...>;

    template <typename Arg, typename Errors>
    using ErrorsFrom = tuple_types_union_t<Errors, Errors_>;

    template <typename Arg, typename Errors, typename K>
    auto k(K k) && {
      return Continuation<K, Eventuals_...>(
          std::move(k),
          std::move(eventuals_));
    }

    template <typename Downstream>
    static constexpr bool CanCompose = Downstream::ExpectsValue;

    using Expects = SingleValue;

    std::tuple<Eventuals_...> eventuals_;
  };
};

////////////////////////////////////////////////////////////////////////

template <typename... Eventuals>
[[nodiscard]] auto DoAll(Eventuals... eventuals) {
  static_assert(
      sizeof...(Eventuals) > 0,
      "'DoAll' expects at least one eventual");

  return _DoAll::Composable<Eventuals...>{
      std::make_tuple(std::move(eventuals)...)};
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
