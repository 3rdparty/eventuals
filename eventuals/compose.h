#pragma once

#include <type_traits>

#include "eventuals/os.h"
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

template <typename, typename = void>
struct HasErrorsFrom : std::false_type {};

template <typename T>
struct HasErrorsFrom<T, std::void_t<void_template<T::template ErrorsFrom>>>
  : std::true_type {};

////////////////////////////////////////////////////////////////////////

struct SingleValue {
  static constexpr bool ExpectsValue = true;
  static constexpr bool ExpectsStream = false;
};

struct StreamOfValues {
  static constexpr bool ExpectsValue = false;
  static constexpr bool ExpectsStream = true;
};

struct StreamOrValue {
  static constexpr bool ExpectsValue = true;
  static constexpr bool ExpectsStream = true;
};

////////////////////////////////////////////////////////////////////////

// Using to get right type for 'std::promise' at 'Terminate' because
// using 'std::promise<std::reference_wrapper<T>>' is forbidden on
// Windows build using MSVC.
// https://stackoverflow.com/questions/49830864
template <typename T>
struct ReferenceWrapperTypeExtractor {
  using type = T;
};

template <typename T>
struct ReferenceWrapperTypeExtractor<std::reference_wrapper<T>> {
  using type = T&;
};

////////////////////////////////////////////////////////////////////////

template <typename Left, typename Right>
inline constexpr bool CanCompose =
    Left::template CanCompose<typename Right::Expects>;

////////////////////////////////////////////////////////////////////////

template <typename Left_, typename Right_>
struct Composed final {
  Left_ left_;
  Right_ right_;

  template <typename Arg, typename Errors>
  using ValueFrom = typename Right_::template ValueFrom<
      typename Left_::template ValueFrom<Arg, Errors>,
      typename Left_::template ErrorsFrom<Arg, Errors>>;

  template <typename Arg, typename Errors>
  using ErrorsFrom = typename Right_::template ErrorsFrom<
      typename Left_::template ValueFrom<Arg, Errors>,
      typename Left_::template ErrorsFrom<Arg, Errors>>;

  template <typename Downstream>
  static constexpr bool CanCompose =
      Right_::template CanCompose<Downstream>;

  using Expects = typename Left_::Expects;

  template <typename Arg, typename Errors>
  auto k() && {
    using LeftValue = typename Left_::template ValueFrom<Arg, Errors>;
    using LeftErrors = typename Left_::template ErrorsFrom<Arg, Errors>;

    return std::move(left_)
        .template k<
            Arg,
            Errors>(std::move(right_).template k<LeftValue, LeftErrors>());
  }

  template <typename Arg, typename Errors, typename K>
  auto k(K k) && {
    using LeftValue = typename Left_::template ValueFrom<Arg, Errors>;
    using LeftErrors = typename Left_::template ErrorsFrom<Arg, Errors>;

    auto composed = [&]() {
      return std::move(left_).template k<Arg, Errors>(
          std::move(right_).template k<LeftValue, LeftErrors>(std::move(k)));
    };

    os::CheckSufficientStackSpace(sizeof(decltype(composed())));

    return composed();
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
[[nodiscard]] auto operator>>(Left left, Right right) {
  static_assert(
      CanCompose<Left, Right>,
      "You can't compose the \"left\" eventual with the \"right\"");
  return Composed<Left, Right>{std::move(left), std::move(right)};
}

////////////////////////////////////////////////////////////////////////

// Helpers for _building_ a continuation out of an eventual.
template <typename Arg, typename Errors, typename E>
[[nodiscard]] auto Build(E e) {
  return std::move(e).template k<Arg, Errors>();
}

template <typename Arg, typename Errors, typename E, typename K>
[[nodiscard]] auto Build(E e, K k) {
  return std::move(e).template k<Arg, Errors>(std::move(k));
}

template <typename E>
[[nodiscard]] auto Build(E e) {
  return Build<void, std::tuple<>>(std::move(e));
}

template <typename E, typename K>
[[nodiscard]] auto Build(E e, K k) {
  return Build<void, std::tuple<>>(std::move(e), std::move(k));
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
