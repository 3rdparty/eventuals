#pragma once

#include "eventuals/expected.h"
#include "eventuals/map.h"
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
          expected<Arg_>(
              std::forward<Args>(args)...));
    }

    template <typename Error>
    void Fail(Error&& error) {
      if constexpr (std::is_same_v<Error, std::exception_ptr>) {
        try {
          std::rethrow_exception(error);
        } catch (const std::exception& e) {
          k_.Start(
              expected<Arg_>(
                  make_unexpected(e.what())));
        } catch (...) {
          LOG(FATAL) << "unreachable";
        }
      } else {
        k_.Start(
            expected<Arg_>(
                make_unexpected(error.what())));
      }
    }

    void Stop() {
      k_.Start(
          expected<Arg_>(
              make_unexpected(StoppedException().what())));
    }

    void Register(Interrupt& interrupt) {
      k_.Register(interrupt);
    }

    K_ k_;
  };

  struct Composable final {
    template <typename Arg>
    using ValueFrom = expected<Arg>;

    template <typename Arg, typename Errors>
    using ErrorsFrom = std::tuple<>;

    template <typename Arg, typename K>
    auto k(K k) && {
      return Continuation<K, Arg>{std::move(k)};
    }
  };
};

////////////////////////////////////////////////////////////////////////

struct _StreamFinally final {
  template <typename K_, typename Arg_>
  struct Continuation final {
    template <typename Error>
    void Fail(Error&& error) {
      if constexpr (std::is_same_v<Error, std::exception_ptr>) {
        try {
          std::rethrow_exception(error);
        } catch (const std::exception& e) {
          k_.Body(
              expected<Arg_>(
                  make_unexpected(e.what())));
        } catch (...) {
          LOG(FATAL) << "unreachable";
        }
      } else {
        k_.Body(
            expected<Arg_>(
                make_unexpected(error.what())));
      }
    }

    void Stop() {
      k_.Body(
          expected<Arg_>(
              make_unexpected(StoppedException().what())));
    }

    void Begin(TypeErasedStream& stream) {
      k_.Begin(stream);
    }

    template <typename... Args>
    void Body(Args&&... args) {
      k_.Body(
          expected<Arg_>(
              std::forward<Args>(args)...));
    }

    void Ended() {
      k_.Ended();
    }

    void Register(Interrupt& interrupt) {
      k_.Register(interrupt);
    }

    K_ k_;
  };

  struct Composable final {
    template <typename Arg>
    using ValueFrom = expected<Arg>;

    template <typename Arg, typename Errors>
    using ErrorsFrom = std::tuple<>;

    template <typename Arg, typename K>
    auto k(K k) && {
      return Continuation<K, Arg>{std::move(k)};
    }
  };
};

////////////////////////////////////////////////////////////////////////

template <typename F>
[[nodiscard]] auto Finally(F f) {
  static_assert(
      !HasValueFrom<F>::value,
      "'Finally' expects a callable (e.g., a lambda), not an eventual");

  return _Finally::Composable()
      | Then(std::move(f));
}

////////////////////////////////////////////////////////////////////////

template <typename F>
[[nodiscard]] auto StreamFinally(F f) {
  static_assert(
      !HasValueFrom<F>::value,
      "'Finally' expects a callable (e.g., a lambda), not an eventual");

  return _StreamFinally::Composable()
      | Map(std::move(f));
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
