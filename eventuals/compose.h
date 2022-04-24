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

template <typename, typename = void>
struct HasErrorsFrom : std::false_type {};

template <typename T>
struct HasErrorsFrom<T, std::void_t<void_template<T::template ErrorsFrom>>>
  : std::true_type {};

////////////////////////////////////////////////////////////////////////

// Helper to avoid creating nested 'std::exception_ptr'.
template <typename... Args>
auto make_exception_ptr_or_forward(Args&&... args) {
  static_assert(sizeof...(args) > 0, "Expecting an error");
  static_assert(!std::is_same_v<std::decay_t<Args>..., std::exception_ptr>);
  static_assert(
      std::is_base_of_v<std::exception, std::decay_t<Args>...>,
      "Expecting a type derived from std::exception");
  return std::make_exception_ptr(std::forward<Args>(args)...);
}

inline auto make_exception_ptr_or_forward(std::exception_ptr error) {
  return error;
}

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

template <typename Left_, typename Right_>
struct Composed final {
  Left_ left_;
  Right_ right_;

  template <typename Arg>
  using ValueFrom = typename Right_::template ValueFrom<
      typename Left_::template ValueFrom<Arg>>;

  template <typename Arg, typename Errors>
  using ErrorsFrom = typename Right_::template ErrorsFrom<
      typename Left_::template ValueFrom<Arg>,
      typename Left_::template ErrorsFrom<Arg, Errors>>;

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
[[nodiscard]] auto operator|(Left left, Right right) {
  return Composed<Left, Right>{std::move(left), std::move(right)};
}

////////////////////////////////////////////////////////////////////////

// Helpers for _building_ a continuation out of an eventual.
template <typename Arg, typename E>
[[nodiscard]] auto Build(E e) {
  return std::move(e).template k<Arg>();
}

template <typename Arg, typename E, typename K>
[[nodiscard]] auto Build(E e, K k) {
  return std::move(e).template k<Arg>(std::move(k));
}

template <typename E>
[[nodiscard]] auto Build(E e) {
  return Build<void>(std::move(e));
}

template <typename E, typename K>
[[nodiscard]] auto Build(E e, K k) {
  return Build<void>(std::move(e), std::move(k));
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
