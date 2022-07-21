#pragma once

#include <functional>

#include "eventuals/loop.h"
#include "eventuals/type-traits.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

template <typename Container, typename InsertFunction>
[[nodiscard]] auto Collect(InsertFunction&& function) {
  struct Context {
    InsertFunction insert_function;
    Container container;
  };

  return Loop<Container>()
      .context(Context{std::forward<InsertFunction>(function), Container()})
      .body([](auto& context, auto& stream, auto&& value) {
        static_assert(
            std::is_void_v<
                std::invoke_result_t<
                    InsertFunction,
                    Container&,
                    decltype(value)&&>>);
        context.insert_function(
            context.container,
            std::forward<decltype(value)>(value));
        stream.Next();
      })
      .ended([](auto& context, auto& k) {
        k.Start(std::move(context.container));
      });
}

////////////////////////////////////////////////////////////////////////

// Provides a shorter version of Collect for STL containers.
template <typename Container, bool NoMatch = false>
[[nodiscard]] auto Collect() {
  if constexpr (HasEmplaceBack<Container>::value) {
    return Collect<Container>([](auto& container, auto&& value) {
      container.emplace_back(std::forward<decltype(value)>(value));
    });
  } else if constexpr (HasInsert<Container>::value) {
    return Collect<Container>([](auto& container, auto&& value) {
      container.insert(std::forward<decltype(value)>(value));
    });
  } else {
    // check_line_length skip
    // https://stackoverflow.com/questions/38304847/constexpr-if-and-static-assert
    []() {
      static_assert(NoMatch, "Provide your own InsertFunction");
    }();
  }
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
