#include "tcp.h"
// tcp.h must be included before anything else on Windows
// due to SSL redefinitions done by wincrypt.h.
#include "eventuals/terminal.h"
#include "eventuals/then.h"

namespace eventuals::test {
namespace {

using eventuals::ip::tcp::Acceptor;
using eventuals::ip::tcp::Protocol;
using eventuals::ip::tcp::Socket;

TEST_F(TCPTest, SocketOpenCloseSuccess) {
  Socket socket;

  eventuals::Interrupt interrupt;

  EXPECT_FALSE(socket.IsOpen());

  auto e = [&]() {
    return socket.Open(Protocol::IPV4)
        >> Then([&socket]() {
             EXPECT_TRUE(socket.IsOpen());
           })
        >> socket.Close()
        >> Then([&socket]() {
             EXPECT_FALSE(socket.IsOpen());
           });
  };

  auto [future, k] = PromisifyForTest(e());

  k.Register(interrupt);

  k.Start();

  EventLoop::Default().RunUntil(future);

  EXPECT_NO_THROW(future.get());
}


TEST_F(TCPTest, SocketOpenInterrupt) {
  Socket socket;

  eventuals::Interrupt interrupt;

  EXPECT_FALSE(socket.IsOpen());

  auto e = [&]() {
    return socket.Open(Protocol::IPV4);
  };

  auto [future, k] = PromisifyForTest(e());

  k.Register(interrupt);

  interrupt.Trigger();

  k.Start();

  EventLoop::Default().RunUntil(future);

  EXPECT_THROW(future.get(), eventuals::StoppedException);

  EXPECT_FALSE(socket.IsOpen());
}


TEST_F(TCPTest, SocketCloseInterrupt) {
  // ---------------------------------------------------------------------
  // Main test section.
  // ---------------------------------------------------------------------
  Socket socket;

  eventuals::Interrupt interrupt;

  EXPECT_FALSE(socket.IsOpen());

  auto e = [&]() {
    return socket.Open(Protocol::IPV4)
        >> Then([&]() {
             EXPECT_TRUE(socket.IsOpen());
             interrupt.Trigger();
           })
        >> socket.Close();
  };

  auto [future, k] = PromisifyForTest(e());

  k.Register(interrupt);

  k.Start();

  EventLoop::Default().RunUntil(future);

  EXPECT_THROW(future.get(), eventuals::StoppedException);

  EXPECT_TRUE(socket.IsOpen());

  // ---------------------------------------------------------------------
  // Cleanup section.
  // ---------------------------------------------------------------------
  eventuals::Interrupt interrupt_close;

  auto e_close = [&]() {
    return socket.Close()
        >> Then([&]() {
             EXPECT_FALSE(socket.IsOpen());
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