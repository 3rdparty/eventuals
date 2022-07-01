#pragma once

#include "eventuals/compose.h" // For 'HasValueFrom'.
#include "eventuals/map.h"
#include "eventuals/stream.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

struct _Repeat final {
  template <typename K_>
  struct Continuation final : public TypeErasedStream {
    // NOTE: explicit constructor because inheriting 'TypeErasedStream'.
    Continuation(K_ k)
      : k_(std::move(k)) {}

    Continuation(Continuation&& that) noexcept = default;

    ~Continuation() override = default;

    template <typename... Args>
    void Start(Args&&...) {
      previous_ = Scheduler::Context::Get();

      k_.Begin(*this);
    }

    template <typename Error>
    void Fail(Error&& error) {
      k_.Fail(std::forward<Error>(error));
    }

    void Stop() {
      k_.Stop();
    }

    void Register(Interrupt& interrupt) {
      k_.Register(interrupt);
    }

    void Next() override {
      previous_->Continue([this]() {
        k_.Body();
      });
    }

    void Done() override {
      previous_->Continue([this]() {
        k_.Ended();
      });
    }

    stout::borrowed_ptr<Scheduler::Context> previous_;

    // NOTE: we store 'k_' as the _last_ member so it will be
    // destructed _first_ and thus we won't have any use-after-delete
    // issues during destruction of 'k_' if it holds any references or
    // pointers to any (or within any) of the above members.
    K_ k_;
  };

  struct Composable final {
    template <typename Arg>
    using ValueFrom = void;

    template <typename Arg, typename Errors>
    using ErrorsFrom = Errors;

    template <typename Arg, typename Errors, typename K>
    auto k(K k) {
      return Continuation<K>(std::move(k));
    }

    template <typename Downstream>
    static constexpr bool CanCompose = Downstream::ExpectsStream;

    using Expects = SingleValue;
  };
};

////////////////////////////////////////////////////////////////////////

template <typename F>
[[nodiscard]] auto Repeat(F f) {
  static_assert(
      !HasValueFrom<F>::value,
      "'Repeat' expects a callable (e.g., a lambda) not an eventual");

  return _Repeat::Composable{} >> Map(std::move(f));
}

[[nodiscard]] inline auto Repeat() {
  return _Repeat::Composable{};
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
