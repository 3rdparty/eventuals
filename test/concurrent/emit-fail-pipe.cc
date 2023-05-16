#include <string>
#include <vector>

#include "eventuals/collect.h"
#include "eventuals/eventual.h"
#include "eventuals/interrupt.h"
#include "eventuals/let.h"
#include "eventuals/map.h"
#include "eventuals/pipe.h"
#include "eventuals/stream.h"
#include "gmock/gmock.h"
#include "test/concurrent/concurrent.h"
#include "test/promisify-for-test.h"

namespace eventuals::test {
namespace {

using testing::ThrowsMessage;

// Tests that when one of the 'Concurrent()' eventuals fails it may
// signal 'upstream' to be done by closing it using 'Pipe'.
TYPED_TEST(ConcurrentTypedTest, EmitFailPipe) {
  Pipe<int> pipe;
  *pipe.Write(1);

  auto e = [&]() {
    return pipe.Read()
        >> this->ConcurrentOrConcurrentOrdered([&]() {
            return Map(Let([&](int& i) {
              return Eventual<std::string>()
                  .raises<RuntimeError>()
                  .start([&](auto& k) {
                    k.Fail(RuntimeError("error"));
                  });
            }));
          })
        >> Collect<std::vector>();
  };

  static_assert(
      eventuals::tuple_types_unordered_equals_v<
          typename decltype(e())::template ErrorsFrom<void, std::tuple<>>,
          std::tuple<RuntimeError>>);


  auto [future, k] = PromisifyForTest(e());

  k.Start();

  EXPECT_EQ(
      std::future_status::timeout,
      future.wait_for(std::chrono::seconds(0)));

  *pipe.Close();

  try {
    future.get();
  } catch (const RuntimeError& error) {
    EXPECT_EQ(error.what(), "error");
  }
}

} // namespace
} // namespace eventuals::test
