#include <iostream>
#include <thread>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "stout/lambda.h"
#include "stout/libuv/uv-signal.h"
#include "stout/terminal.h"

using stout::eventuals::Eventual;
using stout::eventuals::Lambda;
using stout::eventuals::start;
using stout::eventuals::Terminate;
using stout::uv::Loop;
using stout::uv::Signal;

using namespace std::chrono_literals;

TEST(Libuv, SignalQuit) {
  Loop loop;
  Signal signal;
  auto e = signal(loop, SIGQUIT);
  auto [f, e_] = Terminate(e);
  start(e_);
  auto t = std::thread([]() {
    std::this_thread::sleep_for(1s);
    EXPECT_EQ(raise(SIGQUIT), 0);
  });
  loop.run();
  t.join();
}


TEST(Libuv, SignalComposition) {
  Loop loop;
  Signal signal;
  auto e = signal(loop, SIGQUIT)
      | Lambda([]() {
             return "quit";
           });
  auto [f, e_] = Terminate(e);
  start(e_);
  auto t = std::thread([]() {
    std::this_thread::sleep_for(1s);
    EXPECT_EQ(raise(SIGQUIT), 0);
  });
  loop.run();
  t.join();
  EXPECT_EQ(f.get(), "quit");
}
