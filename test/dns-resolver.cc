#include "stout/dns-resolver.h"

#include <regex>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "stout/event-loop.h"
#include "stout/terminal.h"
#include "stout/then.h"
#include "test/event-loop-test.h"

using stout::eventuals::DomainNameResolve;
using stout::eventuals::EventLoop;
using stout::eventuals::Eventual;
using stout::eventuals::Terminate;
using stout::eventuals::Then;

using testing::MockFunction;

class DomainNameResolveTest : public EventLoopTest {};

TEST_F(DomainNameResolveTest, Succeed) {
  std::string address = "docs.libuv.org", port = "6667";

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
  std::string address = "wwww.google.com", port = "6667";

  auto e = DomainNameResolve(address, port);

  auto [future, k] = Terminate(std::move(e));

  k.Start();

  EventLoop::Default().Run();

  EXPECT_THROW(future.get(), std::string);
}


TEST_F(DomainNameResolveTest, Stop) {
  std::string address = "www.google.com", port = "6667";
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

  EXPECT_THROW(future.get(), stout::eventuals::StoppedException);
}
