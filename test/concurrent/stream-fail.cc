#include <string>
#include <vector>

#include "eventuals/collect.h"
#include "eventuals/map.h"
#include "eventuals/promisify.h"
#include "eventuals/stream.h"
#include "gmock/gmock.h"
#include "test/concurrent/concurrent.h"

namespace eventuals::test {
namespace {

using testing::ThrowsMessage;

// Tests when when upstream fails the result will be fail.
TYPED_TEST(ConcurrentTypedTest, StreamFail) {
  auto e = [&]() {
    return Stream<int>()
               .template raises<RuntimeError>()
               .next([](auto& k) {
                 k.Fail(RuntimeError("error"));
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
          std::tuple<RuntimeError>>);

  try {
    *e();
  } catch (const RuntimeError& error) {
    EXPECT_EQ(error.what(), "error");
  }
}

} // namespace
} // namespace eventuals::test
