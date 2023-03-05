#include "eventuals/type-traits.h"

#include <exception>
#include <string>

#include "eventuals/task.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals::test {
namespace {

////////////////////////////////////////////////////////////////////////

static_assert(types_contains_v<int, double, int>);

static_assert(!types_contains_v<int, double, const char*>);

////////////////////////////////////////////////////////////////////////

static_assert(
    tuple_types_subset_v<
        std::tuple<int>,
        std::tuple<int>>);

static_assert(
    !tuple_types_subset_v<
        std::tuple<std::string>,
        std::tuple<int>>);

static_assert(
    tuple_types_subset_v<
        std::tuple<>,
        std::tuple<int>>);

static_assert(
    tuple_types_subset_v<
        std::tuple<int>,
        std::tuple<int, double>>);

static_assert(
    !tuple_types_subset_v<
        std::tuple<int>,
        std::tuple<double, std::string>>);

static_assert(
    tuple_types_subset_v<
        std::tuple<int, std::string>,
        std::tuple<int, double, std::string>>);

////////////////////////////////////////////////////////////////////////

static_assert(
    tuple_types_unordered_equals_v<
        std::tuple<>,
        std::tuple<>>);

static_assert(
    !tuple_types_unordered_equals_v<
        std::tuple<>,
        std::tuple<int>>);

static_assert(
    !tuple_types_unordered_equals_v<
        std::tuple<int>,
        std::tuple<>>);

static_assert(
    tuple_types_unordered_equals_v<
        std::tuple<int>,
        std::tuple<int>>);

static_assert(
    tuple_types_unordered_equals_v<
        std::tuple<int, std::string>,
        std::tuple<std::string, int>>);

static_assert(
    !tuple_types_unordered_equals_v<
        std::tuple<int, std::string>,
        std::tuple<std::string, int, double>>);

////////////////////////////////////////////////////////////////////////

static_assert(
    std::is_same_v<
        tuple_types_union_t<
            std::tuple<>,
            std::tuple<>>,
        std::tuple<>>);

static_assert(
    std::is_same_v<
        tuple_types_union_t<
            std::tuple<>,
            std::tuple<int>>,
        std::tuple<int>>);

static_assert(
    std::is_same_v<
        tuple_types_union_t<
            std::tuple<int>,
            std::tuple<>>,
        std::tuple<int>>);

static_assert(
    std::is_same_v<
        tuple_types_union_t<
            std::tuple<int>,
            std::tuple<int>>,
        std::tuple<int>>);

////////////////////////////////////////////////////////////////////////

static_assert(
    std::is_same_v<
        tuple_types_union_all_t<
            std::tuple<>>,
        std::tuple<>>);

static_assert(
    std::is_same_v<
        tuple_types_union_all_t<
            std::tuple<int>,
            std::tuple<int>,
            std::tuple<int>>,
        std::tuple<int>>);

static_assert(
    tuple_types_unordered_equals_v<
        tuple_types_union_all_t<
            std::tuple<int>,
            std::tuple<int>,
            std::tuple<int, double>>,
        std::tuple<int, double>>);

static_assert(
    tuple_types_unordered_equals_v<
        tuple_types_union_all_t<
            std::tuple<int>,
            std::tuple<float>,
            std::tuple<double>>,
        std::tuple<int, float, double>>);

static_assert(
    tuple_types_unordered_equals_v<
        tuple_types_union_all_t<
            std::tuple<int>,
            std::tuple<float>,
            std::tuple<double>,
            std::tuple<int>,
            std::tuple<float>>,
        std::tuple<int, float, double>>);

////////////////////////////////////////////////////////////////////////

static_assert(
    std::is_same_v<
        tuple_types_subtract_t<
            std::tuple<>,
            std::tuple<>>,
        std::tuple<>>);

static_assert(
    std::is_same_v<
        tuple_types_subtract_t<
            std::tuple<>,
            std::tuple<int>>,
        std::tuple<>>);

static_assert(
    std::is_same_v<
        tuple_types_subtract_t<
            std::tuple<int>,
            std::tuple<>>,
        std::tuple<int>>);

static_assert(
    std::is_same_v<
        tuple_types_subtract_t<
            std::tuple<int>,
            std::tuple<double, std::string>>,
        std::tuple<int>>);

static_assert(
    std::is_same_v<
        tuple_types_subtract_t<
            std::tuple<int, float, double>,
            std::tuple<float>>,
        std::tuple<int, double>>);

static_assert(
    std::is_same_v<
        tuple_types_subtract_t<
            std::tuple<int, float, double>,
            std::tuple<std::string>>,
        std::tuple<int, float, double>>);

////////////////////////////////////////////////////////////////////////

struct A {};

struct B : public A {};

struct C : public B {};

struct D;

static_assert(
    tuple_types_contains_subtype_v<
        A,
        std::tuple<A, C>>);

static_assert(
    tuple_types_contains_subtype_v<
        B,
        std::tuple<D, A>>);

static_assert(
    !tuple_types_contains_subtype_v<
        A,
        std::tuple<B>>);

static_assert(
    !tuple_types_contains_subtype_v<
        A,
        std::tuple<D>>);

static_assert(
    tuple_types_contains_subtype_v<
        C,
        std::tuple<A>>);

////////////////////////////////////////////////////////////////////////

static_assert(tuple_contains_exact_type_v<
              std::runtime_error,
              std::tuple<int, std::runtime_error>>);

static_assert(!tuple_contains_exact_type_v<
              std::runtime_error,
              std::tuple<>>);

static_assert(!tuple_contains_exact_type_v<
              std::runtime_error,
              std::tuple<int, std::exception>>);

////////////////////////////////////////////////////////////////////////

static_assert(
    tuple_types_subset_subtype_v<
        std::tuple<B>,
        std::tuple<B>>);

static_assert(
    tuple_types_subset_subtype_v<
        std::tuple<B, C>,
        std::tuple<A>>);

static_assert(
    tuple_types_subset_subtype_v<
        std::tuple<B, C>,
        std::tuple<A, B, D>>);

static_assert(
    tuple_types_subset_subtype_v<
        std::tuple<>,
        std::tuple<std::runtime_error>>);

static_assert(
    !tuple_types_subset_subtype_v<
        std::tuple<B, A>,
        std::tuple<B, D>>);

////////////////////////////////////////////////////////////////////////

static_assert(
    std::is_same_v<
        apply_tuple_types_t<
            Task::Of<int>::Raises,
            std::tuple<>>,
        Task::Of<int>::Raises<>>);

static_assert(
    std::is_same_v<
        apply_tuple_types_t<
            Task::Of<int>::Raises,
            std::tuple<std::overflow_error>>,
        Task::Of<int>::Raises<std::overflow_error>>);

static_assert(
    std::is_same_v<
        apply_tuple_types_t<
            Task::Of<int>::Raises,
            std::tuple<std::overflow_error, std::underflow_error>>,
        Task::Of<int>::Raises<std::overflow_error, std::underflow_error>>);

////////////////////////////////////////////////////////////////////////

static_assert(!check_errors_v<int>);

static_assert(!check_errors_v<int, std::string>);

static_assert(!check_errors_v<std::string, std::runtime_error>);

static_assert(check_errors_v<std::overflow_error, std::runtime_error>);

////////////////////////////////////////////////////////////////////////

} // namespace
} // namespace eventuals::test

////////////////////////////////////////////////////////////////////////
