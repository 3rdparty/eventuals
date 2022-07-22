#include "eventuals/iterate.h"
#include "eventuals/promisify.h"
#include "eventuals/protobuf/collectors.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace eventuals::test {
namespace {

using testing::ElementsAre;

TEST(Collect, VectorToRepeatedPtrField) {
  std::vector<std::string> v = {"Hello", "World"};

  auto s = [&]() {
    return Iterate(v)
        >> Collect<google::protobuf::RepeatedPtrField>();
  };

  google::protobuf::RepeatedPtrField<std::string> result = *s();

  ASSERT_EQ(result.size(), 2);
  EXPECT_THAT(result, ElementsAre("Hello", "World"));

  // The initial vector should remain unchanged.
  ASSERT_EQ(v.size(), 2);
  EXPECT_THAT(v, ElementsAre("Hello", "World"));
}


TEST(Collect, MoveValueIntoRepeatedPtrField) {
  std::string initial_str = "Hello";

  auto s = [&]() {
    return Stream<std::string>()
               .context(false)
               .next([&](auto& was_completed, auto& k) {
                 if (!was_completed) {
                   was_completed = true;
                   k.Emit(std::move(initial_str));
                 } else {
                   k.Ended();
                 }
               })
        >> Collect<google::protobuf::RepeatedPtrField>();
  };

  google::protobuf::RepeatedPtrField<std::string> result = *s();

  ASSERT_EQ(result.size(), 1);
  EXPECT_THAT(result, ElementsAre("Hello"));

  EXPECT_EQ(initial_str, "");
}


TEST(Collect, VectorToRepeatedField) {
  std::vector<int> v = {42, 25};

  auto s = [&]() {
    return Iterate(v)
        >> Collect<google::protobuf::RepeatedField>();
  };

  google::protobuf::RepeatedField<int> result = *s();

  ASSERT_EQ(result.size(), 2);
  EXPECT_THAT(result, ElementsAre(42, 25));

  // The initial vector should remain unchanged.
  ASSERT_EQ(v.size(), 2);
  EXPECT_THAT(v, ElementsAre(42, 25));
}

} // namespace
} // namespace eventuals::test
