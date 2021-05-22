#pragma once

namespace stout {

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


namespace detail {

template <size_t n>
constexpr size_t decrement()
{
  static_assert(
      n != 0,
      "tried up to 16 arguments; either make your "
      "argument types more explicit (i.e, don't use"
      " 'auto') or specify a larger 'n'");

  return n - 1;
}

} // namespace detail {


template <typename F, size_t n = 16, typename... Args>
struct InvokeResultUnknownArgs : std::conditional_t<
  std::is_invocable_v<F, Args...>,
  std::invoke_result<F, Args...>,
  InvokeResultUnknownArgs<F, detail::decrement<n>(), Args..., AnyArg>> {};

} // namespace stout {
