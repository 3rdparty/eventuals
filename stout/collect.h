#pragma once

#include <functional>

#include "stout/loop.h"

////////////////////////////////////////////////////////////////////////

namespace stout {
namespace eventuals {

////////////////////////////////////////////////////////////////////////

namespace detail {

////////////////////////////////////////////////////////////////////////

template <typename, typename = void>
struct HasEmplaceBack : std::false_type {
};

template <typename T>
struct HasEmplaceBack<
    T,
    std::void_t<decltype(std::declval<T>().emplace_back(
        std::declval<typename T::value_type&&>()))>>
  : std::true_type {
};

} // namespace detail

////////////////////////////////////////////////////////////////////////

template <typename Container>
auto Collect() {
  return Loop<Container>()
      .context(Container())
      .body([](auto& data, auto& stream, auto&& value) {
        if constexpr (detail::HasEmplaceBack<Container>::value) {
          data.emplace_back(std::move(value));
        } else {
          data.insert(data.cend(), std::move(value));
        }
        stream.Next();
      })
      .ended([](auto& data, auto& k) {
        k.Start(std::move(data));
      });
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals
} // namespace stout

////////////////////////////////////////////////////////////////////////
