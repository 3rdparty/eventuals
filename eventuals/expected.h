#pragma once

#include <variant>

#include "eventuals/eventual.h"
#include "eventuals/terminal.h"
#include "eventuals/type-traits.h"

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

// Helper type to represent an 'Unexpected(...)'.
template <typename Error_>
struct _Unexpected final {
  _Unexpected(Error_ error)
    : error(std::move(error)) {}

  Error_ error;
};

////////////////////////////////////////////////////////////////////////

template <typename Value_, typename Errors_ = std::tuple<std::exception>>
class _Expected {
 public:
  static_assert(
      !tuple_types_contains_v<Value_, Errors_>,
      "'Expected::Of' value and raised errors must be disjoint");

  // Providing 'ValueFrom' and 'ErrorsFrom' to compose with eventuals.
  template <typename Arg>
  using ValueFrom = Value_;

  template <typename Arg, typename Errors>
  using ErrorsFrom = tuple_types_union_t<Errors_, Errors>;

#if _WIN32
  // NOTE: default constructor should not exist or be used but is
  // necessary on Windows so this type can be used as a type parameter
  // to 'std::promise', see: https://bit.ly/VisualStudioStdPromiseBug
  _Expected() {}
#else
  _Expected() = delete;
#endif

  template <
      typename T,
      std::enable_if_t<std::is_convertible_v<T, Value_>, int> = 0>
  _Expected(T&& t)
    : variant_(Value_(std::forward<T>(t))) {}

  _Expected(std::exception_ptr error)
    : variant_(std::move(error)) {}

  template <
      typename T,
      typename Errors,
      std::enable_if_t<
          std::conjunction_v<
              std::is_convertible<T, Value_>,
              tuple_types_subset_subtype<Errors, Errors_>>,
          int> = 0>
  _Expected(_Expected<T, Errors> that)
    : variant_([&]() {
        if (that.has_value()) {
          return std::variant<Value_, std::exception_ptr>(
              std::get<0>(std::move(that.variant_)));
        } else {
          return std::variant<Value_, std::exception_ptr>(
              std::get<1>(std::move(that.variant_)));
        }
      }()) {}

  template <typename Error>
  _Expected(_Unexpected<Error> unexpected)
    : variant_([&]() {
        static_assert(std::is_base_of_v<std::exception, Error>);
        static_assert(
            tuple_types_contains_subtype_v<Error, Errors_>,
            "'Expected::Of::Raises' does not include 'Unexpected(...)' type");

        return make_exception_ptr_or_forward(std::move(unexpected.error));
      }()) {}

  _Expected(_Expected&& that) = default;
  _Expected(_Expected& that) = default;
  _Expected(const _Expected& that) = default;

  virtual ~_Expected() = default;

  _Expected& operator=(_Expected&& that) = default;
  _Expected& operator=(_Expected& that) = default;
  _Expected& operator=(const _Expected& that) = default;

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

  bool has_value() const {
    return variant_.index() == 0;
  }

  explicit operator bool() const {
    return has_value();
  }

  Value_* operator->() {
    if (has_value()) {
      return &std::get<0>(variant_);
    } else {
      std::rethrow_exception(std::get<1>(variant_));
    }
  }

  Value_& operator*() {
    if (has_value()) {
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
  template <typename, typename>
  friend class _Expected;

  std::variant<Value_, std::exception_ptr> variant_;
};

////////////////////////////////////////////////////////////////////////

// Facade struct that should only be used like 'Expected::Of<int>'. By
// using a 'struct' instead of a 'namespace' we can have an overloaded
// 'Expected()' function that can be used inside lambdas with deduced
// return types to properly construct the correct type.
struct Expected final {
  template <typename Value>
  struct Of final : public _Expected<Value> {
    template <typename... Errors>
    using Raises = _Expected<Value, std::tuple<Errors...>>;

    using _Expected<Value>::_Expected;

    ~Of() override = default;

    using _Expected<Value>::operator=;
  };

  // Use 'Expected::Of<T>' instead!
  Expected() = delete;
};

////////////////////////////////////////////////////////////////////////

template <typename Value>
auto Expected(Value value) {
  return _Expected<Value, std::tuple<>>(std::move(value));
}

////////////////////////////////////////////////////////////////////////

template <typename Error>
auto Unexpected(Error error) {
  static_assert(
      std::is_base_of_v<std::exception, std::decay_t<Error>>,
      "Expecting a type derived from std::exception");
  return _Unexpected<Error>(std::move(error));
}

////////////////////////////////////////////////////////////////////////

inline auto Unexpected(const std::string& s) {
  return Unexpected(std::runtime_error(s));
}

////////////////////////////////////////////////////////////////////////

inline auto Unexpected(char* s) {
  return Unexpected(std::runtime_error(s));
}

////////////////////////////////////////////////////////////////////////

inline auto Unexpected(const char* s) {
  return Unexpected(std::runtime_error(s));
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
