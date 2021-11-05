#pragma once

#include <variant>

#include "glog/logging.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

// Helper for creating a type based on multiple overloaded
// 'operator()'. Included in the top-level namespace for possible use
// other than 'Expected' below.
template <typename... Fs>
struct Overloaded : Fs... {
  using Fs::operator()...;
};

// NOTE: the following deduction guide needs to be at namespace scope
// for at least gcc.
template <typename... Fs>
Overloaded(Fs...) -> Overloaded<Fs...>;

////////////////////////////////////////////////////////////////////////

template <typename Value_, typename = std::exception_ptr>
class Expected;

template <typename Value_>
class Expected<Value_, std::exception_ptr> {
 public:
  // NOTE: providing a default constructor so this type can be used as
  // a type parameter to 'std::promise' which fails on MSVC (see
  // https://stackoverflow.com/questions/28991694 which also ran into
  // this issue; very surprised this hasn't been fixed, apparently
  // there aren't many users of 'std::promise' on Windows).
  Expected() {}

  template <typename T>
  Expected(T&& t)
    : variant_(std::forward<T>(t)) {}

  Expected(Expected&& that) = default;
  Expected(Expected& that) = default;
  Expected(const Expected& that) = default;

  Expected& operator=(Expected&& that) = default;
  Expected& operator=(Expected& that) = default;
  Expected& operator=(const Expected& that) = default;

  operator bool() const {
    return variant_.index() == 0;
  }

  Value_* operator->() {
    if (*this) {
      return &std::get<0>(variant_);
    } else {
      std::rethrow_exception(std::get<1>(variant_));
    }
  }

  Value_& operator*() {
    if (*this) {
      return std::get<0>(variant_);
    } else {
      std::rethrow_exception(std::get<1>(variant_));
    }
  }

  template <typename... Fs>
  auto Match(Fs&&... fs) {
    return std::visit(Overloaded{std::forward<Fs>(fs)...}, variant_);
  }

 private:
  std::variant<Value_, std::exception_ptr> variant_;
};

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
