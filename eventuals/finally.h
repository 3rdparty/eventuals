#pragma once

#include "eventuals/expected.h"
#include "eventuals/terminal.h" // For 'StoppedException'.
#include "eventuals/then.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

struct _Finally final {
  template <typename K_, typename Arg_>
  struct Continuation final {
    template <typename... Args>
    void Start(Args&&... args) {
      if constexpr (std::is_void_v<Arg_>) {
        k_.Start(std::optional<std::exception_ptr>());
      } else {
        k_.Start(Expected::Of<Arg_>(std::forward<Args>(args)...));
      }
    }

    template <typename Error>
    void Fail(Error&& error) {
      if constexpr (std::is_void_v<Arg_>) {
        k_.Start(
            std::optional<std::exception_ptr>(
                make_exception_ptr_or_forward(
                    std::move(error))));
      } else {
        k_.Start(
            Expected::Of<Arg_>(
                make_exception_ptr_or_forward(
                    std::move(error))));
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
            Expected::Of<Arg_>(
                std::make_exception_ptr(
                    StoppedException())));
      }
    }

    void Register(Interrupt& interrupt) {
      k_.Register(interrupt);
    }

    K_ k_;
  };

  struct Composable final {
    template <typename Arg>
    using ValueFrom =
        std::conditional_t<
            std::is_void_v<Arg>,
            std::optional<std::exception_ptr>,
            Expected::Of<Arg>>;

    template <typename Arg, typename K>
    auto k(K k) && {
      return Continuation<K, Arg>{std::move(k)};
    }
  };
};

////////////////////////////////////////////////////////////////////////

template <typename F>
auto Finally(F f) {
  return _Finally::Composable()
      | Then(std::move(f));
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
