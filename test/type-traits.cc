#include "eventuals/type-traits.h"

#include <string>

using eventuals::tuple_types_contains_subtype_v;
using eventuals::tuple_types_subset_subtype_v;
using eventuals::tuple_types_subset_v;
using eventuals::tuple_types_subtract_t;
using eventuals::tuple_types_union_all_t;
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
    !tuple_types_subset_subtype_v<
        std::tuple<B, A>,
        std::tuple<B, D>>);

////////////////////////////////////////////////////////////////////////
