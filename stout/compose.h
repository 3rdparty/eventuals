#pragma once

////////////////////////////////////////////////////////////////////////

namespace stout {
namespace eventuals {

////////////////////////////////////////////////////////////////////////

namespace detail {

////////////////////////////////////////////////////////////////////////

template <typename Left_, typename Right_>
struct Composed
{
  Left_ left_;
  Right_ right_;

  template <typename Arg>
  using ValueFrom = typename Right_::template ValueFrom<
    typename Left_::template ValueFrom<Arg>>;

  template <typename Arg>
  auto k() &&
  {
    using Value = typename Left_::template ValueFrom<Arg>;
    return std::move(left_)
      .template k<Arg>(std::move(right_).template k<Value>());
  }

  template <typename Arg, typename K>
  auto k(K k) &&
  {
    using Value = typename Left_::template ValueFrom<Arg>;
    return std::move(left_)
      .template k<Arg>(std::move(right_).template k<Value>(std::move(k)));
  }
};

////////////////////////////////////////////////////////////////////////

template <typename Left, typename Right>
auto operator|(Left left, Right right)
{
  return Composed<Left, Right> { std::move(left), std::move(right) };
}

////////////////////////////////////////////////////////////////////////

} // namespace detail {

////////////////////////////////////////////////////////////////////////

} // namespace eventuals {
} // namespace stout {

////////////////////////////////////////////////////////////////////////
