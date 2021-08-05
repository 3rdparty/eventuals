#pragma once

#include "stout/eventual.h"
#include "stout/map.h"
#include "stout/stream.h"

////////////////////////////////////////////////////////////////////////

namespace stout {
namespace eventuals {

////////////////////////////////////////////////////////////////////////

namespace detail {

////////////////////////////////////////////////////////////////////////

template <typename K_>
struct Repeat : public TypeErasedStream {
  // NOTE: explicit constructor because inheriting 'TypeErasedStream'.
  Repeat(K_ k)
    : k_(std::move(k)) {}

  template <typename... Args>
  void Start(Args&&...) {
    eventuals::succeed(k_, *this);
  }

  template <typename... Args>
  void Fail(Args&&... args) {
    eventuals::fail(k_, std::forward<Args>(args)...);
  }

  void Stop() {
    eventuals::stop(k_);
  }

  void Register(Interrupt& interrupt) {
    k_.Register(interrupt);
  }

  void Next() override {
    eventuals::body(k_);
  }

  void Done() override {
    eventuals::ended(k_);
  }

  K_ k_;
};

////////////////////////////////////////////////////////////////////////

struct RepeatComposable {
  template <typename Arg>
  using ValueFrom = void;

  template <typename Arg, typename K>
  auto k(K k) {
    return Repeat<K>{std::move(k)};
  }
};

////////////////////////////////////////////////////////////////////////

} // namespace detail

////////////////////////////////////////////////////////////////////////

template <typename E>
auto Repeat(E e) {
  // static_assert(
  //     IsContinuation<E>::value,
  //     "expecting an eventual continuation for Repeat");

  return detail::RepeatComposable{} | Map(std::move(e));
}

inline auto Repeat() {
  return detail::RepeatComposable{};
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals
} // namespace stout

////////////////////////////////////////////////////////////////////////
