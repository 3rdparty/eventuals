#include "eventuals/collect.h"

#include <set>
#include <utility>
#include <vector>

#include "eventuals/iterate.h"
#include "google/protobuf/repeated_field.h"
#include "gtest/gtest.h"

template <typename Collection>
struct eventuals::EventualsCollector<
    Collection,
    std::enable_if_t<
        std::is_same_v<
            Collection,
            google::protobuf::RepeatedPtrField<
                typename Collection::value_type>>>> {
  // Since 'Add(T&& value)' takes ownership of value, we pass a copy.
  template <typename T>
  std::enable_if_t<
      std::is_convertible_v<
          T,
          typename Collection::value_type>>
  Collect(Collection& collection, T value) {
    collection.Add(std::move(value));
  }
};

template <typename Collection>
struct eventuals::EventualsCollector<
    Collection,
    std::enable_if_t<
        std::is_same_v<
            Collection,
            google::protobuf::RepeatedField<
                typename Collection::value_type>>>> {
  template <typename T>
  std::enable_if_t<
      std::is_convertible_v<
          T,
          typename Collection::value_type>>
  Collect(Collection& collection, T&& value) {
    collection.Add(value);
  }
};

namespace eventuals::test {
namespace {

TEST(Collect, VectorPass) {
  std::vector<int> v = {5, 12};

  auto s = [&]() {
    return Iterate(v)
        | Collect<std::vector<int>>();
  };

  std::vector<int> result = *s();

  ASSERT_EQ(2, result.size());
  EXPECT_EQ(5, result.at(0));
  EXPECT_EQ(12, result.at(1));

  // The initial vector should remain unchanged.
  ASSERT_EQ(2, v.size());
  EXPECT_EQ(5, v.at(0));
  EXPECT_EQ(12, v.at(1));
}


TEST(Collect, SetPass) {
  std::set<int> v = {5, 12};

  auto s = [&]() {
    return Iterate(v)
        | Collect<std::set<int>>();
  };

  std::set<int> result = *s();

  ASSERT_EQ(2, result.size());
  EXPECT_EQ(5, *result.begin());
  EXPECT_EQ(12, *++result.begin());

  // The initial set should remain unchanged.
  ASSERT_EQ(2, v.size());
  EXPECT_EQ(5, *v.begin());
  EXPECT_EQ(12, *++v.begin());
}


TEST(Collect, VectorToRepeatedPtrField) {
  std::vector<std::string> v = {"Hello", "World"};

  auto s = [&]() {
    return Iterate(v)
        | Collect<google::protobuf::RepeatedPtrField<std::string>>();
  };

  google::protobuf::RepeatedPtrField<std::string> result = *s();

  ASSERT_EQ(2, result.size());
  EXPECT_EQ("Hello", *result.begin());
  EXPECT_EQ("World", *(result.begin() + 1));

  // The initial vector should remain unchanged.
  ASSERT_EQ(2, v.size());
  EXPECT_EQ("Hello", *v.begin());
  EXPECT_EQ("World", *(v.begin() + 1));
}


TEST(Collect, VectorToRepeatedField) {
  std::vector<int> v = {42, 25};

  auto s = [&]() {
    return Iterate(v)
        | Collect<google::protobuf::RepeatedField<int>>();
  };

  google::protobuf::RepeatedField<int> result = *s();

  ASSERT_EQ(2, result.size());
  EXPECT_EQ(42, *result.begin());
  EXPECT_EQ(25, *(result.begin() + 1));

  // The initial vector should remain unchanged.
  ASSERT_EQ(2, v.size());
  EXPECT_EQ(42, *v.begin());
  EXPECT_EQ(25, *(v.begin() + 1));
}

} // namespace
} // namespace eventuals::test
