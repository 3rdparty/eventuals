#include "eventuals/expected.h"

#include <string>

#include "gtest/gtest.h"

using eventuals::Expected;

TEST(Expected, Construct) {
  Expected<std::string> s = "hello world";

  ASSERT_TRUE(s);
  EXPECT_EQ("hello world", *s);

  s = Expected<std::string>(s);

  ASSERT_TRUE(s);
  EXPECT_EQ("hello world", *s);

  s = Expected<std::string>(std::move(s));

  ASSERT_TRUE(s);
  EXPECT_EQ("hello world", *s);
}

TEST(Expected, Match) {
  Expected<std::string> s = "hello world";

  bool matched = s.Match(
      [](std::string& s) { return true; },
      [](auto) { return false; });

  EXPECT_TRUE(matched);
}

TEST(Expected, Exception) {
  Expected<std::string> s = std::make_exception_ptr("error");

  bool matched = s.Match(
      [](std::string& s) { return true; },
      [](auto) { return false; });

  EXPECT_TRUE(!matched);
}
