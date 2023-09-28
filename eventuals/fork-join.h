#pragma once

#include <atomic>
#include <optional>
#include <variant>
#include <vector>

#include "eventuals/compose.h"
#include "eventuals/scheduler.h"
#include "eventuals/terminal.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

// The following are used in `Composable` (below) and could logically be scoped
// inside of it, but since this does template specialization these definitions
// must be at namespace-scope for `gcc` to be willing to compile this file.
// Otherwise we'll get the error "Explicit specialization in non-namespace
// scope".
template <typename F_, typename Arg>
struct F_invoke_result_ : std::invoke_result<F_, size_t, Arg&> {};

template <typename F_>
struct F_invoke_result_<F_, void> : std::invoke_result<F_, size_t> {};

////////////////////////////////////////////////////////////////////////

struct _ForkJoin final {
  // We need to run each eventual created from the callable passed to
  // 'ForkJoin()' with it's own 'Scheduler::Context' so that it can be
  // blocked, e.g., on synchronization, get interrupted, etc. We
  // abstract that into a "fiber" similar to other constructs that
  // require separate and independent contexts.
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

  template <typename K_, typename F_, typename Value_, typename... Arg_>
  struct Adaptor final {
    using StoppedOrError = variant_of_type_and_tuple_t<
        Stopped,
        typename std::invoke_result_t<
            F_,
            size_t,
            Arg_&...>::template ErrorsFrom<void, std::tuple<>>>;

    Adaptor(
        K_& k,
        unsigned int forks,
        F_& f,
        stout::borrowed_ref<Scheduler::Context>&& previous,
        Callback<void()>& interrupter)
      : k_(k),
        forks_(forks),
        f_(f),
        previous_(std::move(previous)),
        interrupter_(interrupter) {
      values_.reserve(forks_);
      for (size_t i = 0; i < forks_; i++) {
        values_.emplace_back();
      }
    }

    // NOTE: need to define move constructor but it shouldn't be
    // executed at runtime.
    Adaptor(Adaptor&& that) {
      LOG(FATAL) << "moving after starting";
    }

    K_& k_;
    unsigned int forks_;
    F_& f_;
    stout::borrowed_ref<Scheduler::Context> previous_;
    Callback<void()>& interrupter_;

    std::vector<
        std::variant<
            // NOTE: we use a dummy 'Undefined' in order to default
            // initialize each value. We don't use 'std::monostate'
            // because that's what we use for 'void' return types.
            Undefined,
            std::conditional_t<
                std::is_void_v<Value_>,
                std::monostate,
                Value_>,
            StoppedOrError>>
        values_;

    static constexpr int UNDEFINED_INDEX = 0;
    static constexpr int VALUE_OR_VOID_INDEX = 1;
    static constexpr int STOPPED_OR_ERROR_INDEX = 2;

    std::atomic<size_t> counter_ = forks_;

