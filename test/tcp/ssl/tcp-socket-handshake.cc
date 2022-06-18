#include "tcp.h"

namespace eventuals::test {
namespace {

using testing::StrEq;
using testing::ThrowsMessage;

using eventuals::ip::tcp::Acceptor;
using eventuals::ip::tcp::Protocol;
using eventuals::ip::tcp::ssl::HandshakeType;
using eventuals::ip::tcp::ssl::Socket;
using eventuals::ip::tcp::ssl::SSLContext;

TEST_F(TCPSSLTest, HandshakeClosedFail) {
  SSLContext socket_context(SetupSSLContextClient());

  Socket socket(socket_context, Protocol::IPV4);

  eventuals::Interrupt interrupt;

  auto e = [&]() {
    return socket.Handshake(HandshakeType::CLIENT);
  };

  auto [future, k] = PromisifyForTest(e());

  k.Register(interrupt);

  k.Start();

  EventLoop::Default().RunUntil(future);

  EXPECT_THAT(
      // NOTE: capturing 'future' as a pointer because until C++20 we
      // can't capture a "local binding" by reference and there is a
      // bug with 'EXPECT_THAT' that forces our lambda to be const so
      // if we capture it by copy we can't call 'get()' because that
      // is a non-const function.
      [future = &future]() { future->get(); },
      ThrowsMessage<std::runtime_error>(StrEq("Socket is closed")));
}


TEST_F(TCPSSLTest, HandshakeNotConnectedFail) {
  // ---------------------------------------------------------------------
  // Setup section.
  // ---------------------------------------------------------------------
  SSLContext socket_context(SetupSSLContextClient());

  Socket socket(socket_context, Protocol::IPV4);

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
  // Main section.
  // ---------------------------------------------------------------------
  eventuals::Interrupt interrupt;

  auto e = [&]() {
    return socket.Handshake(HandshakeType::CLIENT);
  };

  auto [future, k] = PromisifyForTest(e());

  k.Register(interrupt);

  k.Start();

  EventLoop::Default().RunUntil(future);

  EXPECT_THAT(
      // NOTE: capturing 'future' as a pointer because until C++20 we
      // can't capture a "local binding" by reference and there is a
      // bug with 'EXPECT_THAT' that forces our lambda to be const so
      // if we capture it by copy we can't call 'get()' because that
      // is a non-const function.
      [future = &future]() { future->get(); },
      ThrowsMessage<std::runtime_error>(StrEq("Socket is not connected")));

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


TEST_F(TCPSSLTest, HandshakeTwiceFail) {
  // ---------------------------------------------------------------------
  // Setup section.
  // ---------------------------------------------------------------------
  SSLContext socket_context(SetupSSLContextClient());
  SSLContext accepted_context(SetupSSLContextServer());

  Acceptor acceptor(Protocol::IPV4);
  Socket socket(socket_context, Protocol::IPV4);
  Socket accepted(accepted_context, Protocol::IPV4);

  eventuals::Interrupt interrupt_setup;

  auto e_setup = [&]() {
    return acceptor.Open()
        >> socket.Open()
        >> acceptor.Bind(TCPSSLTest::kLocalHostIPV4, TCPSSLTest::kAnyPort)
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
        TCPSSLTest::kLocalHostIPV4,
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
  // Handshake.
  // ---------------------------------------------------------------------
  eventuals::Interrupt interrupt_socket_handshake;
  eventuals::Interrupt interrupt_accepted_handshake;

  auto e_socket_handshake = [&]() {
    return socket.Handshake(HandshakeType::CLIENT);
  };

  auto e_accepted_handshake = [&]() {
    return accepted.Handshake(HandshakeType::SERVER);
  };

  auto [future_socket_handshake, k_socket_handshake] =
      PromisifyForTest(e_socket_handshake());
  auto [future_accepted_handshake, k_accepted_handshake] =
      PromisifyForTest(e_accepted_handshake());

  k_socket_handshake.Register(interrupt_socket_handshake);
  k_accepted_handshake.Register(interrupt_accepted_handshake);

  k_socket_handshake.Start();
  k_accepted_handshake.Start();

  EventLoop::Default().RunUntil(future_socket_handshake);
  EventLoop::Default().RunUntil(future_accepted_handshake);

  EXPECT_NO_THROW(future_socket_handshake.get());
  EXPECT_NO_THROW(future_accepted_handshake.get());

  // ---------------------------------------------------------------------
  // Handshake the second time.
  // ---------------------------------------------------------------------
  eventuals::Interrupt interrupt_socket_handshake_second;
  eventuals::Interrupt interrupt_accepted_handshake_second;

  auto e_socket_handshake_second = [&]() {
    return socket.Handshake(HandshakeType::CLIENT);
  };

  auto e_accepted_handshake_second = [&]() {
    return accepted.Handshake(HandshakeType::SERVER);
  };

  auto [future_socket_handshake_second, k_socket_handshake_second] =
      PromisifyForTest(e_socket_handshake_second());
  auto [future_accepted_handshake_second, k_accepted_handshake_second] =
      PromisifyForTest(e_accepted_handshake_second());

  k_socket_handshake_second.Register(interrupt_socket_handshake_second);
  k_accepted_handshake_second.Register(interrupt_accepted_handshake_second);

  k_socket_handshake_second.Start();
  k_accepted_handshake_second.Start();

  EventLoop::Default().RunUntil(future_socket_handshake_second);
  EventLoop::Default().RunUntil(future_accepted_handshake_second);

  EXPECT_THAT(
      // NOTE: capturing 'future' as a pointer because until C++20 we
      // can't capture a "local binding" by reference and there is a
      // bug with 'EXPECT_THAT' that forces our lambda to be const so
      // if we capture it by copy we can't call 'get()' because that
      // is a non-const function.
      [future_socket_handshake_second = &future_socket_handshake_second]() {
        future_socket_handshake_second->get();
      },
      ThrowsMessage<std::runtime_error>(
          StrEq(
              "Handshake was already completed")));

  EXPECT_THAT(
      // NOTE: capturing 'future' as a pointer because until C++20 we
      // can't capture a "local binding" by reference and there is a
      // bug with 'EXPECT_THAT' that forces our lambda to be const so
      // if we capture it by copy we can't call 'get()' because that
      // is a non-const function.
      [future_accepted_handshake_second =
           &future_accepted_handshake_second]() {
        future_accepted_handshake_second->get();
      },
      ThrowsMessage<std::runtime_error>(
          StrEq(
              "Handshake was already completed")));

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


// NOTE: we need to do separate tests for
// calling interrupt.Trigger() before and after k.Start()
// since Handshake operation is asynchronous.
TEST_F(TCPSSLTest, HandshakeInterruptBeforeStart) {
  // ---------------------------------------------------------------------
  // Setup section.
  // ---------------------------------------------------------------------
  SSLContext socket_context(SetupSSLContextClient());
  SSLContext accepted_context(SetupSSLContextServer());

  Acceptor acceptor(Protocol::IPV4);
  Socket socket(socket_context, Protocol::IPV4);
  Socket accepted(accepted_context, Protocol::IPV4);

  eventuals::Interrupt interrupt_setup;

  auto e_setup = [&]() {
    return acceptor.Open()
        >> socket.Open()
        >> acceptor.Bind(TCPSSLTest::kLocalHostIPV4, TCPSSLTest::kAnyPort)
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
        TCPSSLTest::kLocalHostIPV4,
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
  // Interrupt Handshake operation.
  // ---------------------------------------------------------------------
  eventuals::Interrupt interrupt_socket_handshake;
  eventuals::Interrupt interrupt_accepted_handshake;

  auto e_socket_handshake = [&]() {
    return socket.Handshake(HandshakeType::CLIENT);
  };

  auto e_accepted_handshake = [&]() {
    return accepted.Handshake(HandshakeType::SERVER);
  };

  auto [future_socket_handshake, k_socket_handshake] =
      PromisifyForTest(e_socket_handshake());
  auto [future_accepted_handshake, k_accepted_handshake] =
      PromisifyForTest(e_accepted_handshake());

  k_socket_handshake.Register(interrupt_socket_handshake);
  k_accepted_handshake.Register(interrupt_accepted_handshake);

  interrupt_socket_handshake.Trigger();
  interrupt_accepted_handshake.Trigger();

  k_socket_handshake.Start();
  k_accepted_handshake.Start();

  EventLoop::Default().RunUntil(future_socket_handshake);
  EventLoop::Default().RunUntil(future_accepted_handshake);

  EXPECT_THROW(future_socket_handshake.get(), eventuals::StoppedException);
  EXPECT_THROW(future_accepted_handshake.get(), eventuals::StoppedException);

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


// NOTE: we need to do separate tests for
// calling interrupt.Trigger() before and after k.Start()
// since Handshake operation is asynchronous.
TEST_F(TCPSSLTest, HandshakeInterruptAfterStart) {
  // ---------------------------------------------------------------------
  // Setup section.
  // ---------------------------------------------------------------------
  SSLContext socket_context(SetupSSLContextClient());
  SSLContext accepted_context(SetupSSLContextServer());

  Acceptor acceptor(Protocol::IPV4);
  Socket socket(socket_context, Protocol::IPV4);
  Socket accepted(accepted_context, Protocol::IPV4);

  eventuals::Interrupt interrupt_setup;

  auto e_setup = [&]() {
    return acceptor.Open()
        >> socket.Open()
        >> acceptor.Bind(TCPSSLTest::kLocalHostIPV4, TCPSSLTest::kAnyPort)
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
        TCPSSLTest::kLocalHostIPV4,
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
  // Interrupt Handshake operation.
  // ---------------------------------------------------------------------
  eventuals::Interrupt interrupt_socket_handshake;
  eventuals::Interrupt interrupt_accepted_handshake;

  auto e_socket_handshake = [&]() {
    return socket.Handshake(HandshakeType::CLIENT);
  };

  auto e_accepted_handshake = [&]() {
    return accepted.Handshake(HandshakeType::SERVER);
  };

  auto [future_socket_handshake, k_socket_handshake] =
      PromisifyForTest(e_socket_handshake());
  auto [future_accepted_handshake, k_accepted_handshake] =
      PromisifyForTest(e_accepted_handshake());

  k_socket_handshake.Register(interrupt_socket_handshake);
  k_accepted_handshake.Register(interrupt_accepted_handshake);

  k_socket_handshake.Start();
  k_accepted_handshake.Start();

  interrupt_socket_handshake.Trigger();
  interrupt_accepted_handshake.Trigger();

  EventLoop::Default().RunUntil(future_socket_handshake);
  EventLoop::Default().RunUntil(future_accepted_handshake);

  EXPECT_THROW(future_socket_handshake.get(), eventuals::StoppedException);
  EXPECT_THROW(future_accepted_handshake.get(), eventuals::StoppedException);

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

} // namespace
} // namespace eventuals::test