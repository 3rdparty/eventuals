#pragma once

#include <functional>

#include "eventuals/loop.hh"
#include "eventuals/type-traits.hh"

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

// Used when Collection is a completely defined type, e.g.:
// Collect<std::vector<int>>

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

struct _Collect final {
  template <template <typename...> class Collection_>
  struct Composable final {
    template <typename Arg>
    using ValueFrom = Collection_<std::decay_t<Arg>>;

    template <typename Arg, typename Errors>
    using ErrorsFrom = Errors;

    template <typename Downstream>
    static constexpr bool CanCompose = Downstream::ExpectsValue;

    using Expects = StreamOfValues;

    template <typename Arg, typename K>
    auto k(K k) && {
      return Build(Collect<Collection_<std::decay_t<Arg>>>(), std::move(k));
    }
  };
};

////////////////////////////////////////////////////////////////////////

template <template <typename...> class Collection>
[[nodiscard]] auto Collect() {
  return _Collect::Composable<Collection>();
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
