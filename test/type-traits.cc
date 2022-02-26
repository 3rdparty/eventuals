#include "eventuals/type-traits.h"

#include <string>

using eventuals::tuple_types_subset_v;
using eventuals::tuple_types_union_t;
using eventuals::tuple_types_unordered_equals_v;
using eventuals::types_contains_v;

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
