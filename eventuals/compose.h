#pragma once

#include "eventuals/type-traits.h"
#include "glog/logging.h"

////////////////////////////////////////////////////////////////////////

inline bool EventualsLog(int level) {
  static const char* variable = std::getenv("EVENTUALS_LOG");
  static int value = variable != nullptr ? atoi(variable) : 0;
  return value >= level;
}

#define EVENTUALS_LOG(level) LOG_IF(INFO, EventualsLog(level))

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

template <typename, typename = void>
struct HasValueFrom : std::false_type {};

template <typename T>
struct HasValueFrom<T, std::void_t<void_template<T::template ValueFrom>>>
  : std::true_type {};

////////////////////////////////////////////////////////////////////////

template <typename Left_, typename Right_>
struct Composed final {
  Left_ left_;
  Right_ right_;

  template <typename Arg>
  using ValueFrom = typename Right_::template ValueFrom<
      typename Left_::template ValueFrom<Arg>>;

  template <typename Arg>
  auto k() && {
    using Value = typename Left_::template ValueFrom<Arg>;
    return std::move(left_)
        .template k<Arg>(std::move(right_).template k<Value>());
  }

  template <typename Arg, typename K>
  auto k(K k) && {
    using Value = typename Left_::template ValueFrom<Arg>;
    return std::move(left_)
        .template k<Arg>(std::move(right_).template k<Value>(std::move(k)));
  }
};

////////////////////////////////////////////////////////////////////////

template <
    typename Left,
    typename Right,
    std::enable_if_t<
        std::conjunction_v<
            HasValueFrom<Left>,
            HasValueFrom<Right>>,
        int> = 0>
auto operator|(Left left, Right right) {
  return Composed<Left, Right>{std::move(left), std::move(right)};
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
