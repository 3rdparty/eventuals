#include <array>
#include <iterator>

#include "stout/context.h"
#include "stout/loop.h"
#include "stout/stream.h"

////////////////////////////////////////////////////////////////////////

namespace stout {
namespace eventuals {

////////////////////////////////////////////////////////////////////////

template <typename Iter>
auto Iterate(Iter begin, Iter end) {
  struct ContainerData {
    Iter it_;
    Iter end_;
  };

  using T = typename std::iterator_traits<Iter>::value_type;

  return Stream<T>()
      .context(ContainerData{begin, end})
      .next([](auto& containerData, auto& k) {
        if (containerData.it_ != containerData.end_) {
          k.Emit(*(containerData.it_++));
        } else {
          k.Ended();
        }
      })
      .done([](auto&, auto& k) {
        k.Ended();
      });
}

template <typename Container>
auto Iterate(Container& container) {
  return Iterate(container.cbegin(), container.cend());
}

template <typename Container>
auto Iterate(Container&& container) {
  using CIter = typename Container::const_iterator;

  struct ContainerData {
    CIter it_;
    Container data_;
  };

  using T = typename Container::value_type;

  return Stream<T>()
      .context(ContainerData{container.cbegin(), std::move(container)})
      .next([](auto& containerData, auto& k) {
        if (containerData.it_ != containerData.data_.cend()) {
          k.Emit(*(containerData.it_++));
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
  struct ContainerData {
    T* it_;
    T* end_;
  };

  return Stream<T>()
      .context(ContainerData{begin, end})
      .next([](auto& containerData, auto& k) {
        if (containerData.it_ != containerData.end_) {
          k.Emit(*(containerData.it_++));
        } else {
          k.Ended();
        }
      })
      .done([](auto&, auto& k) {
        k.Ended();
      });
}

template <typename T>
auto Iterate(T container[], size_t N) {
  struct ContainerData {
    T* it_;
    T* end_;
  };

  return Stream<T>()
      .context(ContainerData{&container[0], &container[N]})
      .next([](auto& containerData, auto& k) {
        if (containerData.it_ != containerData.end_) {
          k.Emit(*(containerData.it_++));
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
} // namespace stout

////////////////////////////////////////////////////////////////////////