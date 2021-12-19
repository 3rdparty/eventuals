#include "eventuals/expected.h"

#include <string>

#include "eventuals/terminal.h"
#include "eventuals/then.h"
#include "gtest/gtest.h"

using eventuals::Expected;
using eventuals::Then;
using eventuals::Unexpected;

TEST(Expected, Construct) {
  Expected::Of<std::string> s = "hello world";

  ASSERT_TRUE(s);
  EXPECT_EQ("hello world", *s);

  s = Expected::Of<std::string>(s);

  ASSERT_TRUE(s);
  EXPECT_EQ("hello world", *s);

  s = Expected::Of<std::string>(std::move(s));

  ASSERT_TRUE(s);
  EXPECT_EQ("hello world", *s);
}

TEST(Expected, Match) {
  Expected::Of<std::string> s = "hello world";

  bool matched = s.Match(
      [](std::string& s) { return true; },
      [](auto) { return false; });

  EXPECT_TRUE(matched);
}

TEST(Expected, Exception) {
  Expected::Of<std::string> s = std::make_exception_ptr("error");

  bool matched = s.Match(
      [](std::string& s) { return true; },
      [](auto) { return false; });

  EXPECT_TRUE(!matched);
}

TEST(Expected, Unexpected) {
  auto f = [](bool b) -> Expected::Of<std::string> {
    if (b) {
      return Expected("hello");
    } else {
      return Unexpected("error");
    }
  };

  Expected::Of<std::string> s = f(true);

  bool matched = s.Match(
      [](std::string& s) { return true; },
      [](auto) { return false; });

  EXPECT_TRUE(matched);
}

TEST(Expected, Compose) {
  auto f = []() {
    return Expected(41);
  };

  auto e = [&]() {
    return f()
        | Then([](int i) {
             return Expected(i + 1);
           });
  };

  EXPECT_EQ(42, *e());
}
