#pragma once

#include <array>
#include <iterator>
#include <optional>

#include "stout/stream.h"

////////////////////////////////////////////////////////////////////////

namespace stout {
namespace eventuals {

////////////////////////////////////////////////////////////////////////

template <typename Iterator>
auto Iterate(Iterator begin, Iterator end) {
  using T = typename std::iterator_traits<Iterator>::value_type;

  return Stream<T>()
      .next([begin, end](auto& k) mutable {
        if (begin != end) {
          k.Emit(*(begin++));
        } else {
          k.Ended();
        }
      })
      .done([](auto& k) {
        k.Ended();
      });
}

////////////////////////////////////////////////////////////////////////

template <typename Container>
auto Iterate(Container& container) {
  return Iterate(container.cbegin(), container.cend());
}

////////////////////////////////////////////////////////////////////////

template <typename Container>
auto Iterate(Container&& container) {
  using Iterator = typename Container::const_iterator;

  struct Data {
    Container container;
    std::optional<Iterator> begin;
  };

  using T = typename Container::value_type;

  return Stream<T>()
      .context(Data{std::move(container), std::nullopt})
      .start([](auto& data, auto& k) {
        data.begin = data.container.cbegin();
        k.Start();
      })
      .next([](auto& data, auto& k) {
        if (data.begin.value() != data.container.cend()) {
          k.Emit(*(data.begin.value()++));
        } else {
          k.Ended();
        }
      })
      .done([](auto&, auto& k) {
        k.Ended();
      });
}

////////////////////////////////////////////////////////////////////////

template <typename T, size_t n>
auto Iterate(std::array<T, n>&& container) {
  return Stream<T>()
      .next([container = std::move(container), i = 0u](auto& k) mutable {
        if (i != container.size()) {
          k.Emit(container[i++]);
        } else {
          k.Ended();
        }
      })
      .done([](auto& k) {
        k.Ended();
      });
}

////////////////////////////////////////////////////////////////////////

template <typename T>
auto Iterate(T* begin, T* end) {
  return Stream<T>()
      .next([begin, end](auto& k) mutable {
        if (begin != end) {
          k.Emit(*(begin++));
        } else {
          k.Ended();
        }
      })
      .done([](auto& k) {
        k.Ended();
      });
}

////////////////////////////////////////////////////////////////////////

template <typename T>
auto Iterate(T container[], size_t n) {
  return Iterate(&container[0], &container[n]);
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals
} // namespace stout

////////////////////////////////////////////////////////////////////////
