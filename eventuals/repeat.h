#pragma once

#include "eventuals/compose.h" // For 'HasValueFrom'.
#include "eventuals/eventual.h"
#include "eventuals/map.h"
#include "eventuals/stream.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

struct _Repeat {
  template <typename K_>
  struct Continuation : public TypeErasedStream {
    // NOTE: explicit constructor because inheriting 'TypeErasedStream'.
    Continuation(K_ k)
      : k_(std::move(k)) {}

    template <typename... Args>
    void Start(Args&&...) {
      previous_ = Scheduler::Context::Get();

      k_.Begin(*this);
    }

    template <typename... Args>
    void Fail(Args&&... args) {
      k_.Fail(std::forward<Args>(args)...);
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

    K_ k_;

    Scheduler::Context* previous_ = nullptr;
  };

  struct Composable {
    template <typename Arg>
    using ValueFrom = void;

    template <typename Arg, typename K>
    auto k(K k) {
      return Continuation<K>{std::move(k)};
    }
  };
};

////////////////////////////////////////////////////////////////////////

template <typename F>
auto Repeat(F f) {
  static_assert(
      !HasValueFrom<F>::value,
      "'Repeat' expects a callable not an eventual");

  return _Repeat::Composable{} | Map(std::move(f));
}

inline auto Repeat() {
  return _Repeat::Composable{};
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
