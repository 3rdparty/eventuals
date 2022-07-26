#include "eventuals/collect.h"

#include <set>
#include <vector>

#include "eventuals/iterate.h"
#include "eventuals/promisify.h"
#include "gtest/gtest.h"

namespace eventuals::test {
namespace {

TEST(Collect, CommonVectorPass) {
  std::vector<int> v = {5, 12};

  auto s = [&]() {
    return Iterate(v)
        | Collect<std::vector<int>>();
  };

  std::vector<int> result = *s();

  ASSERT_EQ(2, result.size());
  EXPECT_EQ(5, result.at(0));
  EXPECT_EQ(12, result.at(1));
}

TEST(Collect, CommonSetPass) {
  std::set<int> v = {5, 12};

  auto s = [&]() {
    return Iterate(v)
        | Collect<std::set<int>>();
  };

  std::set<int> result = *s();

  ASSERT_EQ(2, result.size());
  EXPECT_EQ(5, *result.begin());
  EXPECT_EQ(12, *++result.begin());
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
}

} // namespace
} // namespace eventuals::test
