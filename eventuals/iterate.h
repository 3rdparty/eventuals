#pragma once

#include <array>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <vector>

#include "eventuals/stream.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

template <typename Iterator>
auto Iterate(Iterator begin, Iterator end) {
  using T = decltype(*begin);

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
  using Iterator = typename Container::iterator;

  struct Data {
    Container container;
    std::optional<Iterator> begin;
  };

  // NOTE: using 'std::decay_t' here because we want value semantics
  // since we'll be doing a 'std::move' below.
  using T = std::decay_t<decltype(*std::declval<Data>().begin.value())>;

  return Stream<T>()
      .context(Data{std::move(container), std::nullopt})
      .start([](auto& data, auto& k) {
        data.begin = data.container.begin();
        k.Start();
      })
      .next([](auto& data, auto& k) {
        if (data.begin.value() != data.container.end()) {
          k.Emit(std::move(*(data.begin.value()++)));
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
  return Stream<decltype(container[0])>()
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
  return Stream<decltype(*begin)>()
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

template <typename T>
auto Iterate(std::initializer_list<T>&& values) {
  using Iterator = typename std::vector<T>::const_iterator;

  struct Data {
    std::vector<T> container;
    std::optional<Iterator> begin;
  };

  return Stream<decltype(*std::declval<Data>().begin.value())>()
      .context(Data{std::vector<T>(std::move(values)), std::nullopt})
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

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
