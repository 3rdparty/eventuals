#pragma once

#include "eventuals/compose.hh"
#include "eventuals/eventual.hh"
#include "tl/expected.hpp"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

// Helper for creating an eventual from an 'expected'.
template <typename T, typename E>
auto ExpectedToEventual(tl::expected<T, E>&& expected) {
  // TODO(benh): support any error type that can be "stringified".
  static_assert(
      std::disjunction_v<
          std::is_base_of<std::exception, std::decay_t<E>>,
          std::is_base_of<std::exception_ptr, std::decay_t<E>>,
          std::is_same<std::string, std::decay_t<E>>,
          std::is_same<char*, std::decay_t<E>>>,
      "To use an 'expected' as an eventual it must have "
      "an error type derived from 'std::exception', "
      "or be a 'std::exception_ptr', or be string-like");

  return Eventual<T>()
      .template raises<
          std::conditional_t<
              std::disjunction_v<
                  std::is_base_of<std::exception, std::decay_t<E>>,
                  std::is_base_of<std::exception_ptr, std::decay_t<E>>>,
              E,
              std::runtime_error>>()
      // NOTE: we only care about "start" here because on "stop"
      // or "fail" we want to propagate that. We don't want to
      // override a "stop" with our failure because then
      // downstream eventuals might not stop but instead try and
      // recover from the error.
      .start([expected = std::move(expected)](auto& k) mutable {
        if (expected.has_value()) {
          if constexpr (std::is_void_v<T>) {
            return k.Start();
          } else {
            return k.Start(std::move(expected.value()));
          }
        } else {
          if constexpr (
              std::disjunction_v<
                  std::is_base_of<std::exception, std::decay_t<E>>,
                  std::is_base_of<std::exception_ptr, std::decay_t<E>>>) {
            return k.Fail(std::move(expected.error()));
          } else {
            return k.Fail(std::runtime_error(std::move(expected.error())));
          }
        }
      });
}

////////////////////////////////////////////////////////////////////////

// Wrapper for 'tl::expected' that allows it to seamlessly be composed
// with other eventuals.
//
// It's currently not possible to not wrap 'tl::expected' without
// dramatic changes to how we compose eventuals (e.g., we'd likely
// need to make 'k()' be a free-standing function and 'ValueFrom' and
// 'ErrorsFrom' be free-standing types).
//
// Even if we did that, however, we may still find ourselves wanting
// to have an "eventuals flavored expected" that is syntactic sugar
// for a 'std::expected'. For example, 'eventuals::expected<T>' might
// be an alias for 'std::expected<T, eventuals::Status>' where
// 'eventuals::Status' is an eventuals specific error type. We might
// then also have 'eventuals::grpc::expected<T>' be an alias for
// 'std::expected<T, eventuals::grpc::Status>', and so forth and so
// on.
//
// For now, an 'eventuals::expected<T>' uses a 'std::string' as the
// default error type to simplify calls to 'make_unexpected("...")'
// that take strings which is the majority (if not all) of calls.
template <typename Value_, typename Error_ = std::string>
class expected : public tl::expected<Value_, Error_> {
 public:
  // Providing 'ValueFrom, 'ErrorsFrom', and 'k()' to be able to
  // compose with other eventuals.
  template <typename Arg>
  using ValueFrom = Value_;

  template <typename Arg, typename Errors>
  using ErrorsFrom = tuple_types_union_t<
      std::tuple<
          std::conditional_t<
              std::is_base_of_v<std::exception, std::decay_t<Error_>>,
              Error_,
              std::runtime_error>>,
      Errors>;

  template <typename Arg, typename K>
  auto k(K k) && {
    return ExpectedToEventual(std::move(*this))
        .template k<Value_>(std::move(k));
  }

  using tl::expected<Value_, Error_>::expected;

  template <typename Downstream>
  static constexpr bool CanCompose = Downstream::ExpectsValue;

  using Expects = SingleValue;

  // Need explicit constructors for 'tl::expected', inherited
  // constructors are not sufficient.
  expected(const tl::expected<Value_, Error_>& that)
    : tl::expected<Value_, Error_>::expected(that) {}

  expected(tl::expected<Value_, Error_>&& that)
    : tl::expected<Value_, Error_>::expected(std::move(that)) {}
};

template <typename E>
using unexpected = tl::unexpected<E>;

using tl::make_unexpected;

////////////////////////////////////////////////////////////////////////

template <
    typename Left,
    typename T,
    typename E,
    std::enable_if_t<HasValueFrom<Left>::value, int> = 0>
[[nodiscard]] auto operator>>(Left left, tl::expected<T, E>&& expected) {
  return std::move(left)
      >> ExpectedToEventual(std::move(expected));
}

////////////////////////////////////////////////////////////////////////

template <
    typename Right,
    typename T,
    typename E,
    std::enable_if_t<HasValueFrom<Right>::value, int> = 0>
[[nodiscard]] auto operator>>(tl::expected<T, E>&& expected, Right right) {
  return ExpectedToEventual(std::move(expected))
      >> std::move(right);
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
