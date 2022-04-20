#include <string>
#include <vector>

#include "eventuals/collect.h"
#include "eventuals/map.h"
#include "eventuals/stream.h"
#include "eventuals/terminal.h"
#include "test/concurrent/concurrent.h"
#include "test/expect-throw-what.h"

namespace eventuals::test {
namespace {

// Tests when when upstream fails the result will be fail.
TYPED_TEST(ConcurrentTypedTest, StreamFail) {
  auto e = [&]() {
    return Stream<int>()
               .template raises<std::runtime_error>()
               .next([](auto& k) {
                 k.Fail(std::runtime_error("error"));
               })
        | this->ConcurrentOrConcurrentOrdered([]() {
            return Map([](int i) {
              return std::to_string(i);
            });
          })
        | Collect<std::vector<std::string>>();
  };

  static_assert(
      eventuals::tuple_types_unordered_equals_v<
          typename decltype(e())::template ErrorsFrom<void, std::tuple<>>,
          std::tuple<std::runtime_error>>);


  auto [future, k] = Terminate(e());

  k.Start();

  EXPECT_THROW_WHAT(future.get(), "error");
}

} // namespace
} // namespace eventuals::test
