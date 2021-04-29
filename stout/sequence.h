// TODO(benh): consider moving into 'stout-sequence' repository.

#pragma once

#include "stout/undefined.h"

namespace stout {

namespace detail {

template <typename F_, typename Next_>
struct Sequence {
  template <typename... Ts>
  void operator()(Ts&&... ts) {
    if (!invoked_) {
      invoked_ = true;
      f_(std::forward<Ts>(ts)...);
    } else {
      if constexpr (!IsUndefined<Next_>::value) {
        next_.operator()(std::forward<Ts>(ts)...);
      } else {
        throw std::runtime_error("End of sequence");
      }
    }
  }

  template <typename F>
  auto Once(F f) && {
    if constexpr (IsUndefined<F_>::value) {
      return Sequence<F, Undefined> { std::move(f), Undefined() };
    } else if constexpr (IsUndefined<Next_>::value) {
      auto next = Sequence<F, Undefined> { std::move(f), Undefined() };
      return Sequence<F_, decltype(next)> {
        std::move(f_),
        std::move(next)
      };
    } else {
      auto next = std::move(next_).Once(std::move(f));
      return Sequence<F_, decltype(next)> {
        std::move(f_),
        std::move(next)
      };
    }
  }

  F_ f_;
  Next_ next_;
  bool invoked_ = false;
};

} // namespace detail {

struct Sequence : public detail::Sequence<Undefined, Undefined> {
  Sequence() {}
};

} // namespace stout {

