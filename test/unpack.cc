#include "eventuals/unpack.hh"

#include <string>

#include "eventuals/just.hh"
#include "eventuals/promisify.hh"
#include "eventuals/then.hh"
#include "gtest/gtest.h"

namespace eventuals::test {
namespace {

TEST(Unpack, Unpack) {
  auto e = []() {
    return Just(std::tuple{4, "2"})
        >> Then(Unpack([](int i, std::string&& s) {
             return std::to_string(i) + s;
           }));
  };

  EXPECT_EQ("42", *e());
}

} // namespace
} // namespace eventuals::test
