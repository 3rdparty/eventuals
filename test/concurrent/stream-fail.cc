#include <string>
#include <vector>

#include "eventuals/collect.hh"
#include "eventuals/map.hh"
#include "eventuals/promisify.hh"
#include "eventuals/stream.hh"
#include "gmock/gmock.h"
#include "test/concurrent/concurrent.hh"

namespace eventuals::test {
namespace {

using testing::StrEq;
using testing::ThrowsMessage;

// Tests when when upstream fails the result will be fail.
TYPED_TEST(ConcurrentTypedTest, StreamFail) {
  auto e = [&]() {
    return Stream<int>()
               .template raises<std::runtime_error>()
               .next([](auto& k) {
                 k.Fail(std::runtime_error("error"));
               })
        >> this->ConcurrentOrConcurrentOrdered([]() {
            return Map([](int i) {
              return std::to_string(i);
            });
          })
        >> Collect<std::vector>();
  };

  static_assert(
      eventuals::tuple_types_unordered_equals_v<
          typename decltype(e())::template ErrorsFrom<void, std::tuple<>>,
          std::tuple<std::runtime_error>>);

  EXPECT_THAT(
      [&]() { *e(); },
      ThrowsMessage<std::runtime_error>(StrEq("error")));
}

} // namespace
} // namespace eventuals::test
