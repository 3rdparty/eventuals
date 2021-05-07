#pragma once

namespace stout {
namespace eventuals {

template <typename>
struct IsContinuation : std::false_type {};

template <typename K>
struct Compose
{
  template <typename Value>
  static auto compose(K k)
  {
    return std::move(k);
  }
};

template <typename Value, typename K>
auto compose(K k)
{
  return Compose<K>::template compose<Value>(std::move(k));
}

namespace detail {

template <typename E, typename K>
auto operator|(E e, K k)
{
  return std::move(e).k(compose<typename E::Value>(std::move(k)));
}

} // namespace detail {
} // namespace eventuals {
} // namespace stout {
