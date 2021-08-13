#include "stout/libuv/dns-resolver.h"

#include <regex>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "stout/lambda.h"
#include "stout/libuv/loop.h"
#include "stout/terminal.h"
#include "stout/then.h"

using stout::eventuals::Eventual;
using stout::eventuals::fail;
using stout::eventuals::Lambda;
using stout::eventuals::start;
using stout::eventuals::stop;
using stout::eventuals::Terminate;
using stout::eventuals::Then;

using stout::uv::DomainNameResolver;
using stout::uv::Loop;

using testing::MockFunction;


TEST(Libuv, IpSucceed) {
  Loop loop;
  DomainNameResolver resolver;

  auto e = resolver(loop, "docs.libuv.org", "6667");

  auto [future_ip, e1] = Terminate(e);

  start(e1);

  loop.run();

  std::regex pattern_ip{R"(\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3})"};

  EXPECT_EQ(true, std::regex_match(future_ip.get(), pattern_ip));
}


TEST(Libuv, IpFail) {
  Loop loop;
  DomainNameResolver resolver;

  auto e = resolver(loop, "wwww.google.com", "6667");

  auto [future_ip, e1] = Terminate(e);
  start(e1);

  loop.run();

  EXPECT_THROW(future_ip.get(), stout::eventuals::FailedException);
}


TEST(Libuv, IpStop) {
  Loop loop;
  DomainNameResolver resolver;

  auto e = resolver(loop, "www.google.com", "6667")
      | Eventual<int>()
            .start([](auto& k, auto&& ip) {
              // imagine that we got ip, and we try to connect
              // in order to get some data (int) from db for example,
              // but there was an error and we stop our continuation
              bool error = true;
              if (error)
                stop(k);
              else
                succeed(k, 13);
            })
      | Lambda([](int data) {
             return std::to_string(data);
           });

  auto [future, e_] = Terminate(e);
  start(e_);
  loop.run();
  EXPECT_THROW(future.get(), stout::eventuals::StoppedException);
}