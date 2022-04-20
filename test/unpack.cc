#include "eventuals/unpack.h"

#include <string>

#include "eventuals/just.h"
#include "eventuals/terminal.h"
#include "eventuals/then.h"
#include "gtest/gtest.h"

namespace eventuals {
namespace {
TEST(Unpack, Unpack) {
  auto e = []() {
    return Just(std::tuple{4, "2"})
        | Then(Unpack([](int i, std::string&& s) {
             return std::to_string(i) + s;
           }));
  };

  EXPECT_EQ("42", *e());
}
} // namespace
} // namespace eventuals
