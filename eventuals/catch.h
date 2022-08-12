#pragma once

#include <memory> // For 'std::unique_ptr'.
#include <optional>
#include <tuple>

#include "eventuals/terminal.h"
#include "eventuals/then.h"
#include "eventuals/type-traits.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

struct _Catch final {
  template <typename K_, typename Error_, typename F_>
  struct Handler final {
    using Error = Error_;

    Handler(F_ f)
      : f_(std::move(f)) {}

    template <typename E>
    void Handle(K_&& k, Interrupt* interrupt, E&& e) {
      adapted_.emplace(Then(std::move(f_)).template k<Error_>(std::move(k)));

      if (interrupt != nullptr) {
        adapted_->Register(*interrupt);
      }

      adapted_->Start(std::forward<E>(e));
    }

    template <typename E>
    // NOTE: we're _must_ explicitly take 'k' and 'e' by rvalue reference here
    // because we do _not_ want the compiler to move the 'k' and 'e' when
    // calling this function in the event that we _don't_ end up handling the
    // error.
    bool TryHandle(K_&& k, Interrupt* interrupt, E&& e) {
      // When 'Error_' is 'std::exception_ptr' it indicates we're the
      // 'all' handler which catches everything and makes a
      // 'std::exception_ptr' if we don't already have one.
      if constexpr (std::is_same_v<Error_, std::exception_ptr>) {
        Handle(
            std::move(k),
            interrupt,
            make_exception_ptr_or_forward(std::forward<E>(e)));
        return true;
      } else if constexpr (std::disjunction_v<
                               std::is_same<Error_, E>,
                               std::is_base_of<Error_, E>>) {
        Handle(std::move(k), interrupt, std::forward<E>(e));
        return true;
      } else if constexpr (std::is_same_v<E, std::exception_ptr>) {
        try {
          std::rethrow_exception(e);
        }
        // Catch by reference because if 'Error_' is a polymorphic
        // type it can't be caught by value.
        catch (Error_& error) {
          Handle(std::move(k), interrupt, std::move(error));
          return true;
        } catch (...) {
          return false;
        }
      } else {
        // Just to avoid '-Werror=unused-but-set-parameter' warning.
        (void) interrupt;
        return false;
      }
    }

    F_ f_;

    using Adapted_ =
        decltype(Then(std::move(f_)).template k<Error_>(std::declval<K_>()));

    std::optional<Adapted_> adapted_;
  };

  // Helper used by the 'Builder' below so that the compiler doesn't
  // try and use 'Undefined' as 'K_' for things like 'Adapted_' above
  // which will cause compilation errors because 'Undefined' is not a
  // valid continuation!
  template <typename Error_, typename F_>
  struct Handler<Undefined, Error_, F_> final {
    using Error = Error_;

    Handler(F_ f)
      : f_(std::move(f)) {}

    // Helper to convert a catch handler to a new 'K'.
    template <typename K>
    auto Convert() && {
      return Handler<K, Error_, F_>{std::move(f_)};
    }

    F_ f_;
  };

  ////////////////////////////////////////////////////////////////////////

  template <typename K_, typename... CatchHandlers_>
  struct Continuation final {
    Continuation(K_ k, std::tuple<CatchHandlers_...>&& catch_handlers)
      : catch_handlers_(std::move(catch_handlers)),
        k_(std::move(k)) {}

    template <typename... Args>
    void Start(Args&&... args) {
      if (interrupt_ != nullptr) {
        k_.Register(*interrupt_);
      }

      k_.Start(std::forward<Args>(args)...);
    }

    template <typename Error>
    void Fail(Error&& error) {
      static_assert(
          std::disjunction_v<
              std::is_base_of<std::exception, std::decay_t<Error>>,
              std::is_same<std::exception_ptr, std::decay_t<Error>>>,
          "'Catch' expects a type derived from "
          "std::exception or a std::exception_ptr");

      std::apply(
          [&](auto&... catch_handler) {
            // Using a fold expression to simplify the iteration.
            ([&](auto& catch_handler) {
              if (!handled_) {
                handled_ = catch_handler.TryHandle(
                    std::move(k_),
                    interrupt_,
                    std::forward<Error>(error));
              }
            }(catch_handler),
             ...);
          },
          catch_handlers_);

      if (!handled_) {
        if (interrupt_ != nullptr) {
          k_.Register(*interrupt_);
        }

        k_.Fail(std::forward<Error>(error));
      }
    }

    void Stop() {
      if (interrupt_ != nullptr) {
        k_.Register(*interrupt_);
      }

      k_.Stop();
    }

    void Register(Interrupt& interrupt) {
      CHECK_EQ(interrupt_, nullptr);
      interrupt_ = &interrupt;

      // NOTE: we defer registering the interrupt with 'k_' in case on
      // of our handlers handles an error and we 'std::move(k_)' into
      // the handler.
    }

