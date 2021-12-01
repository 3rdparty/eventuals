#include "tcp.h"
// tcp.h must be included before anything else on Windows
// due to SSL redefinitions done by wincrypt.h.
#include "eventuals/terminal.h"
#include "eventuals/then.h"

namespace eventuals::test {
namespace {

using eventuals::ip::tcp::Acceptor;
using eventuals::ip::tcp::Protocol;

TEST_F(TCPTest, AcceptorOpenCloseSuccess) {
  Acceptor acceptor;

  Interrupt interrupt;

  EXPECT_FALSE(acceptor.IsOpen());

  auto e = [&]() {
    return acceptor.Open(Protocol::IPV4)
        >> Then([&acceptor]() {
             EXPECT_TRUE(acceptor.IsOpen());
           })
        >> acceptor.Close()
        >> Then([&acceptor]() {
             EXPECT_FALSE(acceptor.IsOpen());
           });
  };

  auto [future, k] = PromisifyForTest(e());

  k.Register(interrupt);

  k.Start();

  EventLoop::Default().RunUntil(future);

  EXPECT_NO_THROW(future.get());
}


TEST_F(TCPTest, AcceptorOpenInterrupt) {
  Acceptor acceptor;

  eventuals::Interrupt interrupt;

  EXPECT_FALSE(acceptor.IsOpen());

  auto e = [&]() {
    return acceptor.Open(Protocol::IPV4);
  };

  auto [future, k] = PromisifyForTest(e());

  k.Register(interrupt);

  interrupt.Trigger();

  k.Start();

  EventLoop::Default().RunUntil(future);

  EXPECT_THROW(future.get(), StoppedException);

  EXPECT_FALSE(acceptor.IsOpen());
}


TEST_F(TCPTest, AcceptorCloseInterrupt) {
  // ---------------------------------------------------------------------
  // Main test section.
  // ---------------------------------------------------------------------
  Acceptor acceptor;

  eventuals::Interrupt interrupt;

  EXPECT_FALSE(acceptor.IsOpen());

  auto e = [&]() {
    return acceptor.Open(Protocol::IPV4)
        >> Then([&]() {
             EXPECT_TRUE(acceptor.IsOpen());
             interrupt.Trigger();
           })
        >> acceptor.Close();
  };

  auto [future, k] = PromisifyForTest(e());

  k.Register(interrupt);

  k.Start();

  EventLoop::Default().RunUntil(future);

  EXPECT_THROW(future.get(), StoppedException);

  EXPECT_TRUE(acceptor.IsOpen());

  // ---------------------------------------------------------------------
  // Cleanup section.
  // ---------------------------------------------------------------------
  eventuals::Interrupt interrupt_close;

  auto e_close = [&]() {
    return acceptor.Close()
        >> Then([&]() {
             EXPECT_FALSE(acceptor.IsOpen());
           });
  };

  auto [future_close, k_close] = PromisifyForTest(e_close());

  k_close.Register(interrupt_close);

  k_close.Start();

  EventLoop::Default().RunUntil(future_close);

  EXPECT_NO_THROW(future_close.get());
}

} // namespace
} // namespace eventuals::test