    template <typename... Args>
    [[nodiscard]] auto BuildFiber(size_t index, Args&... args) {
      auto k = Build(
          f_(index, args...)
          // NOTE: need to reschedule to previous context before we
          // call into continuation!
          >> Reschedule(previous_.reborrow())
          >> Terminal()
                 .start([this, index](auto&&... value) {
                   static_assert(
                       std::is_void_v<Value_> || sizeof...(value) == 1);
                   if constexpr (!std::is_void_v<Value_>) {
                     values_[index]
                         .template emplace<std::decay_t<decltype(value)>...>(
                             std::forward<decltype(value)>(value)...);
                   } else {
                     values_[index]
                         .template emplace<std::monostate>();
                   }
                   if (counter_.fetch_sub(1) == 1) {
                     // You're the last eventual so call the continuation.
                     std::optional<StoppedOrError> stopped_or_error =
                         GetStoppedOrErrorIfExists();

                     if (stopped_or_error) {
                       std::visit(
                           [this](auto&& stopped_or_error) {
                             if constexpr (
                                 std::is_same_v<
                                     std::decay_t<decltype(stopped_or_error)>,
                                     Stopped>) {
                               k_.Stop();
                             } else {
                               k_.Fail(
                                   std::forward<
                                       decltype(stopped_or_error)>(
                                       stopped_or_error));
                             }
                           },
                           std::move(stopped_or_error.value()));
                     } else {
                       k_.Start(GetVectorOfValues());
                     }
                   }
                 })
                 .fail([this, index](auto&&... errors) {
                   values_[index]
                       .template emplace<STOPPED_OR_ERROR_INDEX>(
                           std::forward<decltype(errors)>(errors)...);

                   if (counter_.fetch_sub(1) == 1) {
                     // You're the last eventual so call the continuation.
                     std::optional<StoppedOrError> stopped_or_error =
                         GetStoppedOrErrorIfExists();

                     CHECK(stopped_or_error);
                     std::visit(
                         [this](auto&& stopped_or_error) {
                           if constexpr (
                               std::is_same_v<
                                   std::decay_t<decltype(stopped_or_error)>,
                                   Stopped>) {
                             k_.Stop();
                           } else {
                             k_.Fail(
                                 std::forward<
                                     decltype(stopped_or_error)>(
                                     stopped_or_error));
                           }
                         },
                         std::move(stopped_or_error.value()));
                   } else {
                     // Interrupt the remaining eventuals so we can
                     // propagate the failure.
                     interrupter_();
                   }
                 })
                 .stop([this, index]() {
                   values_[index]
                       .template emplace<STOPPED_OR_ERROR_INDEX>(Stopped());

                   if (counter_.fetch_sub(1) == 1) {
                     // You're the last eventual so call the continuation.
                     std::optional<StoppedOrError> stopped_or_error =
                         GetStoppedOrErrorIfExists();

                     CHECK(stopped_or_error);
                     std::visit(
                         [this](auto&& stopped_or_error) {
                           if constexpr (
                               std::is_same_v<
                                   std::decay_t<decltype(stopped_or_error)>,
                                   Stopped>) {
                             k_.Stop();
                           } else {
                             k_.Fail(
                                 std::forward<
                                     decltype(stopped_or_error)>(
                                     stopped_or_error));
                           }
                         },
                         std::move(stopped_or_error.value()));
                   } else {
                     // Interrupt the remaining eventuals so we can
                     // propagate the stop.
                     interrupter_();
                   }
                 }));
      return Fiber<decltype(k)>(std::move(k));
    }

    std::vector<
        std::conditional_t<
            std::is_void_v<Value_>,
            std::monostate,
            Value_>>
    GetVectorOfValues() {
      std::vector<
          std::conditional_t<
              std::is_void_v<Value_>,
              std::monostate,
              Value_>>
          values;
      values.reserve(forks_);
      for (auto& value : values_) {
        CHECK_EQ(value.index(), 1);
        values.push_back(std::get<1>(std::move(value)));
      }
      return values;
    }

    std::optional<StoppedOrError> GetStoppedOrErrorIfExists() {
      std::optional<StoppedOrError> stopped_or_error;

      for (auto& value : values_) {
        if (value.index() == STOPPED_OR_ERROR_INDEX) {
          if (!stopped_or_error.has_value()
              || !std::holds_alternative<Stopped>(stopped_or_error.value())) {
            stopped_or_error.emplace(std::get<STOPPED_OR_ERROR_INDEX>(value));
          } else if (
              stopped_or_error.has_value()
              && std::holds_alternative<Stopped>(stopped_or_error.value())) {
            // NOTE: we prefer propagate 'Stopped' to any of errors.
            break;
          }
        }
      }

      return stopped_or_error;
    }
  };

  template <typename K_, typename F_, typename Value_, typename... Arg_>
  struct Continuation final {
    Continuation(K_ k, std::string&& name, unsigned int forks, F_ f)
      : name_(std::move(name)),
        forks_(forks),
        f_(std::move(f)),
        k_(std::move(k)) {}

    Continuation(Continuation&& that)
      : name_(std::move(that.name_)),
        forks_(that.forks_),
        f_(std::move(that.f_)),
        k_(std::move(that.k_)) {
      CHECK(!adaptor_) << "moving after starting";
    }

