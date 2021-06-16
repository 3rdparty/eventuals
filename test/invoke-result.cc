#include "gtest/gtest.h"

#include "stout/invoke-result.h"

using stout::InvokeResultUnknownArgs;


template <typename T>
struct Type {};


TEST(InvokeResultUnknownArgs, Lvalue)
{
  auto f = [](Type<int>) {
    return std::declval<Type<int>>();
  };

  static_assert(
      std::is_same_v<
        InvokeResultUnknownArgs<decltype(f)>::type,
        Type<int>>);
}


TEST(InvokeResultUnknownArgs, LValueReference)
{
  auto f = [](Type<int>&) {
    return std::declval<Type<int>>();
  };

  static_assert(
      std::is_same_v<
        InvokeResultUnknownArgs<decltype(f)>::type,
        Type<int>>);
}


TEST(InvokeResultUnknownArgs, ConstLValueReference)
{
  auto f = [](const Type<int>&) {
    return std::declval<Type<int>>();
  };

  static_assert(
      std::is_same_v<
        InvokeResultUnknownArgs<decltype(f)>::type,
        Type<int>>);
}


TEST(InvokeResultUnknownArgs, RValueReference)
{
  auto f = [](Type<int>&&) {
    return std::declval<Type<int>>();
  };

  static_assert(
      std::is_same_v<
        InvokeResultUnknownArgs<decltype(f)>::type,
        Type<int>>);
}


TEST(InvokeResultUnknownArgs, MultipleArgs)
{
  auto f = [](Type<int>&&, const Type<int>&, Type<int>*) {
    return std::declval<Type<int>>();
  };

  static_assert(
      std::is_same_v<
        InvokeResultUnknownArgs<decltype(f)>::type,
        Type<int>>);
}


TEST(InvokeResultUnknownArgs, Auto)
{
  auto g = [](int s) {
    return std::declval<Type<int>>();
  };
  
  auto f = [&](auto i) {
    return g(std::forward<decltype(i)>(i) + 1);
  };

  static_assert(
      std::is_same_v<
        InvokeResultUnknownArgs<decltype(f)>::type,
        Type<int>>);
}

