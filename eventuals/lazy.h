#pragma once

#include <optional>
#include <tuple>

#include "glog/logging.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

template <typename T, typename... Args>
class _Lazy final {
 public:
  template <typename Tuple>
  _Lazy(Tuple args)
    : args_(std::move(args)) {}

  _Lazy(_Lazy&& that)
    : args_(std::move(that.args_)) {
    CHECK(!t_) << "'Lazy' can not be moved after using";
  }

  T* get() {
    if (!t_) {
      auto emplace = [this](auto&&... args) mutable {
        t_.emplace(std::forward<decltype(args)>(args)...);
      };
      std::apply(emplace, std::move(args_));
    }

    return &t_.value();
  }

  T* operator->() {
    return get();
  }

  T& operator*() {
    return *get();
  }

 private:
  std::optional<T> t_;
  std::tuple<Args...> args_;
};

////////////////////////////////////////////////////////////////////////

template <typename T, typename... Args>
auto Lazy(Args&&... args) {
  return _Lazy<T, Args...>(
      std::forward_as_tuple(std::forward<Args>(args)...));
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
