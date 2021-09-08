#pragma once

#include <array>
#include <iterator>
#include <optional>

#include "stout/context.h"
#include "stout/loop.h"
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

template <typename Container>
auto Iterate(Container& container) {
  return Iterate(container.cbegin(), container.cend());
}

template <typename Container>
auto Iterate(Container&& container) {
  using CIterator = typename Container::const_iterator;

  struct ContainerData {
    Container container;

    std::optional<CIterator> begin;
  };

  using T = typename Container::value_type;

  return Stream<T>()
      .context(ContainerData{std::move(container), std::nullopt})
      .start([](auto& ContainerData, auto& k) {
        ContainerData.begin = ContainerData.container.cbegin();
        k.Start();
      })
      .next([](auto& containerData, auto& k) {
        if (containerData.begin.value() != containerData.container.cend()) {
          k.Emit(*(containerData.begin.value()++));
        } else {
          k.Ended();
        }
      })
      .done([](auto&, auto& k) {
        k.Ended();
      });
}

template <typename T, size_t N>
auto Iterate(std::array<T, N>&& container) {
  struct ContainerData {
    std::array<T, N> data_;
    size_t current_;
  };

  return Stream<T>()
      .context(ContainerData{std::move(container), 0})
      .next([](auto& containerData, auto& k) {
        if (containerData.current_ != containerData.data_.size()) {
          k.Emit(containerData.data_[containerData.current_++]);
        } else {
          k.Ended();
        }
      })
      .done([](auto&, auto& k) {
        k.Ended();
      });
}

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

template <typename T>
auto Iterate(T container[], size_t N) {
  struct ContainerData {
    T* it_;
    T* end_;
  };

  return Iterate(&container[0], &container[N]);
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals
} // namespace stout

////////////////////////////////////////////////////////////////////////