    std::tuple<CatchHandlers_...> catch_handlers_;

    bool handled_ = false;

    Interrupt* interrupt_ = nullptr;

    // NOTE: we store 'k_' as the _last_ member so it will be
    // destructed _first_ and thus we won't have any use-after-delete
    // issues during destruction of 'k_' if it holds any references or
    // pointers to any (or within any) of the above members.
    K_ k_;
  };

  template <typename Value_, bool has_all_, typename... CatchHandlers_>
  struct Builder final {
    // NOTE: we ensure 'Arg' and 'Value_' are the same in 'k()'.
    template <typename Arg>
    using ValueFrom = Arg;

    using CatchErrors_ = std::tuple<typename CatchHandlers_::Error...>;

    template <typename Arg, typename Errors>
    using ErrorsFrom = std::conditional_t<
        std::disjunction_v<
            // If 'std::exception' is caught
            // then all exceptions will be caught.
            tuple_types_contains<std::exception, CatchErrors_>,
            // A 'std::exception_ptr' implies we have a '.all(...)'
            // and all exceptions are caught.
            tuple_types_contains<std::exception_ptr, CatchErrors_>>,
        std::tuple<>,
        tuple_types_subtract_t<Errors, CatchErrors_>>;

    template <typename Left, typename Right>
    using Unify_ = typename std::conditional_t<
        std::is_same_v<Left, Right>,
        type_identity<Left>,
        std::conditional_t<
            std::is_void_v<Left>,
            type_identity<Right>,
            std::enable_if<std::is_void_v<Right>, Left>>>::type;

    template <typename Downstream>
    static constexpr bool CanCompose = Downstream::ExpectsValue;

    using Expects = SingleValue;

    template <typename Value, bool has_all, typename... CatchHandlers>
    static auto create(std::tuple<CatchHandlers...>&& catch_handlers) {
      return Builder<Value, has_all, CatchHandlers...>{
          std::move(catch_handlers)};
    }

    template <typename Arg, typename K>
    auto k(K k) && {
      static_assert(
          sizeof...(CatchHandlers_) > 0,
          "No handlers were specified for 'Catch'");

      static_assert(
          std::is_same_v<Arg, Value_>,
          "Catch handlers must return an eventual value of the same "
          "type as passed from upstream");

      // Convert each catch handler to one with 'K'
      // and then return 'Continuation'.
      return std::apply(
          [&](auto&&... catch_handler) {
            return Continuation<
                K,
                decltype(std::move(catch_handler).template Convert<K>())...>(
                std::move(k),
                std::tuple{std::move(catch_handler).template Convert<K>()...});
          },
          std::move(catch_handlers_));
    }

    template <typename Error, typename F>
    auto raised(F f) {
      static_assert(!has_all_, "'all' handler must be installed last");

      static_assert(
          !std::is_same_v<Error, std::exception_ptr>,
          "Only the 'all' handler catches 'std::exception_ptr'");

      static_assert(
          std::is_invocable_v<F, Error>,
          "Catch handler can not be invoked with your specified error");

      using Value = ValueFromMaybeComposable<
          std::invoke_result_t<F, Error>,
          void>;

      static_assert(
          std::disjunction_v<
              std::is_same<Value, Value_>,
              std::is_void<Value>,
              std::is_void<Value_>>,
          "Catch handlers do not return an eventual value of the same type");

      return create<Unify_<Value_, Value>, has_all_>(
          std::tuple_cat(
              std::move(catch_handlers_),
              std::tuple{Handler<Undefined, Error, F>{std::move(f)}}));
    }

    template <typename F>
    auto all(F f) {
      static_assert(!has_all_, "Duplicate 'all'");

      static_assert(
          std::is_invocable_v<F, std::exception_ptr>,
          "Catch 'all' handler must be invocable with 'std::exception_ptr'");

      using Value = ValueFromMaybeComposable<
          std::invoke_result_t<F, std::exception_ptr>,
          void>;

      static_assert(
          std::disjunction_v<
              std::is_same<Value, Value_>,
              std::is_void<Value>,
              std::is_void<Value_>>,
          "Catch handlers do not return an eventual value of the same type");

      return create<Unify_<Value_, Value>, true>(
          std::tuple_cat(
              std::move(catch_handlers_),
              std::tuple{
                  Handler<Undefined, std::exception_ptr, F>{std::move(f)}}));
    }

    std::tuple<CatchHandlers_...> catch_handlers_;
  };
};

////////////////////////////////////////////////////////////////////////

[[nodiscard]] inline auto Catch() {
  return _Catch::Builder<void, false>{};
}

template <typename F>
[[nodiscard]] auto Catch(F f) {
  return Catch().all(std::move(f));
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
