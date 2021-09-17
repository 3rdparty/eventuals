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
//
// On Windows calls to raise() or abort() to programmatically
// raise a signal are not detected by libuv; these will not
// trigger a signal watcher. The link below will be helpful!
// http://docs.libuv.org/en/v1.x/signal.html?highlight=uv_signal_t#c.uv_signal_t

// TODO: think later about possible way of raising signals on Windows.

class SignalTest : public EventLoopTest {};

#if !defined(_WIN32)

TEST_F(SignalTest, SignalComposition) {
  auto e = Signal(SIGQUIT)
      | Then([](auto signum) {
             return "quit";
           });

  auto [future, k] = Terminate(std::move(e));

  k.Start();

  // NOTE: now that we've started the continuation 'k' we will have
  // submitted a callback to the event loop and thus by explicitly
  // submitting another callback we will ensure there is a
  // happens-before relationship between setting up the signal handler
  // and raising the signal.
  EventLoop::Waiter waiter(&EventLoop::Default(), "raise(SIGQUIT)");

  EventLoop::Default().Submit(
      []() {
        EXPECT_EQ(raise(SIGQUIT), 0);
      },
      &waiter);

  EventLoop::Default().Run();

  EXPECT_EQ(future.get(), "quit");
}

#endif // !defined(_WIN32)


TEST_F(SignalTest, SignalInterrupt) {
  auto [future, k] = Terminate(Signal(SIGINT));

  Interrupt interrupt;

  k.Register(interrupt);

  k.Start();

  interrupt.Trigger();

  EventLoop::Default().Run();

  EXPECT_THROW(future.get(), stout::eventuals::StoppedException);
}