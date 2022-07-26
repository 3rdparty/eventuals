#include "eventuals/protobuf/collectors.h"

#include "eventuals/iterate.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace eventuals::test {
namespace {

using testing::ElementsAre;

TEST(Collect, VectorToRepeatedPtrField) {
  std::vector<std::string> v = {"Hello", "World"};

  auto s = [&]() {
    return Iterate(v)
        | Collect<google::protobuf::RepeatedPtrField>();
  };

  google::protobuf::RepeatedPtrField<std::string> result = *s();

  ASSERT_EQ(2, result.size());
  EXPECT_THAT(v, ElementsAre("Hello", "World"));

  // The initial vector should remain unchanged.
  ASSERT_EQ(2, v.size());
  EXPECT_THAT(v, ElementsAre("Hello", "World"));
}


TEST(Collect, VectorToRepeatedField) {
  std::vector<int> v = {42, 25};

  auto s = [&]() {
    return Iterate(v)
        | Collect<google::protobuf::RepeatedField>();
  };

  google::protobuf::RepeatedField<int> result = *s();

  ASSERT_EQ(2, result.size());
  EXPECT_THAT(v, ElementsAre(42, 25));

  // The initial vector should remain unchanged.
  ASSERT_EQ(2, v.size());
  EXPECT_THAT(v, ElementsAre(42, 25));
}

} // namespace
} // namespace eventuals::test
