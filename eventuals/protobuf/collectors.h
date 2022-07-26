#pragma once

#include "eventuals/collect.h"
#include "google/protobuf/repeated_field.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

template <typename Collection>
struct EventualsCollector<
    Collection,
    std::enable_if_t<
        std::is_same_v<
            Collection,
            google::protobuf::RepeatedPtrField<
                typename Collection::value_type>>>> {
  // Since 'Add(T&& value)' takes ownership of value, we pass a copy.
  template <typename T>
  static void Collect(Collection& collection, T value) {
    static_assert(std::is_convertible_v<T, typename Collection::value_type>);
    collection.Add(std::move(value));
  }
};

template <typename Collection>
struct EventualsCollector<
    Collection,
    std::enable_if_t<
        std::is_same_v<
            Collection,
            google::protobuf::RepeatedField<
                typename Collection::value_type>>>> {
  template <typename T>
  static void Collect(Collection& collection, T&& value) {
    static_assert(std::is_convertible_v<T, typename Collection::value_type>);
    collection.Add(value);
  }
};

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
