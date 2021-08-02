#pragma once

#include "stout/compose.h"
#include "stout/eventual.h"

////////////////////////////////////////////////////////////////////////

namespace stout {
namespace eventuals {

////////////////////////////////////////////////////////////////////////

namespace detail {

////////////////////////////////////////////////////////////////////////

template <typename T_>
struct JustComposable
{
  template <typename>
  using ValueFrom = T_;

  template <typename Arg, typename K>
  auto k(K k) &&
  {
    auto start = [t = std::move(t_)](auto& k, auto&&...) {
      eventuals::succeed(k, std::move(t));
    };

    return Eventual<
      decltype(k),
      Undefined,
      decltype(start),
      Undefined,
      Undefined,
      Undefined,
      T_> {
      std::move(k),
      Undefined(),
      std::move(start),
      Undefined(),
      Undefined(),
      Undefined()
    };
  }

  T_ t_;
};

template <>
struct JustComposable<void>
{
  template <typename>
  using ValueFrom = void;

  template <typename Arg, typename K>
  auto k(K k) &&
  {
    auto start = [](auto& k, auto&&...) {
      eventuals::succeed(k);
    };

    return Eventual<
      decltype(k),
      Undefined,
      decltype(start),
      Undefined,
      Undefined,
      Undefined,
      void> {
      std::move(k),
      Undefined(),
      std::move(start),
      Undefined(),
      Undefined(),
      Undefined()
    };
  }
};

////////////////////////////////////////////////////////////////////////

} // namespace detail {

////////////////////////////////////////////////////////////////////////

template <typename T>
auto Just(T t)
{
  return detail::JustComposable<T> { std::move(t) };
}

inline auto Just()
{
  return detail::JustComposable<void> {};
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals {
} // namespace stout {

////////////////////////////////////////////////////////////////////////
