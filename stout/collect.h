#pragma once

#include <deque>
#include <functional>
#include <list>
#include <vector>

#include "stout/eventual.h"
#include "stout/interrupt.h"
#include "stout/loop.h"
#include "stout/stream.h"
#include "stout/undefined.h"

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

template <typename Container, typename F>
auto Collect(F func) {
  using T = typename Container::value_type;

  struct DataContainer {
    std::function<bool(T)> func_;
    Container content_;
  };

  return Loop<Container>()
      .context(DataContainer{std::move(func), Container()})
      .body([](auto& data, auto& stream, auto&& value) {
        if (data.func_(value)) {
          if constexpr (detail::HasEmplaceBack<Container>::value) {
            data.content_.emplace_back(std::move(value));
          } else {
            data.content_.insert(data.content_.cend(), std::move(value));
          }
        }
        stream.Next();
      })
      .ended([](auto& data, auto& k) {
        k.Start(std::move(data.content_));
      });
}

template <typename Container>
auto TakeFirstAsContainer() {
}
////////////////////////////////////////////////////////////////////////

} // namespace eventuals
} // namespace stout

////////////////////////////////////////////////////////////////////////
