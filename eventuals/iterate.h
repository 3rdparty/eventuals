#pragma once

#include <array>
#include <deque>
#include <iterator>
#include <optional>

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
      .begin([](auto& data, auto& k) {
        data.begin = data.container.begin();
        k.Begin();
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

// NOTE: we take an array instead of a 'std::initializer_list' because
// the latter seems to always want to force it's contents to be copied
// rather than moved but the whole point is to move the temporary
// values through the stream!
template <typename T, size_t n>
auto Iterate(T(&&values)[n]) {
  // NOTE: using a 'std::deque' in case 'T' is only moveable so that
  // when we do 'emplace_back()' it won't require that 'T' is also
  // copyable which 'std::vector' does.
  //
  // TODO(benh): emperically it appears with clang as though we can
  // use a 'std::vector' perhaps because it unrolls the for loop below
  // but rather than rely on this behavior we're just going to stick
  // to 'std::deque' for now.
  std::deque<T> container;

  for (size_t i = 0; i < n; i++) {
    container.emplace_back(std::move(values[i]));
  }

  return Iterate(std::move(container));
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
