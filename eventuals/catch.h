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
    using F = F_;

    Handler(F_ f)
      : f_(std::move(f)) {}

    template <typename E>
    void Handle(K_&& k, Interrupt* interrupt, E&& e) {
      adapted_.emplace(
          Then(
              std::move(f_))
              .template k<Error_, std::tuple<>>(std::move(k)));

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
      // When 'Error_' is a variant it indicates we're the
      // 'all' handler which catches everything.
      if constexpr (is_variant_v<Error_>) {
        Handle(
            std::move(k),
            interrupt,
            // Since we are in 'all' handler, we want to be sure that it is
            // invoked with a 'std::variant', so need to create a variant
            // from the propagated error 'E'.
            Error_(std::forward<E>(e)));
        return true;
      } else if constexpr (std::disjunction_v<
                               std::is_same<Error_, E>,
                               std::is_base_of<Error_, E>>) {
        Handle(std::move(k), interrupt, std::forward<E>(e));
        return true;
      } else {
        // Just to avoid '-Werror=unused-but-set-parameter' warning.
        (void) interrupt;
        return false;
      }
    }

    F_ f_;

    using Adapted_ =
        decltype(Then(std::move(f_))
                     .template k<Error_, std::tuple<>>(std::declval<K_>()));

    std::optional<Adapted_> adapted_;
  };

  // Helper used by the 'Builder' below so that the compiler doesn't
  // try and use 'Undefined' as 'K_' for things like 'Adapted_' above
  // which will cause compilation errors because 'Undefined' is not a
  // valid continuation!
  template <typename Error_, typename F_>
  struct Handler<Undefined, Error_, F_> final {
    using Error = Error_;
    using F = F_;

    Handler(F_ f)
      : f_(std::move(f)) {}

    // Helper to convert a catch handler to a new 'K'.
    template <typename K, typename Errors, typename Catches>
    auto Convert() && {
      using AllHandlerArgument = apply_tuple_types_t<
          std::variant,
          tuple_types_subtract_t<Errors, Catches>>;

      return Handler<
          K,
          std::conditional_t<
              std::is_same_v<Error_, std::monostate>,
              AllHandlerArgument,
              Error>,
          F_>{std::move(f_)};
    }

    F_ f_;
  };

  ////////////////////////////////////////////////////////////////////////

  template <typename K_, bool has_all_, typename... CatchHandlers_>
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
          check_errors_v<std::decay_t<Error>>,
          "'Catch' expects a type derived from std::exception");

      // NOTE: we need to put 'handled' on the stack because if 'this'
      // was constructed on the heap it's possible that 'TryHandle()'
      // below will end up causing 'this' to get destructed in which
      // case we wouldn't be able to write to 'handled' if it was part
      // of the heap allocation.
      bool handled = false;

      std::apply(
          [&](auto&... catch_handler) {
            // Using a fold expression to simplify the iteration.
            ([&](auto& catch_handler) {
              if (!handled) {
                handled = catch_handler.TryHandle(
                    std::move(k_),
                    interrupt_,
                    std::forward<Error>(error));
              }
            }(catch_handler),
             ...);
          },
          catch_handlers_);

      using Catches =
          std::tuple<typename CatchHandlers_::Error...>;

      // If 'Error' is part of 'Catches_' or 'all' handler is specified,
      // then we will never propagate fail with 'k_.Fail()' even if handled
      // is false, but since 'handled' is a runtime value the compiler will
      // assume that it's possible that we can call 'k_.Fail()', with what ever
      // 'Error' type so to keep the compiler from trying to compile that
      // code path we need to add the following 'if constexpr'.
      if constexpr (!std::disjunction_v<
                        tuple_types_contains_subtype<
                            std::decay_t<Error>,
                            Catches>,
                        std::conditional_t<
                            has_all_,
                            std::true_type,
                            std::false_type>>) {
        if (!handled) {
          if (interrupt_ != nullptr) {
            k_.Register(*interrupt_);
          }

          k_.Fail(std::forward<Error>(error));
        }
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
    template <typename Arg, typename Errors>
    using ValueFrom = Arg;

    using Catches_ = tuple_types_subtract_t<
        std::tuple<typename CatchHandlers_::Error...>,
        std::tuple<std::monostate>>;

    template <typename Arg, typename Errors>
    using ErrorsFrom = std::conditional_t<
        std::disjunction_v<
            // If 'std::exception' is caught
            // then all exceptions will be caught.
            tuple_types_contains<std::exception, Catches_>,
            // If we have a '.all(...)' handler then
            // all exceptions are caught.
            std::conditional_t<has_all_, std::true_type, std::false_type>>,
        std::tuple<>,
        tuple_types_subtract_t<Errors, Catches_>>;

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

    template <typename Arg, typename Errors, typename K>
    auto k(K k) && {
      static_assert(
          sizeof...(CatchHandlers_) > 0,
          "No handlers were specified for 'Catch'");

      if constexpr (has_all_) {
        using AllHandler =
            std::decay_t<
                decltype(std::get<
                         std::tuple_size_v<
                             decltype(catch_handlers_)> - 1>(
                    catch_handlers_))>;

        using AllHandlerArgument =
            apply_tuple_types_t<
                std::variant,
                tuple_types_subtract_t<Errors, Catches_>>;

        using Value = ValueFromMaybeComposable<
            std::invoke_result_t<
                typename AllHandler::F,
                AllHandlerArgument>,
            void,
            std::tuple<>>;

        static_assert(
            std::is_same_v<Arg, Value>,
            "Catch().all() handler must return an eventual value of the same "
            "type as passed from upstream");
      }

      // Convert each catch handler to one with 'K'
      // and then return 'Continuation'.
      return std::apply(
          [&](auto&&... catch_handler) {
            return Continuation<
                K,
                has_all_,
                decltype(std::move(
                             catch_handler)
                             .template Convert<K, Errors, Catches_>())...>(
                std::move(k),
                std::tuple{std::move(
                               catch_handler)
                               .template Convert<K, Errors, Catches_>()...});
          },
          std::move(catch_handlers_));
    }

    template <typename Error, typename F>
    auto raised(F f) {
      static_assert(!has_all_, "'all' handler must be installed last");

      static_assert(
          std::is_invocable_v<F, Error>,
          "Catch handler can not be invoked with your specified error");

      using Value = ValueFromMaybeComposable<
          std::invoke_result_t<F, Error>,
          void,
          std::tuple<>>;

      static_assert(
          std::disjunction_v<
              std::is_same<Value, Value_>,
              std::is_void<Value>,
              std::is_void<Value_>>,
          "Catch handlers do not return an eventual value of the same type");

      return create<Unify_<Value_, Value>, has_all_>(
          std::tuple_cat(
              std::move(catch_handlers_),
              std::tuple{
                  Handler<Undefined, Error, F>{std::move(f)}}));
    }

    template <typename F>
    auto all(F f) {
      static_assert(!has_all_, "Duplicate 'all'");

      static_assert(
          !tuple_types_contains_v<std::exception, Catches_>,
          "You already have a handler that catches 'std::exception' "
          "so your '.all()' handler is redundant");

      return create<Value_, true>(
          std::tuple_cat(
              std::move(catch_handlers_),
              std::tuple{
                  Handler<Undefined, std::monostate, F>{std::move(f)}}));
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
