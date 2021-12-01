#include "tcp.h"
// tcp.h must be included before anything else on Windows
// due to SSL redefinitions done by wincrypt.h.
#include "eventuals/terminal.h"
#include "eventuals/then.h"

namespace eventuals::test {
namespace {

using eventuals::ip::tcp::Acceptor;
using eventuals::ip::tcp::Protocol;

TEST_F(TCPTest, AcceptorBindSuccess) {
  Acceptor acceptor;

  eventuals::Interrupt interrupt;

  auto e = [&]() {
    return acceptor.Open(Protocol::IPV4)
        >> acceptor.Bind(TCPTest::kLocalHostIPV4, TCPTest::kAnyPort)
        >> Then([&acceptor]() {
             EXPECT_EQ(acceptor.BoundIp(), TCPTest::kLocalHostIPV4);
             EXPECT_GT(acceptor.BoundPort(), 0);
           })
        >> acceptor.Close();
  };

  auto [future, k] = PromisifyForTest(e());

  k.Register(interrupt);

  k.Start();

  EventLoop::Default().RunUntil(future);

  EXPECT_NO_THROW(future.get());
}


TEST_F(TCPTest, AcceptorBindAnyIPSuccess) {
  Acceptor acceptor;

  eventuals::Interrupt interrupt;

  auto e = [&]() {
    return acceptor.Open(Protocol::IPV4)
        >> acceptor.Bind(TCPTest::kAnyIPV4, TCPTest::kAnyPort)
        >> Then([&acceptor]() {
             EXPECT_EQ(acceptor.BoundIp(), TCPTest::kAnyIPV4);
             EXPECT_GT(acceptor.BoundPort(), 0);
           })
        >> acceptor.Close();
  };

  auto [future, k] = PromisifyForTest(e());

  k.Register(interrupt);

  k.Start();

  EventLoop::Default().RunUntil(future);

  EXPECT_NO_THROW(future.get());
}


TEST_F(TCPTest, AcceptorBindBadIPFail) {
  // ---------------------------------------------------------------------
  // Main test section.
  // ---------------------------------------------------------------------
  Acceptor acceptor;

  eventuals::Interrupt interrupt;

  auto e = [&]() {
    return acceptor.Open(Protocol::IPV4)
        >> acceptor.Bind("0.0.0.256", TCPTest::kAnyPort);
  };

  auto [future, k] = PromisifyForTest(e());

  k.Register(interrupt);

  k.Start();

  EventLoop::Default().RunUntil(future);

  // Not using EXPECT_THROW_WHAT since
  // the message depends on the language set in the OS.
  EXPECT_THROW(future.get(), std::runtime_error);

  // ---------------------------------------------------------------------
  // Cleanup section.
  // ---------------------------------------------------------------------
  eventuals::Interrupt interrupt_close;

  auto e_close = [&]() {
    return acceptor.Close();
  };

  auto [future_close, k_close] = PromisifyForTest(e_close());

  k_close.Register(interrupt_close);

  k_close.Start();

  EventLoop::Default().RunUntil(future_close);

  EXPECT_NO_THROW(future_close.get());
}


TEST_F(TCPTest, AcceptorBindClosedFail) {
  Acceptor acceptor;

  eventuals::Interrupt interrupt;

  auto e = [&]() {
    return acceptor.Bind(TCPTest::kAnyIPV4, TCPTest::kAnyPort);
  };

  auto [future, k] = PromisifyForTest(e());

  k.Register(interrupt);

  k.Start();

  EventLoop::Default().RunUntil(future);

  EXPECT_THROW_WHAT(future.get(), "Acceptor is closed");
}


TEST_F(TCPTest, AcceptorBindInterrupt) {
  // ---------------------------------------------------------------------
  // Main test section.
  // ---------------------------------------------------------------------
  Acceptor acceptor;

  eventuals::Interrupt interrupt;

  auto e = [&]() {
    return acceptor.Open(Protocol::IPV4)
        >> Then([&]() {
             interrupt.Trigger();
           })
        >> acceptor.Bind(TCPTest::kLocalHostIPV4, TCPTest::kAnyPort);
  };

  auto [future, k] = PromisifyForTest(e());

  k.Register(interrupt);

  k.Start();

  EventLoop::Default().RunUntil(future);

  EXPECT_THROW(future.get(), StoppedException);

  // ---------------------------------------------------------------------
  // Cleanup section.
  // ---------------------------------------------------------------------
  eventuals::Interrupt interrupt_close;

  auto e_close = [&]() {
    return acceptor.Close();
  };

  auto [future_close, k_close] = PromisifyForTest(e_close());

  k_close.Register(interrupt_close);

  k_close.Start();

  EventLoop::Default().RunUntil(future_close);

  EXPECT_NO_THROW(future_close.get());
}

} // namespace
} // namespace eventuals::test
