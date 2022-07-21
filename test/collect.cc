#include "eventuals/collect.h"

#include <set>
#include <vector>

#include "eventuals/iterate.h"
#include "google/protobuf/repeated_field.h"
#include "gtest/gtest.h"

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


TEST(Collect, DoNothing) {
  std::vector<std::string> v = {"Hello", "World"};

  auto s = [&]() {
    return Iterate(v)
        | Collect<std::vector<std::string>>(
               [](auto& container, auto&& value) {
                 // Literally do nothing.
                 (void) container;
                 (void) value;
               });
  };

  std::vector<std::string> result = *s();

  ASSERT_EQ(0, result.size());

  // The initial vector should remain unchanged.
  ASSERT_EQ(2, v.size());
  EXPECT_EQ("Hello", *v.begin());
  EXPECT_EQ("World", *(v.begin() + 1));
}


TEST(Collect, VectorToRepeatedPtrFieldCopy) {
  std::vector<std::string> v = {"Hello", "World"};

  auto s = [&]() {
    return Iterate(v)
        | Collect<google::protobuf::RepeatedPtrField<std::string>>(
               [](auto& container, auto value) {
                 container.Add(std::move(value));
               });
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


TEST(Collect, VectorToRepeatedPtrFieldMove) {
  std::vector<std::string> v = {"Hello", "World"};

  auto s = [&]() {
    return Iterate(v)
        | Collect<google::protobuf::RepeatedPtrField<std::string>>(
               [](auto& container, auto&& value) {
                 container.Add(std::move(value));
               });
  };

  google::protobuf::RepeatedPtrField<std::string> result = *s();

  ASSERT_EQ(2, result.size());
  EXPECT_EQ("Hello", *result.begin());
  EXPECT_EQ("World", *(result.begin() + 1));

  // The initial vector should have empty strings.
  ASSERT_EQ(2, v.size());
  EXPECT_EQ("", *v.begin());
  EXPECT_EQ("", *(v.begin() + 1));
}


// No need to have a separate move test, since google::protobuf::RepeatedField
// must be used with primitive types.
// Quote: RepeatedField is used to represent repeated fields of a primitive type
// (in other words, everything except strings and nested Messages).
// check_line_length skip
// Source: https://developers.google.com/protocol-buffers/docs/reference/cpp/google.protobuf.repeated_field
TEST(Collect, VectorToRepeatedFieldCopy) {
  std::vector<int> v = {42, 25};

  auto s = [&]() {
    return Iterate(v)
        | Collect<google::protobuf::RepeatedField<int>>(
               [](auto& container, auto&& value) {
                 container.Add(value);
               });
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
