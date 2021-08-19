#include <iostream>
#include <thread>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "stout/lambda.h"
#include "stout/libuv/uv-signal.h"
#include "stout/terminal.h"

using stout::eventuals::Eventual;
using stout::eventuals::Interrupt;
using stout::eventuals::Lambda;
using stout::eventuals::start;
using stout::eventuals::Terminate;
using stout::uv::Loop;
using stout::uv::Signal;

using namespace std::chrono_literals;


TEST(Libuv, SignalComposition) {
  Loop loop;
  auto e = Signal(loop, SIGQUIT)
      | Lambda([](auto&& signal_code) {
             return "quit";
           });
  auto [f, e_] = Terminate(std::move(e));
  start(e_);
  auto t = std::thread([]() {
    std::this_thread::sleep_for(1s);
    EXPECT_EQ(raise(SIGQUIT), 0);
  });
  loop.run();
  t.join();
  EXPECT_EQ(f.get(), "quit");
}


TEST(Libuv, SignalInterrupt) {
  Loop loop;
  auto [f, e] = Terminate(Signal(loop, SIGQUIT));
  Interrupt interrupt;
  e.Register(interrupt);
  start(e);
  interrupt.Trigger();
  loop.run();
  EXPECT_THROW(f.get(), stout::eventuals::StoppedException);
}