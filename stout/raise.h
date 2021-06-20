#pragma once

#include "stout/eventual.h"

namespace stout {
namespace eventuals {

template <typename T>
auto Raise(T t)
{
  return Eventual<Undefined>()
    .context(std::move(t))
    .start([](auto& t, auto& k, auto&&...) {
      eventuals::fail(k, std::move(t));
    });
}

} // namespace eventuals {
} // namespace stout {
