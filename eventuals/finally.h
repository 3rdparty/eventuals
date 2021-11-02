#pragma once

#include <variant>

#include "eventuals/terminal.h" // For 'StoppedException'.
#include "eventuals/then.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

namespace detail {

////////////////////////////////////////////////////////////////////////

struct _Finally {
  template <typename K_, typename Arg_>
  struct Continuation {
    template <typename... Args>
    void Start(Args&&... args) {
      if constexpr (std::is_void_v<Arg_>) {
        k_.Start(std::optional<std::exception_ptr>());
      } else {
        k_.Start(
            std::variant<Arg_, std::exception_ptr>(
                std::in_place_index<0>,
                std::forward<Args>(args)...));
      }
    }

    template <typename... Args>
    void Fail(Args&&... args) {
      if constexpr (std::is_void_v<Arg_>) {
        k_.Start(
            std::optional<std::exception_ptr>(
                std::make_exception_ptr(
                    std::forward<Args>(args)...)));
      } else {
        k_.Start(
            std::variant<Arg_, std::exception_ptr>(
                std::in_place_index<1>,
                std::make_exception_ptr(
                    std::forward<Args>(args)...)));
      }
    }

    void Stop() {
      if constexpr (std::is_void_v<Arg_>) {
        k_.Start(
            std::optional<std::exception_ptr>(
                std::make_exception_ptr(
                    StoppedException())));
      } else {
        k_.Start(
            std::variant<Arg_, std::exception_ptr>(
                std::in_place_index<1>,
                std::make_exception_ptr(
                    StoppedException())));
      }
    }

    void Register(Interrupt& interrupt) {
      k_.Register(interrupt);
    }

    K_ k_;
  };

  struct Composable {
    template <typename Arg>
    using ValueFrom =
        std::conditional_t<
            std::is_void_v<Arg>,
            std::optional<std::exception_ptr>,
            std::variant<Arg, std::exception_ptr>>;

    template <typename Arg, typename K>
    auto k(K k) && {
      return Continuation<K, Arg>{std::move(k)};
    }
  };
};

////////////////////////////////////////////////////////////////////////

} // namespace detail

////////////////////////////////////////////////////////////////////////

template <typename F>
auto Finally(F f) {
  return detail::_Finally::Composable()
      | Then(std::move(f));
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
