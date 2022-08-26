#pragma once

#include "eventuals/collect.hh"
#include "google/protobuf/repeated_field.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

template <typename Collection>
struct Collector<
    Collection,
    std::enable_if_t<
        std::is_same_v<
            Collection,
            google::protobuf::RepeatedPtrField<
                typename Collection::value_type>>>> {
  template <typename T>
  static void Collect(Collection& collection, T&& value) {
    static_assert(std::is_convertible_v<T, typename Collection::value_type>);
    if constexpr (std::is_lvalue_reference_v<T&&>) {
      collection.Add(std::decay_t<T>(value));
    } else if constexpr (std::is_rvalue_reference_v<T&&>) {
      collection.Add(std::move(value));
    } else {
      static_assert(always_false_v<T>, "Unreachable");
    }
  }
};

template <typename Collection>
struct Collector<
    Collection,
    std::enable_if_t<
        std::is_same_v<
            Collection,
            google::protobuf::RepeatedField<
                typename Collection::value_type>>>> {
  template <typename T>
  static void Collect(Collection& collection, T&& value) {
    static_assert(std::is_convertible_v<T, typename Collection::value_type>);
    static_assert(std::is_pod_v<std::decay_t<T>>);
    // Add expects an lvalue reference, so we don't use std::forward here.
    collection.Add(value);
  }
};

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
