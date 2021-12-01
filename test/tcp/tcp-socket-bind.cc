#include "tcp.h"
// tcp.h must be included before anything else on Windows
// due to SSL redefinitions done by wincrypt.h.
#include "eventuals/terminal.h"
#include "eventuals/then.h"

namespace eventuals::test {
namespace {

using eventuals::ip::tcp::Protocol;
using eventuals::ip::tcp::Socket;

TEST_F(TCPTest, SocketBindSuccess) {
  Socket socket;

  eventuals::Interrupt interrupt;

  auto e = [&]() {
    return socket.Open(Protocol::IPV4)
        >> socket.Bind(TCPTest::kLocalHostIPV4, TCPTest::kAnyPort)
        >> Then([&socket]() {
             EXPECT_EQ(socket.BoundIp(), TCPTest::kLocalHostIPV4);
             EXPECT_GT(socket.BoundPort(), 0);
           })
        >> socket.Close();
  };

  auto [future, k] = PromisifyForTest(e());

  k.Register(interrupt);

  k.Start();

  EventLoop::Default().RunUntil(future);

  EXPECT_NO_THROW(future.get());
}


TEST_F(TCPTest, SocketBindAnyIPSuccess) {
  Socket socket;

  eventuals::Interrupt interrupt;

  auto e = [&]() {
    return socket.Open(Protocol::IPV4)
        >> socket.Bind(TCPTest::kAnyIPV4, TCPTest::kAnyPort)
        >> Then([&socket]() {
             EXPECT_EQ(socket.BoundIp(), TCPTest::kAnyIPV4);
             EXPECT_GT(socket.BoundPort(), 0);
           })
        >> socket.Close();
  };

  auto [future, k] = PromisifyForTest(e());

  k.Register(interrupt);

  k.Start();

  EventLoop::Default().RunUntil(future);

  EXPECT_NO_THROW(future.get());
}


TEST_F(TCPTest, SocketBindBadIPFail) {
  // ---------------------------------------------------------------------
  // Main section.
  // ---------------------------------------------------------------------
  Socket socket;

  eventuals::Interrupt interrupt;

  auto e = socket.Open(Protocol::IPV4)
      >> socket.Bind("0.0.0.256", TCPTest::kAnyPort);

  auto [future, k] = PromisifyForTest(std::move(e));

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
    return socket.Close();
  };

  auto [future_close, k_close] = PromisifyForTest(e_close());

  k_close.Register(interrupt_close);

  k_close.Start();

  EventLoop::Default().RunUntil(future_close);

  EXPECT_NO_THROW(future_close.get());
}


TEST_F(TCPTest, SocketBindClosedFail) {
  Socket socket;

  eventuals::Interrupt interrupt;

  auto e = [&]() {
    return socket.Bind(TCPTest::kAnyIPV4, TCPTest::kAnyPort);
  };

  auto [future, k] = PromisifyForTest(e());

  k.Register(interrupt);

  k.Start();

  EventLoop::Default().RunUntil(future);

  EXPECT_THROW_WHAT(future.get(), "Socket is closed");
}


TEST_F(TCPTest, SocketBindInterrupt) {
  // ---------------------------------------------------------------------
  // Main section.
  // ---------------------------------------------------------------------
  Socket socket;

  eventuals::Interrupt interrupt;

  auto e = [&]() {
    return socket.Open(Protocol::IPV4)
        >> Then([&]() {
             interrupt.Trigger();
           })
        >> socket.Bind(TCPTest::kAnyIPV4, TCPTest::kAnyPort);
  };

  auto [future, k] = PromisifyForTest(e());

  k.Register(interrupt);

  k.Start();

  EventLoop::Default().RunUntil(future);

  EXPECT_THROW(future.get(), eventuals::StoppedException);

  // ---------------------------------------------------------------------
  // Cleanup section.
  // ---------------------------------------------------------------------
  eventuals::Interrupt interrupt_close;

  auto e_close = [&]() {
    return socket.Close();
  };

  auto [future_close, k_close] = PromisifyForTest(e_close());

  k_close.Register(interrupt_close);

  k_close.Start();

  EventLoop::Default().RunUntil(future_close);

  EXPECT_NO_THROW(future_close.get());
}

} // namespace
} // namespace eventuals::test
