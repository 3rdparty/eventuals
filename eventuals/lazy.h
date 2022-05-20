#pragma once

#include <optional>
#include <tuple>

#include "glog/logging.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

template <typename T_, typename... Args_>
struct _Lazy final {
 public:
  template <typename Arg, typename... Args>
  using Args =
      std::enable_if_t<sizeof...(Args_) == 0, _Lazy<T_, Arg, Args...>>;

  _Lazy(_Lazy&& that)
    : args_(std::move(that.args_)) {
    CHECK(!t_) << "'Lazy' can not be moved after using";
  }

  template <
      typename... Args,
      std::enable_if_t<
          std::is_convertible_v<std::tuple<Args...>, std::tuple<Args_...>>,
          int> = 0>
  _Lazy(Args&&... args)
    : args_(std::forward<Args>(args)...) {}

  T_* get() {
    if (!t_) {
      auto emplace = [this](auto&&... args) mutable {
        t_.emplace(std::forward<decltype(args)>(args)...);
      };
      std::apply(emplace, std::move(args_));
    }

    return &t_.value();
  }

  T_* operator->() {
    return get();
  }

  T_& operator*() {
    return *get();
  }

 private:
  std::optional<T_> t_;
  std::tuple<Args_...> args_;
};

////////////////////////////////////////////////////////////////////////

struct Lazy final {
  template <typename T>
  using Of = _Lazy<T>;

  // Use 'Lazy::Of<T>' instead!
  Lazy() = delete;
};

////////////////////////////////////////////////////////////////////////

template <typename T, typename... Args>
auto Lazy(Args&&... args) {
  return _Lazy<T, Args...>(std::forward<Args>(args)...);
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
