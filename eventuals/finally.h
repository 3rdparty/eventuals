#pragma once

#include "eventuals/expected.h"
#include "eventuals/terminal.h" // For 'Stopped'.
#include "eventuals/then.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

struct _Finally final {
  template <typename K_, typename Arg_, typename Errors_>
  struct Continuation final {
    template <typename... Args>
    void Start(Args&&... args) {
      k_.Start(
          expected<Arg_, Errors_>(
              std::forward<Args>(args)...));
    }

    template <typename Error>
    void Fail(Error&& error) {
      k_.Start(
          expected<Arg_, Errors_>(
              make_unexpected(
                  std::forward<Error>(error))));
    }

    void Stop() {
      k_.Start(
          expected<Arg_, Errors_>(
              make_unexpected(
                  Stopped())));
    }

    void Register(Interrupt& interrupt) {
      k_.Register(interrupt);
    }

    K_ k_;
  };

  struct Composable final {
    template <typename...>
    struct AddStopped {};

    template <typename... Errors>
    struct AddStopped<std::variant<Errors...>> {
      using type = std::variant<Stopped, Errors...>;
    };

    template <typename Arg, typename Errors>
    using ValueFrom = expected<
        Arg,
        typename AddStopped<typename TupleToVariant<Errors>::type>::type>;

    template <typename Arg, typename Errors>
    using ErrorsFrom = std::tuple<>;

    template <typename Arg, typename Errors, typename K>
    auto k(K k) && {
      return Continuation<
          K,
          Arg,
          typename AddStopped<typename TupleToVariant<Errors>::type>::type>{
          std::move(k)};
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
