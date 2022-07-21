#pragma once

#include <functional>

#include "eventuals/loop.h"
#include "eventuals/type-traits.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

template <typename Collection, typename = void>
struct Collector {
  template <typename T>
  static void Collect(Collection& collection, T&& value) {
    // This static_assert will always fail
    // if the compiler needs to compile this implementation.
    static_assert(
        always_false_v<T>,
        "Provide your own EventualsCollector");
  }
};

////////////////////////////////////////////////////////////////////////

// Collectors for STL collections.

template <typename Collection>
struct Collector<
    Collection,
    std::enable_if_t<
        eventuals::HasEmplaceBack<Collection>::value>> {
  template <typename T>
  static void Collect(Collection& collection, T&& value) {
    static_assert(std::is_convertible_v<T, typename Collection::value_type>);
    collection.emplace_back(std::forward<T>(value));
  }
};

template <typename Collection>
struct Collector<
    Collection,
    std::enable_if_t<
        eventuals::HasInsert<Collection>::value>> {
  template <typename T>
  static void Collect(Collection& collection, T&& value) {
    static_assert(std::is_convertible_v<T, typename Collection::value_type>);
    collection.insert(std::forward<T>(value));
  }
};

////////////////////////////////////////////////////////////////////////

// TODO(folming): Issue #486.
template <typename Collection>
[[nodiscard]] auto Collect() {
  return Loop<Collection>()
      .context(Collection())
      .body([](auto& collection, auto& stream, auto&& value) {
        Collector<Collection>::Collect(
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
