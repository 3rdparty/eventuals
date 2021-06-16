#pragma once

#include "stout/choice.h"
#include "stout/invoke-result.h"

namespace stout {
namespace eventuals {

template <typename Condition, typename Then, typename Else>
auto Conditional(Condition condition, Then then, Else els3)
{
  using ThenResult = typename InvokeResultUnknownArgs<Then>::type;
  using ElseResult = typename InvokeResultUnknownArgs<Else>::type;

  using ThenValue = typename ThenResult::Value;
  using ElseValue = typename ElseResult::Value;

  static_assert(
      std::is_same_v<ThenValue, ElseValue>,
      "\"then\" and \"else\" branch DO NOT return "
      "an eventual value of the same type");

  return Choice<ThenValue>(std::move(then), std::move(els3))
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
