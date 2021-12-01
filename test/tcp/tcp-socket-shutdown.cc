#include "tcp.h"
// tcp.h must be included before anything else on Windows
// due to SSL redefinitions done by wincrypt.h.
#include "eventuals/terminal.h"
#include "eventuals/then.h"

namespace eventuals::test {
namespace {

using eventuals::ip::tcp::Acceptor;
using eventuals::ip::tcp::Protocol;
using eventuals::ip::tcp::ShutdownType;
using eventuals::ip::tcp::Socket;

TEST_F(TCPTest, ShutdownSendSuccess) {
  // ---------------------------------------------------------------------
  // Setup section.
  // ---------------------------------------------------------------------
  Acceptor acceptor;
  Socket socket;
  Socket accepted;

  eventuals::Interrupt interrupt_setup;

  auto e_setup = [&]() {
    return acceptor.Open(Protocol::IPV4)
        >> socket.Open(Protocol::IPV4)
        >> acceptor.Bind(TCPTest::kLocalHostIPV4, TCPTest::kAnyPort)
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
    return socket.Connect(TCPTest::kLocalHostIPV4, acceptor.BoundPort());
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

  EXPECT_EQ(0, memcmp(TCPTest::kTestData, buffer, TCPTest::kTestDataSize));

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


TEST_F(TCPTest, ShutdownReceiveSuccess) {
  // ---------------------------------------------------------------------
  // Setup section.
  // ---------------------------------------------------------------------
  Acceptor acceptor;
  Socket socket;
  Socket accepted;

  eventuals::Interrupt interrupt_setup;

  auto e_setup = [&]() {
    return acceptor.Open(Protocol::IPV4)
        >> socket.Open(Protocol::IPV4)
        >> acceptor.Bind(TCPTest::kLocalHostIPV4, TCPTest::kAnyPort)
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
    return socket.Connect(TCPTest::kLocalHostIPV4, acceptor.BoundPort());
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

  EXPECT_EQ(0, memcmp(TCPTest::kTestData, buffer, TCPTest::kTestDataSize));

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


TEST_F(TCPTest, ShutdownBothSuccess) {
  // ---------------------------------------------------------------------
  // Setup section.
  // ---------------------------------------------------------------------
  Acceptor acceptor;
  Socket socket;
  Socket accepted;

  eventuals::Interrupt interrupt_setup;

  auto e_setup = [&]() {
    return acceptor.Open(Protocol::IPV4)
        >> socket.Open(Protocol::IPV4)
        >> acceptor.Bind(TCPTest::kLocalHostIPV4, TCPTest::kAnyPort)
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
    return socket.Connect(TCPTest::kLocalHostIPV4, acceptor.BoundPort());
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

  EXPECT_THROW(future_receive_from_accepted.get(), std::runtime_error);

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

} // namespace
} // namespace eventuals::test