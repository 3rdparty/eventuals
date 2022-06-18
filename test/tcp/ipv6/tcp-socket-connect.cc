#include "tcp.h"

namespace eventuals::test {
namespace {

using testing::StrEq;
using testing::ThrowsMessage;

using eventuals::ip::tcp::Acceptor;
using eventuals::ip::tcp::Protocol;
using eventuals::ip::tcp::Socket;

TEST_F(TCPIPV6Test, SocketConnectToAcceptorSuccess) {
  // ---------------------------------------------------------------------
  // Setup section.
  // ---------------------------------------------------------------------
  Acceptor acceptor(Protocol::IPV6);
  Socket socket(Protocol::IPV6);
  Socket accepted(Protocol::IPV6);

  eventuals::Interrupt interrupt_setup;

  auto e_setup = [&]() {
    return acceptor.Open()
        >> socket.Open()
        >> acceptor.Bind(TCPIPV6Test::kLocalHostIPV6, TCPIPV6Test::kAnyPort)
        >> acceptor.Listen(1);
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
    return socket.Connect(
        TCPIPV6Test::kLocalHostIPV6,
        acceptor.ListeningPort());
  };

  auto e_accept = [&]() {
    return acceptor.Accept(accepted);
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
  eventuals::Interrupt interrupt_cleanup;

  auto e_cleanup = [&]() {
    return accepted.Close()
        >> acceptor.Close()
        >> socket.Close();
  };

  auto [future_cleanup, k_cleanup] = PromisifyForTest(e_cleanup());

  k_cleanup.Register(interrupt_cleanup);

  k_cleanup.Start();

  EventLoop::Default().RunUntil(future_cleanup);

  EXPECT_NO_THROW(future_cleanup.get());
}


TEST_F(TCPIPV6Test, SocketConnectToAcceptorTwiceFail) {
  // ---------------------------------------------------------------------
  // Setup section.
  // ---------------------------------------------------------------------
  Acceptor acceptor(Protocol::IPV6);
  Socket socket(Protocol::IPV6);
  Socket accepted(Protocol::IPV6);

  eventuals::Interrupt interrupt_setup;

  auto e_setup = [&]() {
    return acceptor.Open()
        >> socket.Open()
        >> acceptor.Bind(TCPIPV6Test::kLocalHostIPV6, TCPIPV6Test::kAnyPort)
        >> acceptor.Listen(1);
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
    return socket.Connect(
        TCPIPV6Test::kLocalHostIPV6,
        acceptor.ListeningPort());
  };

  auto e_accept = [&]() {
    return acceptor.Accept(accepted);
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
  // Connect to acceptor the second time.
  // ---------------------------------------------------------------------
  eventuals::Interrupt interrupt_connect_second;

  auto e_connect_second = [&]() {
    return socket.Connect(
        TCPIPV6Test::kLocalHostIPV6,
        acceptor.ListeningPort());
  };

  auto [future_connect_second, k_connect_second] =
      PromisifyForTest(e_connect_second());

  k_connect_second.Register(interrupt_connect_second);

  k_connect_second.Start();

  EventLoop::Default().RunUntil(future_connect_second);

  EXPECT_THAT(
      // NOTE: capturing 'future' as a pointer because until C++20 we
      // can't capture a "local binding" by reference and there is a
      // bug with 'EXPECT_THAT' that forces our lambda to be const so
      // if we capture it by copy we can't call 'get()' because that
      // is a non-const function.
      [future_connect_second = &future_connect_second]() {
        future_connect_second->get();
      },
      ThrowsMessage<std::runtime_error>(StrEq("Socket is already connected")));

  // ---------------------------------------------------------------------
  // Cleanup section.
  // ---------------------------------------------------------------------
  eventuals::Interrupt interrupt_cleanup;

  auto e_cleanup = [&]() {
    return accepted.Close()
        >> acceptor.Close()
        >> socket.Close();
  };

  auto [future_cleanup, k_cleanup] = PromisifyForTest(e_cleanup());

  k_cleanup.Register(interrupt_cleanup);

  k_cleanup.Start();

  EventLoop::Default().RunUntil(future_cleanup);

  EXPECT_NO_THROW(future_cleanup.get());
}


TEST_F(TCPIPV6Test, SocketConnectToBadIpAddressFail) {
  // ---------------------------------------------------------------------
  // Setup section.
  // ---------------------------------------------------------------------
  Socket socket(Protocol::IPV6);

  eventuals::Interrupt interrupt_setup;

  auto e_setup = [&]() {
    return socket.Open();
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
    return socket.Connect("127.0.0.256", 8000);
  };

  auto [future_connect, k_connect] = PromisifyForTest(e_connect());

  k_connect.Register(interrupt_connect);

  k_connect.Start();

  EventLoop::Default().RunUntil(future_connect);

  // Not using EXPECT_THAT since
  // the message depends on the language set in the OS.
  EXPECT_THROW(future_connect.get(), std::runtime_error);

  // ---------------------------------------------------------------------
  // Cleanup section.
  // ---------------------------------------------------------------------
  eventuals::Interrupt interrupt_cleanup;

  auto e_cleanup = [&]() {
    return socket.Close();
  };

  auto [future_cleanup, k_cleanup] = PromisifyForTest(e_cleanup());

  k_cleanup.Register(interrupt_cleanup);

  k_cleanup.Start();

  EventLoop::Default().RunUntil(future_cleanup);

  EXPECT_NO_THROW(future_cleanup.get());
}


// NOTE: we need to do separate tests for
// calling interrupt.Trigger() before and after k.Start()
// since Connect operation is asynchronous.
TEST_F(TCPIPV6Test, SocketConnectToAcceptorInterruptBeforeStart) {
  // ---------------------------------------------------------------------
  // Setup section.
  // ---------------------------------------------------------------------
  Acceptor acceptor(Protocol::IPV6);
  Socket socket(Protocol::IPV6);

  eventuals::Interrupt interrupt_setup;

  auto e_setup = [&]() {
    return acceptor.Open()
        >> socket.Open()
        >> acceptor.Bind(TCPIPV6Test::kLocalHostIPV6, TCPIPV6Test::kAnyPort)
        >> acceptor.Listen(1);
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

  auto e_connect = [&]() {
    return socket.Connect(
        TCPIPV6Test::kLocalHostIPV6,
        acceptor.ListeningPort());
  };

  auto [future_connect, k_connect] = PromisifyForTest(e_connect());

  k_connect.Register(interrupt_connect);

  interrupt_connect.Trigger();

  k_connect.Start();

  EventLoop::Default().RunUntil(future_connect);

  EXPECT_THROW(future_connect.get(), eventuals::StoppedException);

  // ---------------------------------------------------------------------
  // Cleanup section.
  // ---------------------------------------------------------------------
  eventuals::Interrupt interrupt_cleanup;

  auto e_cleanup = [&]() {
    return acceptor.Close()
        >> socket.Close();
  };

  auto [future_cleanup, k_cleanup] = PromisifyForTest(e_cleanup());

  k_cleanup.Register(interrupt_cleanup);

  k_cleanup.Start();

  EventLoop::Default().RunUntil(future_cleanup);

  EXPECT_NO_THROW(future_cleanup.get());
}


// NOTE: we need to do separate tests for
// calling interrupt.Trigger() before and after k.Start()
// since Connect operation is asynchronous.
TEST_F(TCPIPV6Test, SocketConnectToAcceptorInterruptAfterStart) {
  // ---------------------------------------------------------------------
  // Setup section.
  // ---------------------------------------------------------------------
  Acceptor acceptor(Protocol::IPV6);
  Socket socket(Protocol::IPV6);

  eventuals::Interrupt interrupt_setup;

  auto e_setup = [&]() {
    return acceptor.Open()
        >> socket.Open()
        >> acceptor.Bind(TCPIPV6Test::kLocalHostIPV6, TCPIPV6Test::kAnyPort)
        >> acceptor.Listen(1);
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

  auto e_connect = [&]() {
    return socket.Connect(
        TCPIPV6Test::kLocalHostIPV6,
        acceptor.ListeningPort());
  };

  auto [future_connect, k_connect] = PromisifyForTest(e_connect());

  k_connect.Register(interrupt_connect);

  k_connect.Start();

  interrupt_connect.Trigger();

  EventLoop::Default().RunUntil(future_connect);

  EXPECT_THROW(future_connect.get(), eventuals::StoppedException);

  // ---------------------------------------------------------------------
  // Cleanup section.
  // ---------------------------------------------------------------------
  eventuals::Interrupt interrupt_cleanup;

  auto e_cleanup = [&]() {
    return acceptor.Close()
        >> socket.Close();
  };

  auto [future_cleanup, k_cleanup] = PromisifyForTest(e_cleanup());

  k_cleanup.Register(interrupt_cleanup);

  k_cleanup.Start();

  EventLoop::Default().RunUntil(future_cleanup);

  EXPECT_NO_THROW(future_cleanup.get());
}

} // namespace
} // namespace eventuals::test
