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

TEST_F(TCPIPV6Test, SocketConnectToAcceptorSuccess) {
  // ---------------------------------------------------------------------
  // Setup section.
  // ---------------------------------------------------------------------
  Acceptor acceptor;
  Socket socket;
  Socket accepted;

  eventuals::Interrupt interrupt_setup;

  auto e_setup = [&]() {
    return acceptor.Open(Protocol::IPV6)
        >> socket.Open(Protocol::IPV6)
        >> acceptor.Bind(TCPIPV6Test::kLocalHostIPV6, TCPIPV6Test::kAnyPort)
        >> acceptor.Listen();
  };

  auto [future_setup, k_setup] = PromisifyForTest(e_setup());

  k_setup.Register(interrupt_setup);

  k_setup.Start();

  EventLoop::Default().RunUntil(future_setup);

  EXPECT_NO_THROW(future_setup.get());

  // ---------------------------------------------------------------------
  // Connect to acceptor.
  // ---------------------------------------------------------------------
  eventuals::Interrupt interrupt_connect;
  eventuals::Interrupt interrupt_accept;

  auto e_connect = [&]() {
    return socket.Connect(TCPIPV6Test::kLocalHostIPV6, acceptor.BoundPort());
  };

  auto e_accept = [&]() {
    return acceptor.Accept(&accepted);
  };

  auto [future_connect, k_connect] = PromisifyForTest(e_connect());
  auto [future_accept, k_accept] = PromisifyForTest(e_accept());

  k_connect.Register(interrupt_connect);
  k_accept.Register(interrupt_accept);

  k_connect.Start();
  k_accept.Start();

  EventLoop::Default().RunUntil(future_connect);
  EventLoop::Default().RunUntil(future_accept);

  EXPECT_NO_THROW(future_connect.get());
  EXPECT_NO_THROW(future_accept.get());

  // ---------------------------------------------------------------------
  // Cleanup section.
  // ---------------------------------------------------------------------
  eventuals::Interrupt interrupt_close;

  auto e_close = [&]() {
    return accepted.Close()
        >> acceptor.Close()
        >> socket.Close();
  };

  auto [future_close, k_close] = PromisifyForTest(e_close());

  k_close.Register(interrupt_close);

  k_close.Start();

  EventLoop::Default().RunUntil(future_close);

  EXPECT_NO_THROW(future_close.get());
}


TEST_F(TCPIPV6Test, SocketConnectToBadIpAddressFail) {
  // ---------------------------------------------------------------------
  // Setup section.
  // ---------------------------------------------------------------------
  Socket socket;

  eventuals::Interrupt interrupt_setup;

  auto e_setup = [&]() {
    return socket.Open(Protocol::IPV6);
  };

  auto [future_setup, k_setup] = PromisifyForTest(e_setup());

  k_setup.Register(interrupt_setup);

  k_setup.Start();

  EventLoop::Default().RunUntil(future_setup);

  EXPECT_NO_THROW(future_setup.get());

  // ---------------------------------------------------------------------
  // Try to connect.
  // ---------------------------------------------------------------------
  eventuals::Interrupt interrupt_connect;

  auto e_connect = [&]() {
    return socket.Connect("::H", 8000);
  };

  auto [future_connect, k_connect] = PromisifyForTest(e_connect());

  k_connect.Register(interrupt_connect);

  k_connect.Start();

  EventLoop::Default().RunUntil(future_connect);

  // Not using EXPECT_THROW_WHAT since
  // the message depends on the language set in the OS.
  EXPECT_THROW(future_connect.get(), std::runtime_error);

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