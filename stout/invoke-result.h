#pragma once

#include "stout/undefined.h"

// Helpers for getting invocation result types.

////////////////////////////////////////////////////////////////////////

namespace stout {

////////////////////////////////////////////////////////////////////////

// TODO(benh): Replace with std::type_identity from C++20.
template <typename T>
struct type_identity
{
  using type = T;
};

////////////////////////////////////////////////////////////////////////

struct AnyArg
{
  template <typename T>
  operator T&& () { throw std::runtime_error("unexpected invocation"); }

  template <typename T>
  operator T& () { throw std::runtime_error("unexpected invocation"); }
};


template <typename T>
auto operator+(AnyArg arg, T&& t)
{
  return static_cast<T>(arg) + std::forward<T>(t);
}

// TODO(benh): add more overloads for operators (-, !, etc).

////////////////////////////////////////////////////////////////////////

namespace detail {

////////////////////////////////////////////////////////////////////////

template <size_t n>
constexpr size_t decrement()
{
  static_assert(
      n > 1,
      "could not determine invoke result; either make "
      "your argument types more explicit (i.e, don't use"
      " 'auto') or specify a larger 'n'");

  return n - 1;
}

////////////////////////////////////////////////////////////////////////

} // namespace detail {

////////////////////////////////////////////////////////////////////////

template <typename F, size_t n = 12, typename... Args>
struct InvokeResultUnknownArgs : std::conditional_t<
  std::is_invocable_v<F, Args...>,
  std::invoke_result<F, Args...>,
  InvokeResultUnknownArgs<F, detail::decrement<n>(), Args..., AnyArg>> {};


template <typename F, typename... Args>
struct InvokeResultUnknownArgs<F, 0, Args...> {};

////////////////////////////////////////////////////////////////////////

template <typename F, typename... Values>
struct InvokeResultPossiblyUndefined
{
  using type = std::invoke_result_t<F, Values...>;
};


template <typename F>
struct InvokeResultPossiblyUndefined<F, Undefined>
{
  using type = typename std::conditional_t<
    std::is_invocable_v<F>,
    std::invoke_result<F>,
    type_identity<Undefined>>::type;
};

////////////////////////////////////////////////////////////////////////

} // namespace stout {

////////////////////////////////////////////////////////////////////////
