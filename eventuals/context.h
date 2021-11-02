#pragma once

#include <optional>
#include <tuple>

#include "glog/logging.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {
namespace detail {

////////////////////////////////////////////////////////////////////////

template <typename T, typename... Args>
class Context {
 public:
  template <typename Tuple>
  Context(Tuple args)
    : args_(std::move(args)) {}

  Context(Context&& that)
    : args_(std::move(that.args_)) {
    CHECK(!t_) << "'Context' can not be moved after using";
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

} // namespace detail

////////////////////////////////////////////////////////////////////////

template <typename T, typename... Args>
auto Context(Args&&... args) {
  return detail::Context<T, Args...>(
      std::forward_as_tuple(std::forward<Args>(args)...));
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