    template <typename... Args>
    void Start(Args&&... args) {
      if (handler_.has_value() && !handler_->Install()) {
        // TODO: consider propagating through each eventual?
        k_.Stop();
      } else {
        adaptor_.emplace(
            k_,
            forks_,
            f_,
            Scheduler::Context::Get().reborrow(),
            interrupter_);

        fibers_.reserve(forks_);

        for (size_t index = 0; index < forks_; index++) {
          auto& fiber = fibers_.emplace_back(
              adaptor_->BuildFiber(index, args...));

          // Clone the current scheduler context for running the eventual.
          fiber.context.emplace(
              Scheduler::Context::Get()->name()
              + " [ForkJoin - " + name_ + " - " + std::to_string(index) + "]");

          fiber.context->scheduler()->Submit(
              [&]() {
                CHECK_EQ(
                    &fiber.context.value(),
                    Scheduler::Context::Get().get());
                fiber.k.Register(fiber.interrupt);
                fiber.k.Start();
              },
              fiber.context.value());
        }
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

    std::string name_;
    unsigned int forks_;
    F_ f_;

    // NOTE: need to destruct the fibers LAST since they have a
    // Scheduler::Context which may get borrowed in 'adaptor_' and
    // it's continuations so those need to be destructed first.
    std::vector<
        decltype(std::declval<Adaptor<K_, F_, Value_, Arg_...>>()
                     .BuildFiber(0, std::declval<Arg_&>()...))>
        fibers_;

    std::optional<Adaptor<K_, F_, Value_, Arg_...>> adaptor_;

    Callback<void()> interrupter_ = [this]() {
      // Trigger inner interrupt for each eventual.
      for (auto& fiber : fibers_) {
        fiber.interrupt.Trigger();
      };
    };

    std::optional<Interrupt::Handler> handler_;

    // NOTE: we store 'k_' as the _last_ member so it will be
    // destructed _first_ and thus we won't have any use-after-delete
    // issues during destruction of 'k_' if it holds any references or
    // pointers to any (or within any) of the above members.
    K_ k_;
  };

  template <typename F_>
  struct Composable final {
    template <typename Arg>
    using E_ = typename F_invoke_result_<F_, Arg>::type;

    template <typename Arg>
    using Value_ = typename E_<Arg>::template ValueFrom<void, std::tuple<>>;

    template <typename Arg, typename Errors>
    using ValueFrom = std::vector<
        std::conditional_t<
            std::is_void_v<Value_<Arg>>,
            std::monostate,
            Value_<Arg>>>;

    template <typename Arg, typename Errors>
    using ErrorsFrom = tuple_types_union_t<
        Errors,
        typename E_<Arg>::template ErrorsFrom<void, std::tuple<>>>;

    template <typename Arg, typename Errors, typename K>
    auto k(K k) && {
      if constexpr (std::is_void_v<Arg>) {
        static_assert(
            std::is_invocable_v<F_, size_t>,
            "'ForkJoin' expects callable that takes an unsigned index");

        return Continuation<K, F_, Value_<Arg>>(
            std::move(k),
            std::move(name_),
            forks_,
            std::move(f_));
      } else {
        static_assert(
            std::is_invocable_v<F_, size_t, Arg&>,
            "'ForkJoin' expects callable that takes an "
            "unsigned index and the upstream value");

        return Continuation<K, F_, Value_<Arg>, Arg>(
            std::move(k),
            std::move(name_),
            forks_,
            std::move(f_));
      }
    }

    template <typename Downstream>
    static constexpr bool CanCompose = Downstream::ExpectsValue;

    using Expects = SingleValue;

    std::string name_;
    unsigned int forks_;
    F_ f_;
  };
};

////////////////////////////////////////////////////////////////////////

template <typename F>
[[nodiscard]] auto ForkJoin(const std::string& name, unsigned int forks, F f) {
  static_assert(
      !HasValueFrom<F>::value,
      "'ForkJoin' expects a callable (e.g., a lambda) not an eventual");

  return _ForkJoin::Composable<F>{name, forks, std::move(f)};
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
