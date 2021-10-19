#include "eventuals/dns-resolver.h"

#include <regex>

#include "eventuals/event-loop.h"
#include "eventuals/terminal.h"
#include "eventuals/then.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "test/event-loop-test.h"

using eventuals::DomainNameResolve;
using eventuals::EventLoop;
using eventuals::Eventual;
using eventuals::Terminate;
using eventuals::Then;

using testing::MockFunction;

class DomainNameResolveTest : public EventLoopTest {};

TEST_F(DomainNameResolveTest, Succeed) {
  std::string address = "localhost", port = "6667";

  auto e = DomainNameResolve(address, port);

  auto [future, k] = Terminate(std::move(e));

  k.Start();

  EventLoop::Default().Run();

  EXPECT_TRUE(
      std::regex_match(
          future.get(),
          std::regex{R"(\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3})"}));
}


TEST_F(DomainNameResolveTest, Fail) {
  std::string address = ";;!(*#!()%$%*(#*!~_+", port = "6667";

  auto e = DomainNameResolve(address, port);

  auto [future, k] = Terminate(std::move(e));

  k.Start();

  EventLoop::Default().Run();

  EXPECT_THROW(future.get(), std::string);
}


TEST_F(DomainNameResolveTest, Stop) {
  std::string address = "localhost", port = "6667";
  auto e = DomainNameResolve(address, port)
      | Eventual<int>()
            .start([](auto& k, auto&& ip) {
              // Imagine that we got ip, and we try to connect
              // in order to get some data (int) from db for example,
              // but there was an error and we stop our continuation
              bool error = true;
              if (error) {
                k.Stop();
              } else
                k.Start(13);
            })
      | Then([](int data) {
             return std::to_string(data);
           });

  auto [future, k] = Terminate(std::move(e));

  k.Start();

  EventLoop::Default().Run();

  EXPECT_THROW(future.get(), eventuals::StoppedException);
}
