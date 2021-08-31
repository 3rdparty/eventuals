#include "stout/signal.h"

#include <thread>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "stout/event-loop.h"
#include "stout/terminal.h"
#include "stout/then.h"
#include "test/event-loop-test.h"

using stout::eventuals::EventLoop;
using stout::eventuals::Eventual;
using stout::eventuals::Interrupt;
using stout::eventuals::Signal;
using stout::eventuals::Terminate;
using stout::eventuals::Then;

using namespace std::chrono_literals;

// Windows notes!
// On Windows calls to raise() or abort() to programmatically
// raise a signal are not detected by libuv; these will not
// trigger a signal watcher. The link below will be helpful!
// http://docs.libuv.org/en/v1.x/signal.html?highlight=uv_signal_t#c.uv_signal_t

// TODO: think later about possible way of raising signals on
// Windows.

#if !defined(_WIN32)

TEST_F(EventLoopTest, SignalComposition) {
  auto e = Signal(SIGQUIT)
      | Then([](auto&& signal_code) {
             return "quit";
           });
  auto [f, e_] = Terminate(std::move(e));
  e_.Start();
  auto t = std::thread([]() {
    std::this_thread::sleep_for(10ms);
    EXPECT_EQ(raise(SIGQUIT), 0);
  });
  EventLoop::Default().Run();
  t.join();
  EXPECT_EQ(f.get(), "quit");
}


TEST_F(EventLoopTest, SignalInterrupt) {
  auto [f, e] = Terminate(Signal(SIGQUIT));
  Interrupt interrupt;
  e.Register(interrupt);
  e.Start();
  interrupt.Trigger();
  EventLoop::Default().Run();
  EXPECT_THROW(f.get(), stout::eventuals::StoppedException);
}

#endif

#if defined(_WIN32)

TEST_F(EventLoopTest, SignalInterrupt) {
  auto [f, e] = Terminate(Signal(SIGINT));
  Interrupt interrupt;
  e.Register(interrupt);
  e.Start();
  interrupt.Trigger();
  EventLoop::Default().Run();
  EXPECT_THROW(f.get(), stout::eventuals::StoppedException);
}

#endif