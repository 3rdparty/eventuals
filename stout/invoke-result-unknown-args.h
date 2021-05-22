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


template <typename F, size_t n = 16, typename... Args>
struct InvokeResultUnknownArgs : std::conditional_t<
  std::is_invocable_v<F, Args...>,
  std::invoke_result<F, Args...>,
  InvokeResultUnknownArgs<F, n - 1, Args..., AnyArg>>
{
  static_assert(
      n != 0,
      "tried up to 16 arguments; either make your "
      "argument types more explicit (i.e, don't use"
      " 'auto') or specify a larger 'n'");
};

} // namespace stout {
