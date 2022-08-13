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

TEST_F(TCPSSLTest, SocketSendReceiveSuccess) {
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

  EventLoop::Default().RunUntil(future_connect, future_accept);

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

  EventLoop::Default().RunUntil(
      future_socket_handshake,
      future_accepted_handshake);

  EXPECT_NO_THROW(future_socket_handshake.get());
  EXPECT_NO_THROW(future_accepted_handshake.get());

  // ---------------------------------------------------------------------
  // Send and receive data (socket -> accepted).
  // ---------------------------------------------------------------------
  eventuals::Interrupt interrupt_send_to_accepted;
  eventuals::Interrupt interrupt_receive_from_socket;

  char buffer[TCPSSLTest::kTestDataSize];
  memset(buffer, 0, sizeof(buffer));

  auto e_send_to_accepted = [&]() {
    return socket.Send(TCPSSLTest::kTestData, TCPSSLTest::kTestDataSize);
  };

  auto e_receive_from_socket = [&]() {
    return accepted.Receive(buffer, sizeof(buffer), TCPSSLTest::kTestDataSize);
  };

  auto [future_send_to_accepted, k_send_to_accepted] =
      PromisifyForTest(e_send_to_accepted());
  auto [future_receive_from_socket, k_receive_from_socket] =
      PromisifyForTest(e_receive_from_socket());

  k_send_to_accepted.Register(interrupt_send_to_accepted);
  k_receive_from_socket.Register(interrupt_receive_from_socket);

  k_send_to_accepted.Start();
  k_receive_from_socket.Start();

  EventLoop::Default().RunUntil(
      future_send_to_accepted,
      future_receive_from_socket);

  EXPECT_NO_THROW(future_send_to_accepted.get());
  EXPECT_NO_THROW(future_receive_from_socket.get());

  EXPECT_STREQ(buffer, TCPSSLTest::kTestData);

  // ---------------------------------------------------------------------
  // Send and receive data (accepted -> socket).
  // ---------------------------------------------------------------------
  eventuals::Interrupt interrupt_send_to_socket;
  eventuals::Interrupt interrupt_receive_from_accepted;

  memset(buffer, 0, sizeof(buffer));

  auto e_send_to_socket = [&]() {
    return accepted.Send(TCPSSLTest::kTestData, TCPSSLTest::kTestDataSize);
  };

  auto e_receive_from_accepted = [&]() {
    return socket.Receive(buffer, sizeof(buffer), TCPSSLTest::kTestDataSize);
  };

  auto [future_send_to_socket, k_send_to_socket] =
      PromisifyForTest(e_send_to_socket());
  auto [future_receive_from_accepted, k_receive_from_accepted] =
      PromisifyForTest(e_receive_from_accepted());

  k_send_to_socket.Register(interrupt_send_to_socket);
  k_receive_from_accepted.Register(interrupt_receive_from_accepted);

  k_send_to_socket.Start();
  k_receive_from_accepted.Start();

  EventLoop::Default().RunUntil(
      future_send_to_socket,
      future_receive_from_accepted);

  EXPECT_NO_THROW(future_send_to_socket.get());
  EXPECT_NO_THROW(future_receive_from_accepted.get());

  EXPECT_STREQ(buffer, TCPSSLTest::kTestData);

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


TEST_F(TCPSSLTest, SocketSendReceiveClosedFail) {
  SSLContext socket_context(SetupSSLContextClient());

  Socket socket(socket_context, Protocol::IPV4);

  char buffer[TCPTest::kTestDataSize];
  memset(buffer, 0, sizeof(buffer));

  // ---------------------------------------------------------------------
  // Test Send operation.
  // ---------------------------------------------------------------------
  eventuals::Interrupt interrupt_send;

  auto e_send = [&]() {
    return socket.Send(TCPTest::kTestData, TCPTest::kTestDataSize);
  };

  auto [future_send, k_send] = PromisifyForTest(e_send());

  k_send.Register(interrupt_send);

  k_send.Start();

  EventLoop::Default().RunUntil(future_send);

  EXPECT_THAT(
      // NOTE: capturing 'future' as a pointer because until C++20 we
      // can't capture a "local binding" by reference and there is a
      // bug with 'EXPECT_THAT' that forces our lambda to be const so
      // if we capture it by copy we can't call 'get()' because that
      // is a non-const function.
      [future_send = &future_send]() { future_send->get(); },
      ThrowsMessage<std::runtime_error>(StrEq("Socket is closed")));

  // ---------------------------------------------------------------------
  // Test Receive operation.
  // ---------------------------------------------------------------------
  eventuals::Interrupt interrupt_receive;

  auto e_receive = [&]() {
    return socket.Receive(buffer, sizeof(buffer), TCPTest::kTestDataSize);
  };

  auto [future_receive, k_receive] = PromisifyForTest(e_receive());

  k_receive.Register(interrupt_receive);

  k_receive.Start();

  EventLoop::Default().RunUntil(future_receive);

  EXPECT_THAT(
      // NOTE: capturing 'future' as a pointer because until C++20 we
      // can't capture a "local binding" by reference and there is a
      // bug with 'EXPECT_THAT' that forces our lambda to be const so
      // if we capture it by copy we can't call 'get()' because that
      // is a non-const function.
      [future_receive = &future_receive]() { future_receive->get(); },
      ThrowsMessage<std::runtime_error>(StrEq("Socket is closed")));
}


TEST_F(TCPSSLTest, SocketSendReceiveNotConnectedFail) {
  // ---------------------------------------------------------------------
  // Setup section.
  // ---------------------------------------------------------------------
  SSLContext socket_context(SetupSSLContextClient());

  Socket socket(socket_context, Protocol::IPV4);

  char buffer[TCPTest::kTestDataSize];
  memset(buffer, 0, sizeof(buffer));

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
  // Test Send operation.
  // ---------------------------------------------------------------------
  eventuals::Interrupt interrupt_send;

  auto e_send = [&]() {
    return socket.Send(TCPTest::kTestData, TCPTest::kTestDataSize);
  };

  auto [future_send, k_send] = PromisifyForTest(e_send());

  k_send.Register(interrupt_send);

  k_send.Start();

  EventLoop::Default().RunUntil(future_send);

  EXPECT_THAT(
      // NOTE: capturing 'future' as a pointer because until C++20 we
      // can't capture a "local binding" by reference and there is a
      // bug with 'EXPECT_THAT' that forces our lambda to be const so
      // if we capture it by copy we can't call 'get()' because that
      // is a non-const function.
      [future_send = &future_send]() { future_send->get(); },
      ThrowsMessage<std::runtime_error>(StrEq("Socket is not connected")));

  // ---------------------------------------------------------------------
  // Test Receive operation.
  // ---------------------------------------------------------------------
  eventuals::Interrupt interrupt_receive;

  auto e_receive = [&]() {
    return socket.Receive(buffer, sizeof(buffer), TCPTest::kTestDataSize);
  };

  auto [future_receive, k_receive] = PromisifyForTest(e_receive());

  k_receive.Register(interrupt_receive);

  k_receive.Start();

  EventLoop::Default().RunUntil(future_receive);

  EXPECT_THAT(
      // NOTE: capturing 'future' as a pointer because until C++20 we
      // can't capture a "local binding" by reference and there is a
      // bug with 'EXPECT_THAT' that forces our lambda to be const so
      // if we capture it by copy we can't call 'get()' because that
      // is a non-const function.
      [future_receive = &future_receive]() { future_receive->get(); },
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


TEST_F(TCPSSLTest, SocketSendReceiveBeforeHandshakeFail) {
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

  EventLoop::Default().RunUntil(future_connect, future_accept);

  EXPECT_NO_THROW(future_connect.get());
  EXPECT_NO_THROW(future_accept.get());

  // ---------------------------------------------------------------------
  // Send and receive data (socket -> accepted).
  // ---------------------------------------------------------------------
  eventuals::Interrupt interrupt_send_to_accepted;
  eventuals::Interrupt interrupt_receive_from_socket;

  char buffer[TCPSSLTest::kTestDataSize];
  memset(buffer, 0, sizeof(buffer));

  auto e_send_to_accepted = [&]() {
    return socket.Send(TCPSSLTest::kTestData, TCPSSLTest::kTestDataSize);
  };

  auto e_receive_from_socket = [&]() {
    return accepted.Receive(buffer, sizeof(buffer), TCPSSLTest::kTestDataSize);
  };

  auto [future_send_to_accepted, k_send_to_accepted] =
      PromisifyForTest(e_send_to_accepted());
  auto [future_receive_from_socket, k_receive_from_socket] =
      PromisifyForTest(e_receive_from_socket());

  k_send_to_accepted.Register(interrupt_send_to_accepted);
  k_receive_from_socket.Register(interrupt_receive_from_socket);

  k_send_to_accepted.Start();
  k_receive_from_socket.Start();

  EventLoop::Default().RunUntil(
      future_send_to_accepted,
      future_receive_from_socket);

  EXPECT_THAT(
      // NOTE: capturing 'future' as a pointer because until C++20 we
      // can't capture a "local binding" by reference and there is a
      // bug with 'EXPECT_THAT' that forces our lambda to be const so
      // if we capture it by copy we can't call 'get()' because that
      // is a non-const function.
      [future_send_to_accepted = &future_send_to_accepted]() {
        future_send_to_accepted->get();
      },
      ThrowsMessage<std::runtime_error>(
          StrEq(
              "Must Handshake before trying to Send")));

  EXPECT_THAT(
      // NOTE: capturing 'future' as a pointer because until C++20 we
      // can't capture a "local binding" by reference and there is a
      // bug with 'EXPECT_THAT' that forces our lambda to be const so
      // if we capture it by copy we can't call 'get()' because that
      // is a non-const function.
      [future_receive_from_socket = &future_receive_from_socket]() {
        future_receive_from_socket->get();
      },
      ThrowsMessage<std::runtime_error>(
          StrEq(
              "Must Handshake before trying to Receive")));

  // ---------------------------------------------------------------------
  // Send and receive data (accepted -> socket).
  // ---------------------------------------------------------------------
  eventuals::Interrupt interrupt_send_to_socket;
  eventuals::Interrupt interrupt_receive_from_accepted;

  memset(buffer, 0, sizeof(buffer));

  auto e_send_to_socket = [&]() {
    return accepted.Send(TCPSSLTest::kTestData, TCPSSLTest::kTestDataSize);
  };

  auto e_receive_from_accepted = [&]() {
    return socket.Receive(buffer, sizeof(buffer), TCPSSLTest::kTestDataSize);
  };

  auto [future_send_to_socket, k_send_to_socket] =
      PromisifyForTest(e_send_to_socket());
  auto [future_receive_from_accepted, k_receive_from_accepted] =
      PromisifyForTest(e_receive_from_accepted());

  k_send_to_socket.Register(interrupt_send_to_socket);
  k_receive_from_accepted.Register(interrupt_receive_from_accepted);

  k_send_to_socket.Start();
  k_receive_from_accepted.Start();

  EventLoop::Default().RunUntil(
      future_send_to_socket,
      future_receive_from_accepted);

  EXPECT_THAT(
      // NOTE: capturing 'future' as a pointer because until C++20 we
      // can't capture a "local binding" by reference and there is a
      // bug with 'EXPECT_THAT' that forces our lambda to be const so
      // if we capture it by copy we can't call 'get()' because that
      // is a non-const function.
      [future_send_to_socket = &future_send_to_socket]() {
        future_send_to_socket->get();
      },
      ThrowsMessage<std::runtime_error>(
          StrEq(
              "Must Handshake before trying to Send")));

  EXPECT_THAT(
      // NOTE: capturing 'future' as a pointer because until C++20 we
      // can't capture a "local binding" by reference and there is a
      // bug with 'EXPECT_THAT' that forces our lambda to be const so
      // if we capture it by copy we can't call 'get()' because that
      // is a non-const function.
      [future_receive_from_accepted = &future_receive_from_accepted]() {
        future_receive_from_accepted->get();
      },
      ThrowsMessage<std::runtime_error>(
          StrEq(
              "Must Handshake before trying to Receive")));

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
// since Send and Receive operations are asynchronous.
TEST_F(TCPSSLTest, SocketSendReceiveInterruptBeforeStart) {
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

  EventLoop::Default().RunUntil(future_connect, future_accept);

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

  EventLoop::Default().RunUntil(
      future_socket_handshake,
      future_accepted_handshake);

  EXPECT_NO_THROW(future_socket_handshake.get());
  EXPECT_NO_THROW(future_accepted_handshake.get());

  // ---------------------------------------------------------------------
  // Interrupt: Send data from socket.
  // ---------------------------------------------------------------------
  eventuals::Interrupt interrupt_send;

  auto e_send = [&]() {
    return socket.Send(TCPTest::kTestData, TCPTest::kTestDataSize);
  };

  auto [future_send, k_send] = PromisifyForTest(e_send());

  k_send.Register(interrupt_send);

  interrupt_send.Trigger();

  k_send.Start();

  EventLoop::Default().RunUntil(future_send);

  EXPECT_THROW(future_send.get(), eventuals::StoppedException);

  // ---------------------------------------------------------------------
  // Interrupt: Receive data.
  // ---------------------------------------------------------------------
  eventuals::Interrupt interrupt_receive;

  char buffer[TCPTest::kTestDataSize];
  memset(buffer, 0, sizeof(buffer));

  auto e_receive = [&]() {
    return socket.Receive(buffer, sizeof(buffer), TCPTest::kTestDataSize);
  };

  auto [future_receive, k_receive] = PromisifyForTest(e_receive());

  k_receive.Register(interrupt_receive);

  interrupt_receive.Trigger();

  k_receive.Start();

  EventLoop::Default().RunUntil(future_receive);

  EXPECT_THROW(future_receive.get(), eventuals::StoppedException);

  EXPECT_EQ(strlen(buffer), 0);

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
// since Send and Receive operations are asynchronous.
TEST_F(TCPSSLTest, SocketSendReceiveInterruptAfterStart) {
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

  EventLoop::Default().RunUntil(future_connect, future_accept);

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

  EventLoop::Default().RunUntil(
      future_socket_handshake,
      future_accepted_handshake);

  EXPECT_NO_THROW(future_socket_handshake.get());
  EXPECT_NO_THROW(future_accepted_handshake.get());

  // ---------------------------------------------------------------------
  // Interrupt: Send data from socket.
  // ---------------------------------------------------------------------
  eventuals::Interrupt interrupt_send;

  auto e_send = [&]() {
    return socket.Send(TCPTest::kTestData, TCPTest::kTestDataSize);
  };

  auto [future_send, k_send] = PromisifyForTest(e_send());

  k_send.Register(interrupt_send);

  k_send.Start();

  interrupt_send.Trigger();

  EventLoop::Default().RunUntil(future_send);

  EXPECT_THROW(future_send.get(), eventuals::StoppedException);

  // ---------------------------------------------------------------------
  // Interrupt: Receive data.
  // ---------------------------------------------------------------------
  eventuals::Interrupt interrupt_receive;

  char buffer[TCPTest::kTestDataSize];
  memset(buffer, 0, sizeof(buffer));

  auto e_receive = [&]() {
    return socket.Receive(buffer, sizeof(buffer), TCPTest::kTestDataSize);
  };

  auto [future_receive, k_receive] = PromisifyForTest(e_receive());

  k_receive.Register(interrupt_receive);

  k_receive.Start();

  interrupt_receive.Trigger();

  EventLoop::Default().RunUntil(future_receive);

  EXPECT_THROW(future_receive.get(), eventuals::StoppedException);

  EXPECT_EQ(strlen(buffer), 0);

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
