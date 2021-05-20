#pragma once

#include "stout/choice.h"

namespace stout {
namespace eventuals {

template <typename Value, typename Condition, typename Then, typename Else>
auto Conditional(Condition condition, Then then, Else els3)
{
  return Choice<Value>(std::move(then), std::move(els3))
    .context(std::move(condition))
    .start([](auto& condition, auto& k, auto& then, auto& els3, auto&&... args) {
      if (condition(std::forward<decltype(args)>(args)...)) {
        eventuals::succeed(then, std::forward<decltype(args)>(args)...);
      } else {
        eventuals::succeed(els3, std::forward<decltype(args)>(args)...);
      }
    });
}

} // namespace eventuals {
} // namespace stout {
