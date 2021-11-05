#pragma once

#include "eventuals/expected.h"
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
        k_.Start(Expected<Arg_>(std::forward<Args>(args)...));
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
            Expected<Arg_>(
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
            Expected<Arg_>(
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
            Expected<Arg>>;

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
