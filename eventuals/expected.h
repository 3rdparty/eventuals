#pragma once

#include <variant>

#include "eventuals/eventual.h"

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

// Tag type to identify the result of 'Unexpected(...)'.
struct _Unexpected {
  _Unexpected() = delete;
};

////////////////////////////////////////////////////////////////////////

template <typename Value_>
class _Expected {
 public:
  // Providing 'ValueFrom' in order to compose with other eventuals.
  template <typename Arg>
  using ValueFrom = Value_;

  // NOTE: providing a default constructor so this type can be used as
  // a type parameter to 'std::promise' which fails on MSVC (see
  // https://stackoverflow.com/questions/28991694 which also ran into
  // this issue; very surprised this hasn't been fixed, apparently
  // there aren't many users of 'std::promise' on Windows).
  _Expected() {}

  template <
      typename T,
      std::enable_if_t<
          std::is_convertible_v<T, Value_>,
          int> = 0>
  _Expected(T&& t)
    : variant_(std::forward<T>(t)) {}

  _Expected(std::exception_ptr exception)
    : variant_(std::move(exception)) {}

  _Expected(_Expected&& that) = default;
  _Expected(_Expected& that) = default;
  _Expected(const _Expected& that) = default;

  _Expected& operator=(_Expected&& that) = default;
  _Expected& operator=(_Expected& that) = default;
  _Expected& operator=(const _Expected& that) = default;

  template <typename U>
  _Expected(_Expected<U>&& that)
    : variant_(std::move(that).template convert<Value_>()) {}

  template <typename U>
  _Expected(const _Expected<U>& that)
    : variant_(that.template convert<Value_>()) {}

  // Providing 'k()' in order to compose with other eventuals.
  template <typename Arg, typename K>
  auto k(K k) && {
    // NOTE: we only care about "start" here because on "stop" or
    // "fail" we want to propagate that. We don't want to override a
    // "stop" with our failure because then downstream eventuals might
    // not stop but instead try and recover from the error.
    return Eventual<Value_>([variant = std::move(variant_)](auto& k) mutable {
             if (variant.index() == 0) {
               return k.Start(std::move(std::get<0>(variant)));
             } else {
               return k.Fail(std::move(std::get<1>(variant)));
             }
           })
        .template k<Value_>(std::move(k));
  }

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
  template <typename U>
  friend class _Expected;

  // Helpers that converts to a 'std::variant' of the specified type.
  template <typename To>
  std::variant<To, std::exception_ptr> convert() && {
    static_assert(
        std::disjunction_v<
            std::is_same<Value_, _Unexpected>,
            std::is_convertible<Value_, To>>,
        "cannot convert 'Expected<T>' to 'Expected<U>' because "
        "'T' can not be converted to 'U'");
    if constexpr (std::is_same_v<Value_, _Unexpected>) {
      return std::move(std::get<1>(variant_));
    } else {
      if (variant_.index() == 0) {
        return To(std::move(std::get<0>(variant_)));
      } else {
        return std::move(std::get<1>(variant_));
      }
    }
  }

  template <typename To>
  std::variant<To, std::exception_ptr> convert() const& {
    static_assert(
        std::disjunction_v<
            std::is_same<Value_, _Unexpected>,
            std::is_convertible<Value_, To>>,
        "cannot convert 'Expected<T>' to 'Expected<U>' because "
        "'T' can not be converted to 'U'");
    if constexpr (std::is_same_v<Value_, _Unexpected>) {
      return std::get<1>(variant_);
    } else {
      if (variant_.index() == 0) {
        return To(std::get<0>(variant_));
      } else {
        return std::get<1>(variant_);
      }
    }
  }

  std::variant<Value_, std::exception_ptr> variant_;
};

////////////////////////////////////////////////////////////////////////

// Facade struct that should only be used like 'Expected::Of<int>'. By
// using a 'struct' instead of a 'namespace' we can have an overloaded
// 'Expected()' function that can be used inside lambdas with deduced
// return types to properly construct the correct type.
struct Expected {
  template <typename T>
  using Of = _Expected<T>;

  // Use 'Expected::Of<T>' instead!
  Expected() = delete;
};

////////////////////////////////////////////////////////////////////////

template <typename T>
auto Expected(T t) {
  return _Expected<T>(std::move(t));
}

////////////////////////////////////////////////////////////////////////

template <typename T>
auto Unexpected(T t) {
  return _Expected<_Unexpected>(std::make_exception_ptr(std::move(t)));
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
