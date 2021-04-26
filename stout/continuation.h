#pragma once

namespace stout {
namespace eventuals {

template <typename>
struct IsContinuation : std::false_type {};

namespace detail {

template <typename E, typename K>
auto operator|(E e, K k)
{
  return std::move(e).k(std::move(k));
}

} // namespace detail {
} // namespace eventuals {
} // namespace stout {
