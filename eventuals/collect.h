#pragma once

#include <functional>

#include "eventuals/loop.h"
#include "eventuals/type-traits.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

template <typename Container>
auto Collect() {
  return Loop<Container>()
      .context(Container())
      .body([](auto& data, auto& stream, auto&& value) {
        if constexpr (HasEmplaceBack<Container>::value) {
          data.emplace_back(std::forward<decltype(value)>(value));
        } else {
          data.insert(data.cend(), std::forward<decltype(value)>(value));
        }
        stream.Next();
      })
      .ended([](auto& data, auto& k) {
        k.Start(std::move(data));
      });
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
