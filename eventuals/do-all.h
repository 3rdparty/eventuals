#include <atomic>
#include <tuple>
#include <variant>

#include "eventuals/compose.h"
#include "eventuals/terminal.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

struct _DoAll {
  template <typename K_, typename... Eventuals_>
  struct Adaptor {
    Adaptor(K_& k, Interrupt& interrupt)
      : k_(k),
        interrupt_(interrupt) {}

    Adaptor(Adaptor&& that)
      : k_(that.k_),
        interrupt_(that.interrupt_) {
      CHECK(that.counter_.load() == sizeof...(Eventuals_))
          << "moving after starting is illegal";
    }

    K_& k_;
    Interrupt& interrupt_;

    std::tuple<
        std::variant<
            std::conditional_t<
                std::is_void_v<typename Eventuals_::template ValueFrom<void>>,
                std::monostate,
                typename Eventuals_::template ValueFrom<void>>,
            std::exception_ptr>...>
        values_;

    std::atomic<size_t> counter_ = sizeof...(Eventuals_);

    template <size_t index, typename Eventual>
    auto BuildEventual(Eventual eventual) {
      return Build(
          std::move(eventual)
          | Terminal()
                .start([this](auto&&... value) {
                  if constexpr (sizeof...(value) != 0) {
                    std::get<index>(values_)
                        .template emplace<std::decay_t<decltype(value)>...>(
                            std::forward<decltype(value)>(value)...);
                  } else {
                    // Assume it's void, std::monostate will be the default.
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
                          std::make_exception_ptr(
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
                    interrupt_.Trigger();
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
                    interrupt_.Trigger();
                  }
                }));
    }

    std::tuple<
        std::conditional_t<
            std::is_void_v<typename Eventuals_::template ValueFrom<void>>,
            std::monostate,
            typename Eventuals_::template ValueFrom<void>>...>
    GetTupleOfValues() {
      return std::apply(
          [](auto&... value) {
            // NOTE: not using `CHECK_EQ()` here because compiler
            // messages out the error: expected expression
            //    (CHECK_EQ(0, value.index()), ...);
            //     ^
            // That's why we use CHECK instead.
            ((CHECK(value.index() == 0)), ...);
            return std::make_tuple(std::get<0>(value)...);
          },
          values_);
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
    auto BuildAll(
        std::tuple<Eventuals_...>&& eventuals,
        std::index_sequence<index...>) {
      return std::make_tuple(
          BuildEventual<index>(std::move(std::get<index>(eventuals)))...);
    }
  };

  template <typename K_, typename... Eventuals_>
  struct Continuation {
    Continuation(K_&& k, std::tuple<Eventuals_...>&& eventuals)
      : k_(std::move(k)),
        eventuals_(std::move(eventuals)) {}

    Continuation(Continuation&& that)
      : k_(std::move(that.k_)),
        eventuals_(std::move(that.eventuals_)),
        adaptor_(std::move(that.adaptor_)),
        ks_(std::move(that.ks_)),
        handler_(std::move(that.handler_)) {}

    template <typename... Args>
    void Start(Args&&...) {
      adaptor_.emplace(k_, interrupt_);

      ks_.emplace(adaptor_->BuildAll(
          std::move(eventuals_),
          std::make_index_sequence<sizeof...(Eventuals_)>{}));

      std::apply(
          [this](auto&... k) {
            (k.Register(interrupt_), ...);
            (k.Start(), ...);
          },
          ks_.value());
    }

    template <typename... Args>
    void Fail(Args&&... args) {
      // TODO: consider propagating through each eventual?
      k_.Fail(std::forward<Args>(args)...);
    }

    void Stop() {
      // TODO: consider propagating through each eventual?
      k_.Stop();
    }

    void Register(Interrupt& interrupt) {
      k_.Register(interrupt);

      handler_.emplace(&interrupt, [this]() {
        // Trigger inner interrupt for each eventual.
        interrupt_.Trigger();
      });
    }

    K_ k_;
    std::tuple<Eventuals_...> eventuals_;
    Interrupt interrupt_;

    std::optional<Adaptor<K_, Eventuals_...>> adaptor_;

    std::optional<
        decltype(adaptor_->BuildAll(
            std::move(eventuals_),
            std::make_index_sequence<sizeof...(Eventuals_)>{}))>
        ks_;

    std::optional<Interrupt::Handler> handler_;
  };

  template <typename... Eventuals_>
  struct Composable {
    template <typename Arg>
    using ValueFrom = std::tuple<
        std::conditional_t<
            std::is_void_v<typename Eventuals_::template ValueFrom<void>>,
            std::monostate,
            typename Eventuals_::template ValueFrom<void>>...>;

    template <typename Arg, typename K>
    auto k(K k) && {
      return Continuation<K, Eventuals_...>{
          std::move(k),
          std::move(eventuals_)};
    }

    std::tuple<Eventuals_...> eventuals_;
  };
};

////////////////////////////////////////////////////////////////////////

template <typename Eventual, typename... Eventuals>
auto DoAll(Eventual eventual, Eventuals... eventuals) {
  return _DoAll::Composable<Eventual, Eventuals...>{
      std::make_tuple(std::move(eventual), std::move(eventuals)...)};
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
