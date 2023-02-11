#pragma once

#include <atomic>
#include <optional>
#include <tuple>
#include <variant>

#include "eventuals/compose.h"
#include "eventuals/scheduler.h"
#include "eventuals/terminal.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

struct _DoAll final {
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
    Adaptor(
        K_& k,
        stout::borrowed_ref<Scheduler::Context>&& context,
        Callback<void()>& interrupter)
      : k_(k),
        context_(std::move(context)),
        interrupter_(interrupter) {}

    // NOTE: need to define move constructor but it shouldn't be
    // executed at runtime.
    Adaptor(Adaptor&& that) {
      LOG(FATAL) << "moving after starting";
    }

    K_& k_;
    stout::borrowed_ref<Scheduler::Context> context_;
    Callback<void()>& interrupter_;

    std::tuple<
        std::variant<
            // NOTE: we use a dummy 'Undefined' in order to default
            // initialize the tuple. We don't use 'std::monostate'
            // because that's what we use for 'void' return types.
            Undefined,
            std::conditional_t<
                std::is_void_v<typename Eventuals_::template ValueFrom<void>>,
                std::monostate,
                typename Eventuals_::template ValueFrom<void>>,
            std::exception_ptr>...>
        values_;

    std::atomic<size_t> counter_ = sizeof...(Eventuals_);

    template <size_t index, typename Eventual>
    [[nodiscard]] auto BuildFiber(Eventual eventual) {
      auto k = Build(
          std::move(eventual)
          // NOTE: need to reschedule to initial context beforew we
          // call into continuation!
          | Reschedule(Reborrow(context_))
          | Terminal()
                .start([this](auto&&... value) {
                  using Value = typename Eventual::template ValueFrom<void>;
                  static_assert(std::is_void_v<Value> || sizeof...(value) == 1);
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
                    std::optional<std::exception_ptr> exception =
                        GetExceptionIfExists();

                    if (exception) {
                      try {
                        std::rethrow_exception(*exception);
                      } catch (const StoppedException&) {
                        k_.Stop();
                      } catch (...) {
                        k_.Fail(std::current_exception());
                      }
                    } else {
                      k_.Start(GetTupleOfValues());
                    }
                  }
                })
                .fail([this](auto&&... errors) {
                  std::get<index>(values_)
                      .template emplace<std::exception_ptr>(
                          make_exception_ptr_or_forward(
                              std::forward<decltype(errors)>(errors)...));
                  if (counter_.fetch_sub(1) == 1) {
                    // You're the last eventual so call the continuation.
                    std::optional<std::exception_ptr> exception =
                        GetExceptionIfExists();

                    CHECK(exception);
                    try {
                      std::rethrow_exception(*exception);
                    } catch (const StoppedException&) {
                      k_.Stop();
                    } catch (...) {
                      k_.Fail(std::current_exception());
                    }
                  } else {
                    // Interrupt the remaining eventuals so we can
                    // propagate the failure.
                    interrupter_();
                  }
                })
                .stop([this]() {
                  std::get<index>(values_)
                      .template emplace<std::exception_ptr>(
                          std::make_exception_ptr(
                              StoppedException()));
                  if (counter_.fetch_sub(1) == 1) {
                    // You're the last eventual so call the continuation.
                    std::optional<std::exception_ptr> exception =
                        GetExceptionIfExists();

                    CHECK(exception);
                    try {
                      std::rethrow_exception(*exception);
                    } catch (const StoppedException&) {
                      k_.Stop();
                    } catch (...) {
                      k_.Fail(std::current_exception());
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
            std::is_void_v<typename Eventuals_::template ValueFrom<void>>,
            std::monostate,
            typename Eventuals_::template ValueFrom<void>>...>
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

    std::optional<std::exception_ptr> GetExceptionIfExists() {
      std::optional<std::exception_ptr> exception;

      bool stopped = true;

      auto check_value = [&stopped, &exception](auto& value) {
        if (std::holds_alternative<std::exception_ptr>(value)) {
          try {
            std::rethrow_exception(std::get<std::exception_ptr>(value));
          } catch (const StoppedException&) {
          } catch (...) {
            stopped = false;
            exception.emplace(std::current_exception());
          }
        } else {
          stopped = false;
        }
      };

      std::apply(
          [&check_value](auto&... value) {
            (check_value(value), ...);
          },
          values_);

      if (stopped) {
        CHECK(!exception);
        exception = std::make_exception_ptr(eventuals::StoppedException{});
      }

      return exception;
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
      adaptor_.emplace(k_, Reborrow(Scheduler::Context::Get()), interrupter_);

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

      handler_->Install();
    }

    // NOTE: need to destruct the fibers LAST since they have a
    // Scheduler::Context which may get borrowed in 'adaptor_' and
    // it's continuations so those need to be destructed first.
    std::optional<
        decltype(std::declval<Adaptor<K_, Eventuals_...>>().BuildFibers(
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
    template <typename Arg>
    using ValueFrom = std::tuple<
        std::conditional_t<
            std::is_void_v<typename Eventuals_::template ValueFrom<void>>,
            std::monostate,
            typename Eventuals_::template ValueFrom<void>>...>;

    using Errors_ = tuple_types_union_all_t<
        typename Eventuals_::template ErrorsFrom<void, std::tuple<>>...>;

    template <typename Arg, typename Errors>
    using ErrorsFrom = tuple_types_union_t<Errors, Errors_>;

    template <typename Arg, typename K>
    auto k(K k) && {
      return Continuation<K, Eventuals_...>(
          std::move(k),
          std::move(eventuals_));
    }

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
