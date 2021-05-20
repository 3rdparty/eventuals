#pragma once

#include "stout/eventual.h"

namespace stout {
namespace eventuals {

template <typename T>
auto Return(T t)
{
  return Eventual<T>()
    .context(std::move(t))
    .start([](auto& t, auto& k) {
      eventuals::succeed(k, std::move(t));
    });
}

} // namespace eventuals {
} // namespace stout {
