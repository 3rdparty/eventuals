#pragma once

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

// Helper for checking if a template type exists. Can be used with
// 'std::void_t', for example:
//
//   template <typename, typename = void>
//   struct has_template_foo : std::false_type {};
//
//   template <typename T>
//   struct has_template_foo<T, std::void_t<void_template<T::template foo>>>
//     : std::true_type {};
template <template <typename...> class>
struct void_template {
  using type = void;
};

////////////////////////////////////////////////////////////////////////

// TODO(benh): Replace with std::type_identity from C++20.
template <typename T>
struct type_identity {
  using type = T;
};

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
