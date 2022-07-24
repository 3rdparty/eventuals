#pragma once

#include <functional>

#include "eventuals/loop.h"
#include "eventuals/type-traits.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

template <typename Collection, typename Enable = void>
struct EventualsCollector {
  template <typename T>
  void Collect(Collection& collection, T&& value) {
    static_assert(
        !std::is_same_v<void, Enable>,
        "Provide your own EventualsCollector");
  }
};

////////////////////////////////////////////////////////////////////////

// Collectors for STL collections.

template <typename Collection>
struct EventualsCollector<
    Collection,
    std::enable_if_t<
        eventuals::HasEmplaceBack<Collection>::value>> {
  template <typename T>
  std::enable_if_t<
      std::is_convertible_v<
          T,
          typename Collection::value_type>>
  Collect(Collection& collection, T&& value) {
    collection.emplace_back(std::forward<T>(value));
  }
};

template <typename Collection>
struct EventualsCollector<
    Collection,
    std::enable_if_t<
        eventuals::HasInsert<Collection>::value>> {
  template <typename T>
  std::enable_if_t<
      std::is_convertible_v<
          T,
          typename Collection::value_type>>
  Collect(Collection& collection, T&& value) {
    collection.insert(std::forward<T>(value));
  }
};

////////////////////////////////////////////////////////////////////////

template <typename Collection>
[[nodiscard]] auto Collect() {
  return Loop<Collection>()
      .context(Collection())
      .body([](auto& collection, auto& stream, auto&& value) {
        EventualsCollector<Collection>().Collect(
            collection,
            std::forward<decltype(value)>(value));
        stream.Next();
      })
      .ended([](auto& collection, auto& k) {
        k.Start(std::move(collection));
      });
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
