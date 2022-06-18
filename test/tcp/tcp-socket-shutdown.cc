#include "tcp.h"

namespace eventuals::test {
namespace {

using testing::StrEq;
using testing::ThrowsMessage;

using eventuals::ip::tcp::Acceptor;
using eventuals::ip::tcp::Protocol;
using eventuals::ip::tcp::ShutdownType;
using eventuals::ip::tcp::Socket;

TEST_F(TCPTest, ShutdownSendSuccess) {
  // ---------------------------------------------------------------------
  // Setup section.
  // ---------------------------------------------------------------------
  Acceptor acceptor(Protocol::IPV4);
  Socket socket(Protocol::IPV4);
  Socket accepted(Protocol::IPV4);

  eventuals::Interrupt interrupt_setup;

  auto e_setup = [&]() {
    return acceptor.Open()
        >> socket.Open()
        >> acceptor.Bind(TCPTest::kLocalHostIPV4, TCPTest::kAnyPort)
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
    return socket.Connect(TCPTest::kLocalHostIPV4, acceptor.ListeningPort());
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
  // Shutdown socket's send channel.
  // ---------------------------------------------------------------------
  eventuals::Interrupt interrupt_shutdown_send;

  auto e_shutdown_send = [&]() {
    return socket.Shutdown(ShutdownType::SEND);
  };

  auto [future_shutdown_send, k_shutdown_send] =
      PromisifyForTest(e_shutdown_send());

  k_shutdown_send.Register(interrupt_shutdown_send);

  k_shutdown_send.Start();

  EventLoop::Default().RunUntil(future_shutdown_send);

  EXPECT_NO_THROW(future_shutdown_send.get());

  // ---------------------------------------------------------------------
  // Try to send data (socket -> accepted). Should fail.
  // ---------------------------------------------------------------------
  eventuals::Interrupt interrupt_send_to_accepted;
  eventuals::Interrupt interrupt_receive_from_socket;

  auto e_send_to_accepted = [&]() {
    return socket.Send(TCPTest::kTestData, TCPTest::kTestDataSize);
  };

  auto [future_send_to_accepted, k_send_to_accepted] =
      PromisifyForTest(e_send_to_accepted());

  k_send_to_accepted.Register(interrupt_send_to_accepted);

  k_send_to_accepted.Start();

  EventLoop::Default().RunUntil(future_send_to_accepted);

  // Not using EXPECT_THAT since
  // the message depends on the language set in the OS.
  EXPECT_THROW(future_send_to_accepted.get(), std::runtime_error);

  // ---------------------------------------------------------------------
  // Send and receive data (accepted -> socket). Should still work.
  // ---------------------------------------------------------------------
  eventuals::Interrupt interrupt_send_to_socket;
  eventuals::Interrupt interrupt_receive_from_accepted;

  char buffer[TCPTest::kTestDataSize];
  memset(buffer, 0, sizeof(buffer));

  auto e_send_to_socket = [&]() {
    return accepted.Send(TCPTest::kTestData, TCPTest::kTestDataSize);
  };

  auto e_receive_from_accepted = [&]() {
    return socket.Receive(buffer, sizeof(buffer), TCPTest::kTestDataSize);
  };

  auto [future_send_to_socket, k_send_to_socket] =
      PromisifyForTest(e_send_to_socket());
  auto [future_receive_from_accepted, k_receive_from_accepted] =
      PromisifyForTest(e_receive_from_accepted());

  k_send_to_socket.Register(interrupt_send_to_socket);
  k_receive_from_accepted.Register(interrupt_receive_from_accepted);

  k_send_to_socket.Start();
  k_receive_from_accepted.Start();

  EventLoop::Default().RunUntil(future_send_to_socket);
  EventLoop::Default().RunUntil(future_receive_from_accepted);

  EXPECT_NO_THROW(future_send_to_socket.get());
  EXPECT_NO_THROW(future_receive_from_accepted.get());

  EXPECT_STREQ(buffer, TCPTest::kTestData);

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


TEST_F(TCPTest, ShutdownReceiveSuccess) {
  // ---------------------------------------------------------------------
  // Setup section.
  // ---------------------------------------------------------------------
  Acceptor acceptor(Protocol::IPV4);
  Socket socket(Protocol::IPV4);
  Socket accepted(Protocol::IPV4);

  eventuals::Interrupt interrupt_setup;

  auto e_setup = [&]() {
    return acceptor.Open()
        >> socket.Open()
        >> acceptor.Bind(TCPTest::kLocalHostIPV4, TCPTest::kAnyPort)
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
    return socket.Connect(TCPTest::kLocalHostIPV4, acceptor.ListeningPort());
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
  // Shutdown socket's receive channel.
  // ---------------------------------------------------------------------
  eventuals::Interrupt interrupt_shutdown_send;

  auto e_shutdown_send = [&]() {
    return socket.Shutdown(ShutdownType::RECEIVE);
  };

  auto [future_shutdown_send, k_shutdown_send] =
      PromisifyForTest(e_shutdown_send());

  k_shutdown_send.Register(interrupt_shutdown_send);

  k_shutdown_send.Start();

  EventLoop::Default().RunUntil(future_shutdown_send);

  EXPECT_NO_THROW(future_shutdown_send.get());

  // ---------------------------------------------------------------------
  // Try to receive data (accepted -> socket). Should fail.
  // ---------------------------------------------------------------------
  eventuals::Interrupt interrupt_receive_from_accepted;

  char buffer[TCPTest::kTestDataSize];
  memset(buffer, 0, sizeof(buffer));

  auto e_receive_from_accepted = [&]() {
    return socket.Receive(buffer, sizeof(buffer), TCPTest::kTestDataSize);
  };

  auto [future_receive_from_accepted, k_receive_from_accepted] =
      PromisifyForTest(e_receive_from_accepted());

  k_receive_from_accepted.Register(interrupt_receive_from_accepted);

  k_receive_from_accepted.Start();

  EventLoop::Default().RunUntil(future_receive_from_accepted);

  // Not using EXPECT_THAT since
  // the message depends on the language set in the OS.
  EXPECT_THROW(future_receive_from_accepted.get(), std::runtime_error);

  // ---------------------------------------------------------------------
  // Send and receive data (socket -> accepted). Should still work.
  // ---------------------------------------------------------------------
  eventuals::Interrupt interrupt_send_to_accepted;
  eventuals::Interrupt interrupt_receive_from_socket;

  memset(buffer, 0, sizeof(buffer));

  auto e_send_to_accepted = [&]() {
    return socket.Send(TCPTest::kTestData, TCPTest::kTestDataSize);
  };

  auto e_receive_from_socket = [&]() {
    return accepted.Receive(buffer, sizeof(buffer), TCPTest::kTestDataSize);
  };

  auto [future_send_to_accepted, k_send_to_accepted] =
      PromisifyForTest(e_send_to_accepted());
  auto [future_receive_from_socket, k_receive_from_socket] =
      PromisifyForTest(e_receive_from_socket());

  k_send_to_accepted.Register(interrupt_send_to_accepted);
  k_receive_from_socket.Register(interrupt_receive_from_socket);

  k_send_to_accepted.Start();
  k_receive_from_socket.Start();

  EventLoop::Default().RunUntil(future_send_to_accepted);
  EventLoop::Default().RunUntil(future_receive_from_socket);

  EXPECT_NO_THROW(future_send_to_accepted.get());
  EXPECT_NO_THROW(future_receive_from_socket.get());

  EXPECT_STREQ(buffer, TCPTest::kTestData);

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


TEST_F(TCPTest, ShutdownBothSuccess) {
  // ---------------------------------------------------------------------
  // Setup section.
  // ---------------------------------------------------------------------
  Acceptor acceptor(Protocol::IPV4);
  Socket socket(Protocol::IPV4);
  Socket accepted(Protocol::IPV4);

  eventuals::Interrupt interrupt_setup;

  auto e_setup = [&]() {
    return acceptor.Open()
        >> socket.Open()
        >> acceptor.Bind(TCPTest::kLocalHostIPV4, TCPTest::kAnyPort)
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
    return socket.Connect(TCPTest::kLocalHostIPV4, acceptor.ListeningPort());
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
  // Shutdown socket's both send and receive channels.
  // ---------------------------------------------------------------------
  eventuals::Interrupt interrupt_shutdown_send;

  auto e_shutdown_send = [&]() {
    return socket.Shutdown(ShutdownType::BOTH);
  };

  auto [future_shutdown_send, k_shutdown_send] =
      PromisifyForTest(e_shutdown_send());

  k_shutdown_send.Register(interrupt_shutdown_send);

  k_shutdown_send.Start();

  EventLoop::Default().RunUntil(future_shutdown_send);

  EXPECT_NO_THROW(future_shutdown_send.get());

  // ---------------------------------------------------------------------
  // Try to send data (socket -> accepted). Should fail.
  // ---------------------------------------------------------------------
  eventuals::Interrupt interrupt_send_to_accepted;
  eventuals::Interrupt interrupt_receive_from_socket;

  auto e_send_to_accepted = [&]() {
    return socket.Send(TCPTest::kTestData, TCPTest::kTestDataSize);
  };

  auto [future_send_to_accepted, k_send_to_accepted] =
      PromisifyForTest(e_send_to_accepted());

  k_send_to_accepted.Register(interrupt_send_to_accepted);

  k_send_to_accepted.Start();

  EventLoop::Default().RunUntil(future_send_to_accepted);

  // Not using EXPECT_THAT since
  // the message depends on the language set in the OS.
  EXPECT_THROW(future_send_to_accepted.get(), std::runtime_error);

  // ---------------------------------------------------------------------
  // Try to receive data (accepted -> socket). Should fail.
  // ---------------------------------------------------------------------
  eventuals::Interrupt interrupt_receive_from_accepted;

  char buffer[TCPTest::kTestDataSize];
  memset(buffer, 0, sizeof(buffer));

  auto e_receive_from_accepted = [&]() {
    return socket.Receive(buffer, sizeof(buffer), TCPTest::kTestDataSize);
  };

  auto [future_receive_from_accepted, k_receive_from_accepted] =
      PromisifyForTest(e_receive_from_accepted());

  k_receive_from_accepted.Register(interrupt_receive_from_accepted);

  k_receive_from_accepted.Start();

  EventLoop::Default().RunUntil(future_receive_from_accepted);

  // Not using EXPECT_THAT since
  // the message depends on the language set in the OS.
  EXPECT_THROW(future_receive_from_accepted.get(), std::runtime_error);

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


TEST_F(TCPTest, ShutdownClosedFail) {
  Socket socket(Protocol::IPV4);

  eventuals::Interrupt interrupt;

  auto e = [&]() {
    return socket.Shutdown(ShutdownType::BOTH);
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


// NOTE: we don't need to do separate tests for
// calling interrupt.Trigger() before and after k.Start()
// since Shutdown operation is not asynchronous.
TEST_F(TCPTest, ShutdownInterrupt) {
  // ---------------------------------------------------------------------
  // Setup section.
  // ---------------------------------------------------------------------
  Acceptor acceptor(Protocol::IPV4);
  Socket socket(Protocol::IPV4);
  Socket accepted(Protocol::IPV4);

  eventuals::Interrupt interrupt_setup;

  auto e_setup = [&]() {
    return acceptor.Open()
        >> socket.Open()
        >> acceptor.Bind(TCPTest::kLocalHostIPV4, TCPTest::kAnyPort)
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
    return socket.Connect(TCPTest::kLocalHostIPV4, acceptor.ListeningPort());
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
  // Interrupted: Shutdown socket's both send and receive channels.
  // ---------------------------------------------------------------------
  eventuals::Interrupt interrupt_shutdown_send;

  auto e_shutdown_send = [&]() {
    return socket.Shutdown(ShutdownType::BOTH);
  };

  auto [future_shutdown_send, k_shutdown_send] =
      PromisifyForTest(e_shutdown_send());

  k_shutdown_send.Register(interrupt_shutdown_send);

  k_shutdown_send.Start();

  interrupt_shutdown_send.Trigger();

  EventLoop::Default().RunUntil(future_shutdown_send);

  EXPECT_THROW(future_shutdown_send.get(), eventuals::StoppedException);

  // ---------------------------------------------------------------------
  // Send and receive data (socket -> accepted). Should still work.
  // ---------------------------------------------------------------------
  eventuals::Interrupt interrupt_send_to_accepted;
  eventuals::Interrupt interrupt_receive_from_socket;

  char buffer[TCPTest::kTestDataSize];
  memset(buffer, 0, sizeof(buffer));

  auto e_send_to_accepted = [&]() {
    return socket.Send(TCPTest::kTestData, TCPTest::kTestDataSize);
  };

  auto e_receive_from_socket = [&]() {
    return accepted.Receive(buffer, sizeof(buffer), TCPTest::kTestDataSize);
  };

  auto [future_send_to_accepted, k_send_to_accepted] =
      PromisifyForTest(e_send_to_accepted());
  auto [future_receive_from_socket, k_receive_from_socket] =
      PromisifyForTest(e_receive_from_socket());

  k_send_to_accepted.Register(interrupt_send_to_accepted);
  k_receive_from_socket.Register(interrupt_receive_from_socket);

  k_send_to_accepted.Start();
  k_receive_from_socket.Start();

  EventLoop::Default().RunUntil(future_send_to_accepted);
  EventLoop::Default().RunUntil(future_receive_from_socket);

  EXPECT_NO_THROW(future_send_to_accepted.get());
  EXPECT_NO_THROW(future_receive_from_socket.get());

  EXPECT_STREQ(buffer, TCPTest::kTestData);

  // ---------------------------------------------------------------------
  // Send and receive data (accepted -> socket). Should still work.
  // ---------------------------------------------------------------------
  eventuals::Interrupt interrupt_send_to_socket;
  eventuals::Interrupt interrupt_receive_from_accepted;

  memset(buffer, 0, sizeof(buffer));

  auto e_send_to_socket = [&]() {
    return accepted.Send(TCPTest::kTestData, TCPTest::kTestDataSize);
  };

  auto e_receive_from_accepted = [&]() {
    return socket.Receive(buffer, sizeof(buffer), TCPTest::kTestDataSize);
  };

  auto [future_send_to_socket, k_send_to_socket] =
      PromisifyForTest(e_send_to_socket());
  auto [future_receive_from_accepted, k_receive_from_accepted] =
      PromisifyForTest(e_receive_from_accepted());

  k_send_to_socket.Register(interrupt_send_to_socket);
  k_receive_from_accepted.Register(interrupt_receive_from_accepted);

  k_send_to_socket.Start();
  k_receive_from_accepted.Start();

  EventLoop::Default().RunUntil(future_send_to_socket);
  EventLoop::Default().RunUntil(future_receive_from_accepted);

  EXPECT_NO_THROW(future_send_to_socket.get());
  EXPECT_NO_THROW(future_receive_from_accepted.get());

  EXPECT_STREQ(buffer, TCPTest::kTestData);

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
