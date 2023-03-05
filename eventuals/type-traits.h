#pragma once

#include <exception>
#include <experimental/type_traits>
#include <tuple>
#include <type_traits>
#include <variant>

////////////////////////////////////////////////////////////////////////

namespace eventuals {

struct Stopped;

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

template <typename>
inline constexpr bool always_false_v = false;

////////////////////////////////////////////////////////////////////////

// TODO(benh): Replace with std::type_identity from C++20.
template <typename T>
struct type_identity {
  using type = T;
};

////////////////////////////////////////////////////////////////////////

// Helper for getting a C/C++ "function type" which properly handles
// singular 'void' function arguments.
template <typename R, typename... Args>
struct function_type {
  using type = R(Args...);
};

template <typename R>
struct function_type<R, void> {
  using type = R();
};

template <typename R, typename... Args>
using function_type_t = typename function_type<R, Args...>::type;

////////////////////////////////////////////////////////////////////////

template <typename, typename = void>
struct HasEmplaceBack : std::false_type {
};

template <typename T>
struct HasEmplaceBack<
    T,
    std::void_t<decltype(std::declval<T>().emplace_back(
        std::declval<typename T::value_type&&>()))>>
  : std::true_type {
};

////////////////////////////////////////////////////////////////////////

template <typename, typename = void>
struct HasInsert : std::false_type {
};

template <typename T>
struct HasInsert<
    T,
    std::void_t<decltype(std::declval<T>().insert(
        std::declval<typename T::value_type&&>()))>>
  : std::true_type {
};

////////////////////////////////////////////////////////////////////////

template <typename Left, typename Right>
using tuple_types_concatenate_t = decltype(std::tuple_cat(
    std::declval<Left>(),
    std::declval<Right>()));

////////////////////////////////////////////////////////////////////////

template <typename T, typename... Ts>
using types_contains = std::disjunction<std::is_same<T, Ts>...>;

template <typename T, typename... Ts>
inline constexpr bool types_contains_v = types_contains<T, Ts...>::value;

////////////////////////////////////////////////////////////////////////

template <typename T, typename Tuple>
struct tuple_types_contains {
  static constexpr bool value = false;
};

template <typename T, typename... Ts>
struct tuple_types_contains<T, std::tuple<Ts...>> {
  static constexpr bool value = types_contains_v<T, Ts...>;
};

template <typename T, typename Tuple>
inline constexpr bool tuple_types_contains_v =
    tuple_types_contains<T, Tuple>::value;

////////////////////////////////////////////////////////////////////////

template <typename T, typename Tuple>
struct tuple_types_contains_subtype : std::false_type {};

template <typename T, typename... Ts>
struct tuple_types_contains_subtype<T, std::tuple<Ts...>> {
  static constexpr bool value = std::disjunction_v<std::is_base_of<Ts, T>...>;
};

template <typename T, typename Tuple>
static constexpr bool tuple_types_contains_subtype_v =
    tuple_types_contains_subtype<T, Tuple>::value;

////////////////////////////////////////////////////////////////////////

template <typename Left, typename Right>
struct tuple_types_subset_subtype : std::false_type {};

template <typename... Lefts, typename Right>
struct tuple_types_subset_subtype<std::tuple<Lefts...>, Right> {
  static constexpr bool value = std::conjunction_v<
      tuple_types_contains_subtype<Lefts, Right>...>;
};

template <typename Left, typename Right>
static constexpr bool tuple_types_subset_subtype_v =
    tuple_types_subset_subtype<Left, Right>::value;

////////////////////////////////////////////////////////////////////////

template <typename Left, typename Right>
struct tuple_types_subset : std::false_type {};

template <typename... Lefts, typename Right>
struct tuple_types_subset<std::tuple<Lefts...>, Right> {
  static constexpr bool value =
      std::conjunction_v<tuple_types_contains<Lefts, Right>...>;
};

template <typename Left, typename Right>
inline constexpr bool tuple_types_subset_v =
    tuple_types_subset<Left, Right>::value;

////////////////////////////////////////////////////////////////////////

template <typename Left, typename Right>
using tuple_types_unordered_equals = std::conjunction<
    tuple_types_subset<Left, Right>,
    tuple_types_subset<Right, Left>>;

template <typename Left, typename Right>
inline constexpr bool tuple_types_unordered_equals_v =
    tuple_types_unordered_equals<Left, Right>::value;

////////////////////////////////////////////////////////////////////////

template <typename, typename>
struct tuple_types_union;

template <>
struct tuple_types_union<std::tuple<>, std::tuple<>> {
  using intersection = std::tuple<>;
  using unique_left = std::tuple<>;
  using unique_right = std::tuple<>;
  using type = std::tuple<>;
};

template <typename... Lefts>
struct tuple_types_union<std::tuple<Lefts...>, std::tuple<>> {
  using intersection = std::tuple<>;
  using unique_left = std::tuple<Lefts...>;
  using unique_right = std::tuple<>;
  using type = std::tuple<Lefts...>;
};

template <typename... Rights>
struct tuple_types_union<std::tuple<>, std::tuple<Rights...>> {
  using intersection = std::tuple<>;
  using unique_left = std::tuple<>;
  using unique_right = std::tuple<Rights...>;
  using type = std::tuple<Rights...>;
};

template <typename Left, typename... Lefts, typename Right, typename... Rights>
struct tuple_types_union<
    std::tuple<Left, Lefts...>,
    std::tuple<Right, Rights...>> {
  using intersection = tuple_types_concatenate_t<
      std::conditional_t<
          types_contains_v<Left, Right, Rights...>,
          std::tuple<Left>,
          std::tuple<>>,
      typename tuple_types_union<
          std::tuple<Lefts...>,
          std::tuple<Right, Rights...>>::intersection>;

  using unique_left = tuple_types_concatenate_t<
      std::conditional_t<
          types_contains_v<Left, Right, Rights...>,
          std::tuple<>,
          std::tuple<Left>>,
      typename tuple_types_union<
          std::tuple<Lefts...>,
          std::tuple<Right, Rights...>>::unique_left>;

  using unique_right = tuple_types_concatenate_t<
      std::conditional_t<
          types_contains_v<Right, Left, Lefts...>,
          std::tuple<>,
          std::tuple<Right>>,
      typename tuple_types_union<
          std::tuple<Left, Lefts...>,
          std::tuple<Rights...>>::unique_right>;

  using type = tuple_types_concatenate_t<
      tuple_types_concatenate_t<unique_left, unique_right>,
      intersection>;
};

template <typename Left, typename Right>
using tuple_types_union_t = typename tuple_types_union<Left, Right>::type;

////////////////////////////////////////////////////////////////////////

template <typename...>
struct tuple_types_union_all;

template <typename Left, typename Right, typename... Tail>
struct tuple_types_union_all<Left, Right, Tail...> {
  using type = tuple_types_union_t<
      tuple_types_union_t<Left, Right>,
      typename tuple_types_union_all<Tail...>::type>;
};

template <typename Tuple>
struct tuple_types_union_all<Tuple> {
  using type = Tuple;
};

template <>
struct tuple_types_union_all<> {
  using type = std::tuple<>;
};

template <typename... Tuples>
using tuple_types_union_all_t = typename tuple_types_union_all<Tuples...>::type;

////////////////////////////////////////////////////////////////////////

template <typename...>
struct tuple_types_subtract {
  using type = std::tuple<>;
};

template <typename Left, typename... Lefts, typename Right>
struct tuple_types_subtract<
    std::tuple<Left, Lefts...>,
    Right> {
  using type = tuple_types_concatenate_t<
      std::conditional_t<
          tuple_types_contains_v<Left, Right>,
          std::tuple<>,
          std::tuple<Left>>,
      typename tuple_types_subtract<std::tuple<Lefts...>, Right>::type>;
};

template <typename Types, typename ExcludeTypes>
using tuple_types_subtract_t =
    typename tuple_types_subtract<Types, ExcludeTypes>::type;

////////////////////////////////////////////////////////////////////////

template <template <typename...> class T, typename Tuple>
struct apply_tuple_types;

template <template <typename...> class T, typename... Types>
struct apply_tuple_types<T, std::tuple<Types...>> {
  using type = T<Types...>;
};

template <template <typename...> class T, typename Tuple>
using apply_tuple_types_t = typename apply_tuple_types<T, Tuple>::type;

////////////////////////////////////////////////////////////////////////

template <typename... Errors>
inline constexpr bool check_errors_v =
    std::conjunction_v<
        std::is_base_of<std::exception, std::decay_t<Errors>>...>;

template <typename... Errors>
using check_errors_t =
    std::conditional_t<
        check_errors_v<Errors...>,
        std::true_type,
        std::false_type>;

////////////////////////////////////////////////////////////////////////

template <typename T>
struct is_variant : std::false_type {};

template <typename... Args>
struct is_variant<std::variant<Args...>> : std::true_type {};

template <typename T>
inline constexpr bool is_variant_v = is_variant<T>::value;

////////////////////////////////////////////////////////////////////////

template <typename T>
struct is_tuple : std::false_type {};

template <typename... Args>
struct is_tuple<std::tuple<Args...>> : std::true_type {};

template <typename T>
inline constexpr bool is_tuple_v = is_tuple<T>::value;

////////////////////////////////////////////////////////////////////////

template <typename V, typename T>
struct tuple_contains_exact_type;

template <typename V, typename T0, typename... T>
struct tuple_contains_exact_type<V, std::tuple<T0, T...>> {
  static constexpr bool value = tuple_contains_exact_type<V, std::tuple<T...>>::value;
};

template <typename V, typename... T>
struct tuple_contains_exact_type<V, std::tuple<V, T...>> {
  static constexpr bool value = true;
};

template <typename V>
struct tuple_contains_exact_type<V, std::tuple<>> {
  static constexpr bool value = false;
};

template <typename V, typename T>
inline constexpr bool tuple_contains_exact_type_v = tuple_contains_exact_type<V, T>::value;

////////////////////////////////////////////////////////////////////////

template <typename>
struct check_variant_errors;

template <typename... Errors>
struct check_variant_errors<std::variant<Errors...>> {
  static constexpr bool value = check_errors_v<Errors...>;
};

template <typename Variant>
inline constexpr bool check_variant_errors_v = check_variant_errors<Variant>::value;

////////////////////////////////////////////////////////////////////////

template <typename T>
struct LambdaType : public LambdaType<decltype(&T::operator())> {};

template <typename ClassType, typename ReturnType, typename... Args>
struct LambdaType<ReturnType (ClassType::*)(Args...) const> {
  static constexpr int size = sizeof...(Args);

  template <size_t i>
  struct arg {
    typedef typename std::tuple_element<i, std::tuple<Args...>>::type type;
  };

  typedef ReturnType result_type;
};

////////////////////////////////////////////////////////////////////////

template <typename T>
using has_operator = decltype(&T::operator());

template <typename T>
using is_default_lambda =
    std::experimental::is_detected<has_operator, T>;

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
