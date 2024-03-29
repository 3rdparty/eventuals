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
          std::tuple<RuntimeError>>);

  EXPECT_TRUE(
      std::regex_match(
          *std::move(e),
          std::regex{R"(\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3})"}));
}


TEST_F(DomainNameResolveTest, Fail) {
  std::string address = ";;!(*#!()%$%*(#*!~_+", port = "6667";

  auto e = DomainNameResolve(address, port);

  static_assert(
      eventuals::tuple_types_unordered_equals_v<
          typename decltype(e)::template ErrorsFrom<void, std::tuple<>>,
          std::tuple<RuntimeError>>);

  EXPECT_THAT(
      [&]() { *std::move(e); },
      ThrowsMessage<RuntimeError>(StrEq("EAI_NONAME")));
}


TEST_F(DomainNameResolveTest, Stop) {
  std::string address = "localhost", port = "6667";
  auto e = DomainNameResolve(address, port)
      >> Eventual<int>()
             .start([](auto& k, std::string&& ip) {
               // Imagine that we got ip, and we try to connect
               // in order to get some data (int) from db for example,
               // but there was an error and we stop our continuation.
               bool error = true;
               if (error) {
                 k.Stop();
               } else
                 k.Start(13);
             })
      >> Then([](int data) {
             return std::to_string(data);
           });

  EXPECT_THROW(*std::move(e), eventuals::Stopped);
}

TEST_F(DomainNameResolveTest, Raises) {
  struct MyError : public Error {
    const char* what() const noexcept override {
      return "child error";
    }
  };

  std::string address = "localhost", port = "6667";
  auto e = DomainNameResolve(address, port)
      >> Eventual<int>()
             .raises<MyError>()
             .start([](auto& k, std::string&& ip) {
               // Imagine that we got ip, and we try to connect
               // in order to get some data (int) from db for example,
               // but there was an error and we stop our continuation.
               bool error = true;
               if (error) {
                 k.Fail(MyError{});
               } else
                 k.Start(13);
             })
      >> Then([](int data) {
             return std::to_string(data);
           });

  static_assert(
      eventuals::tuple_types_unordered_equals_v<
          typename decltype(e)::template ErrorsFrom<void, std::tuple<>>,
          std::tuple<RuntimeError, MyError>>);

  EXPECT_THAT(
      [&]() { *std::move(e); },
      ThrowsMessage<MyError>(StrEq("child error")));
}

} // namespace
} // namespace eventuals::test
