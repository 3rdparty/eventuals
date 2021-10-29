#pragma once

#include "eventuals/eventual.h"
#include "eventuals/map.h"
#include "eventuals/stream.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

namespace detail {

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

} // namespace detail

////////////////////////////////////////////////////////////////////////

template <typename E>
auto Repeat(E e) {
  // static_assert(
  //     IsContinuation<E>::value,
  //     "expecting an eventual continuation for Repeat");

  return detail::_Repeat::Composable{} | Map(std::move(e));
}

inline auto Repeat() {
  return detail::_Repeat::Composable{};
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
