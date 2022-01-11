#include <string>
#include <vector>

#include "eventuals/collect.h"
#include "eventuals/map.h"
#include "eventuals/stream.h"
#include "eventuals/terminal.h"
#include "test/concurrent.h"

using eventuals::Collect;
using eventuals::Map;
using eventuals::Stream;
using eventuals::Terminate;

// Tests when when upstream stops the result will be stop.
TYPED_TEST(ConcurrentTypedTest, StreamStop) {
  auto e = [&]() {
    return Stream<int>()
               .next([](auto& k) {
                 k.Stop();
               })
        | this->ConcurrentOrConcurrentOrdered([]() {
            return Map([](int i) {
              return std::to_string(i);
            });
          })
        | Collect<std::vector<std::string>>();
  };

  auto [future, k] = Terminate(e());

  k.Start();

  EXPECT_THROW(future.get(), eventuals::StoppedException);
}
