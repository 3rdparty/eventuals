#include "stout/dns-resolver.h"

#include <regex>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "stout/event-loop.h"
#include "stout/lambda.h"
#include "stout/terminal.h"
#include "stout/then.h"
#include "test/event-loop-test.h"

using stout::eventuals::DomainNameResolve;
using stout::eventuals::EventLoop;
using stout::eventuals::Eventual;
using stout::eventuals::Lambda;
using stout::eventuals::Terminate;
using stout::eventuals::Then;

using testing::MockFunction;


TEST_F(EventLoopTest, IpSucceed) {
  std::string address = "docs.libuv.org", port = "6667";

  auto e = DomainNameResolve(address, port);

  auto [future_ip, e_] = Terminate(std::move(e));

  e_.Start();

  EventLoop::Default().Run();

  std::regex pattern_ip{R"(\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3})"};

  EXPECT_EQ(true, std::regex_match(future_ip.get(), pattern_ip));
}


TEST_F(EventLoopTest, IpFail) {
  std::string address = "wwww.google.com", port = "6667";

  auto e = DomainNameResolve(address, port);

  auto [future_ip, e_] = Terminate(std::move(e));
  e_.Start();

  EventLoop::Default().Run();

  EXPECT_THROW(future_ip.get(), std::string);
}


TEST_F(EventLoopTest, IpStop) {
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
      | Lambda([](int data) {
             return std::to_string(data);
           });

  auto [future, e_] = Terminate(std::move(e));
  e_.Start();
  EventLoop::Default().Run();
  EXPECT_THROW(future.get(), stout::eventuals::StoppedException);
}