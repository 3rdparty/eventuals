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
      k_.Start(
          expected<Arg_, std::exception_ptr>(
              std::forward<Args>(args)...));
    }

    template <typename Error>
    void Fail(Error&& error) {
      k_.Start(
          expected<Arg_, std::exception_ptr>(
              make_unexpected(
                  std::make_exception_ptr(
                      std::forward<Error>(error)))));
    }

    void Stop() {
      k_.Start(
          expected<Arg_, std::exception_ptr>(
              make_unexpected(
                  std::make_exception_ptr(
                      StoppedException()))));
    }

    void Register(Interrupt& interrupt) {
      k_.Register(interrupt);
    }

    K_ k_;
  };

  struct Composable final {
    template <typename Arg>
    using ValueFrom = expected<Arg, std::exception_ptr>;

    template <typename Arg, typename Errors>
    using ErrorsFrom = std::tuple<>;

    template <typename Arg, typename K>
    auto k(K k) && {
      return Continuation<K, Arg>{std::move(k)};
    }

    template <typename Downstream>
    static constexpr bool CanCompose = Downstream::ExpectsValue;

    using Expects = SingleValue;
  };
};

////////////////////////////////////////////////////////////////////////

template <typename F>
[[nodiscard]] auto Finally(F f) {
  return _Finally::Composable()
      >> Then(std::move(f));
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
