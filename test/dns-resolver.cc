#include "eventuals/dns-resolver.h"

#include <regex>

#include "eventuals/event-loop.h"
#include "eventuals/then.h"
#include "eventuals/type-traits.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "test/event-loop-test.h"
#include "test/promisify-for-test.h"

namespace eventuals::test {
namespace {

using testing::MockFunction;
using testing::StrEq;
using testing::ThrowsMessage;

class DomainNameResolveTest : public EventLoopTest {};

TEST_F(DomainNameResolveTest, Succeed) {
  std::string address = "localhost", port = "6667";

  auto e = DomainNameResolve(address, port);

  static_assert(
      eventuals::tuple_types_unordered_equals_v<
          typename decltype(e)::template ErrorsFrom<void, std::tuple<>>,
          std::tuple<std::runtime_error>>);

  auto [future, k] = PromisifyForTest(std::move(e));

  k.Start();

  EventLoop::Default().RunUntil(future);

  EXPECT_TRUE(
      std::regex_match(
          future.get(),
          std::regex{R"(\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3})"}));
}


TEST_F(DomainNameResolveTest, Fail) {
  std::string address = ";;!(*#!()%$%*(#*!~_+", port = "6667";

  auto e = DomainNameResolve(address, port);

  static_assert(
      eventuals::tuple_types_unordered_equals_v<
          typename decltype(e)::template ErrorsFrom<void, std::tuple<>>,
          std::tuple<std::runtime_error>>);

  auto [future, k] = PromisifyForTest(std::move(e));

  k.Start();

  EventLoop::Default().RunUntil(future);

  EXPECT_THAT(
      // NOTE: capturing 'future' as a pointer because until C++20 we
      // can't capture a "local binding" by reference and there is a
      // bug with 'EXPECT_THAT' that forces our lambda to be const so
      // if we capture it by copy we can't call 'get()' because that
      // is a non-const function.
      [future = &future]() { future->get(); },
      ThrowsMessage<std::runtime_error>(StrEq("EAI_NONAME")));
}


TEST_F(DomainNameResolveTest, Stop) {
  std::string address = "localhost", port = "6667";
  auto e = DomainNameResolve(address, port)
      | Eventual<int>()
            .start([](auto& k, auto&& ip) {
              // Imagine that we got ip, and we try to connect
              // in order to get some data (int) from db for example,
              // but there was an error and we stop our continuation.
              bool error = true;
              if (error) {
                k.Stop();
              } else
                k.Start(13);
            })
      | Then([](int data) {
             return std::to_string(data);
           });

  auto [future, k] = PromisifyForTest(std::move(e));

  k.Start();

  EventLoop::Default().RunUntil(future);

  EXPECT_THROW(future.get(), eventuals::StoppedException);
}

TEST_F(DomainNameResolveTest, Raises) {
  std::string address = "localhost", port = "6667";
  auto e = DomainNameResolve(address, port)
      | Eventual<int>()
            .raises<std::overflow_error>()
            .start([](auto& k, auto&& ip) {
              // Imagine that we got ip, and we try to connect
              // in order to get some data (int) from db for example,
              // but there was an error and we stop our continuation.
              bool error = true;
              if (error) {
                k.Fail(std::overflow_error("error"));
              } else
                k.Start(13);
            })
      | Then([](int data) {
             return std::to_string(data);
           });

  static_assert(
      eventuals::tuple_types_unordered_equals_v<
          typename decltype(e)::template ErrorsFrom<void, std::tuple<>>,
          std::tuple<std::runtime_error, std::overflow_error>>);

  auto [future, k] = PromisifyForTest(std::move(e));

  k.Start();

  EventLoop::Default().RunUntil(future);

  EXPECT_THAT(
      // NOTE: capturing 'future' as a pointer because until C++20 we
      // can't capture a "local binding" by reference and there is a
      // bug with 'EXPECT_THAT' that forces our lambda to be const so
      // if we capture it by copy we can't call 'get()' because that
      // is a non-const function.
      [future = &future]() { future->get(); },
      ThrowsMessage<std::runtime_error>(StrEq("error")));
}

} // namespace
} // namespace eventuals::test
