#pragma once

#include <functional>

#include "eventuals/loop.h"
#include "eventuals/type-traits.h"

// NOTE: The following include is a rather large one.
#include "google/protobuf/repeated_field.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

template <typename Container>
[[nodiscard]] auto Collect() {
  return Loop<Container>()
      .context(Container())
      .body([](auto& data, auto& stream, auto&& value) {
        if constexpr (
            std::is_same_v<
                Container,
                google::protobuf::RepeatedField<
                    typename Container::value_type>>) {
          data.Add(value);
        } else if constexpr (
            std::is_same_v<
                Container,
                google::protobuf::RepeatedPtrField<
                    typename Container::value_type>>) {
          // Should probably use std::forward instead of std::move,
          // but using std::forward results in a compiler error,
          // because google::protobuf::RepeatedPtrField::Add
          // expects an rvalue reference.
          data.Add(std::move<decltype(value)>(value));
        } else if constexpr (HasEmplaceBack<Container>::value) {
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
