#include <atomic>
#include <tuple>
#include <variant>

#include "eventuals/compose.h"
#include "eventuals/terminal.h"

namespace eventuals {
namespace detail {

struct _DoAll {
  template <typename K_, typename... Eventuals_>
  struct Adaptor {
    Adaptor(K_& k)
      : k_(k) {}
    Adaptor(Adaptor&& that)
      : k_(that.k_) {
      CHECK(that.counter_.load() == sizeof...(Eventuals_))
          << "moving after starting is illegal";
    }

    K_& k_;

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
                    k_.Start(std::move(values_));
                  }
                })
                .fail([this](auto&&... errors) {
                  std::get<index>(values_)
                      .template emplace<std::exception_ptr>(
                          std::make_exception_ptr(
                              std::forward<decltype(errors)>(errors)...));
                  if (counter_.fetch_sub(1) == 1) {
                    // You're the last eventual so call the continuation.
                    k_.Start(std::move(values_));
                  }
                })
                .stop([this]() {
                  std::get<index>(values_)
                      .template emplace<std::exception_ptr>(
                          std::make_exception_ptr(
                              StoppedException()));
                  if (counter_.fetch_sub(1) == 1) {
                    // You're the last eventual so call the continuation.
                    k_.Start(std::move(values_));
                  }
                }));
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
    template <typename... Args>
    void Start(Args&&...) {
      adaptor_.emplace(k_);

      ks_.emplace(adaptor_->BuildAll(
          std::move(eventuals_),
          std::make_index_sequence<sizeof...(Eventuals_)>{}));

      std::apply(
          [this](auto&... k) {
            if (interrupt_ != nullptr) {
              (k.Register(*interrupt_), ...);
            }
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
      interrupt_ = &interrupt;
      k_.Register(interrupt);
    }

    K_ k_;
    std::tuple<Eventuals_...> eventuals_;

    std::optional<Adaptor<K_, Eventuals_...>> adaptor_;

    std::optional<
        decltype(adaptor_->BuildAll(
            std::move(eventuals_),
            std::make_index_sequence<sizeof...(Eventuals_)>{}))>
        ks_;

    Interrupt* interrupt_ = nullptr;
  };

  template <typename... Eventuals_>
  struct Composable {
    template <typename Arg>
    using ValueFrom =
        std::tuple<
            std::variant<
                std::conditional_t<
                    std::is_void_v<
                        typename Eventuals_::template ValueFrom<void>>,
                    std::monostate,
                    typename Eventuals_::template ValueFrom<void>>,
                std::exception_ptr>...>;

    template <typename Arg, typename K>
    auto k(K k) && {
      return Continuation<K, Eventuals_...>{
          std::move(k),
          std::move(eventuals_)};
    }

    std::tuple<Eventuals_...> eventuals_;
  };
};

} // namespace detail

template <typename Eventual, typename... Eventuals>
auto DoAll(Eventual eventual, Eventuals... eventuals) {
  return detail::_DoAll::Composable<Eventual, Eventuals...>{
      std::make_tuple(std::move(eventual), std::move(eventuals)...)};
}

} // namespace eventuals
