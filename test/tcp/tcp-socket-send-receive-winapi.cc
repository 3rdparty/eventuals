#if defined(_WIN32)

#include "tcp.h"

namespace eventuals::test {
namespace {

using testing::StrEq;
using testing::ThrowsMessage;

using eventuals::ip::tcp::Protocol;
using eventuals::ip::tcp::Socket;

TEST_F(TCPTest, SocketSendReceiveWinApiSuccess) {
  // ---------------------------------------------------------------------
  // Setup section.
  // ---------------------------------------------------------------------
  Socket socket(Protocol::IPV4);

  std::thread winapi_thread;
  SOCKET socket_winapi = INVALID_SOCKET;
  SOCKET accepted_winapi = INVALID_SOCKET;
  uint16_t socket_port = 0;

  eventuals::Interrupt interrupt_setup;

  auto e_setup = [&]() {
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_port = htons(TCPTest::kAnyPort);
    inet_pton(AF_INET, TCPTest::kLocalHostIPV4, &address.sin_addr);

    socket_winapi = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    EXPECT_NE(socket_winapi, INVALID_SOCKET)
        << "Error code: "
        << WSAGetLastError();

    int error = bind(
        socket_winapi,
        reinterpret_cast<sockaddr*>(&address),
        sizeof(address));
    EXPECT_NE(error, SOCKET_ERROR)
        << "Error code: "
        << WSAGetLastError();

    error = listen(socket_winapi, 1);
    EXPECT_NE(error, SOCKET_ERROR)
        << "Error code: "
        << WSAGetLastError();

    ZeroMemory(&address, sizeof(address));
    int address_size = sizeof(address);
    error = getsockname(
        socket_winapi,
        reinterpret_cast<sockaddr*>(&address),
        &address_size);
    EXPECT_NE(error, SOCKET_ERROR)
        << "Error code: "
        << WSAGetLastError();

    socket_port = ntohs(address.sin_port);

    return socket.Open();
  };

  auto [future_setup, k_setup] = PromisifyForTest(e_setup());

  k_setup.Register(interrupt_setup);

  k_setup.Start();

  EventLoop::Default().RunUntil(future_setup);

  EXPECT_NO_THROW(future_setup.get());

  // ---------------------------------------------------------------------
  // Connect to WinAPI socket.
  // ---------------------------------------------------------------------
  eventuals::Interrupt interrupt_connect;

  auto e_accept = [&]() {
    return Eventual<void>()
        .start([&](auto& k) {
          winapi_thread = std::thread([&]() {
            accepted_winapi = accept(socket_winapi, nullptr, nullptr);
            EXPECT_NE(accepted_winapi, INVALID_SOCKET)
                << "Error code: "
                << WSAGetLastError();

            k.Start();
          });
        });
  };

  auto e_connect = [&]() {
    return socket.Connect(TCPTest::kLocalHostIPV4, socket_port);
  };

  auto [future_connect, k_connect] = PromisifyForTest(e_connect());
  auto [future_accept, k_accept] = PromisifyForTest(e_accept());

  k_connect.Register(interrupt_connect);

  k_connect.Start();
  k_accept.Start();

  EventLoop::Default().RunUntil(future_connect, future_accept);

  EXPECT_NO_THROW(future_connect.get());

  winapi_thread.join();

  // ---------------------------------------------------------------------
  // Send and receive data (socket -> winapi).
  // ---------------------------------------------------------------------
  eventuals::Interrupt interrupt_send_to_winapi;

  char buffer[TCPTest::kTestDataSize];
  memset(buffer, 0, sizeof(buffer));

  auto e_send_to_winapi = [&]() {
    return socket.Send(TCPTest::kTestData, TCPTest::kTestDataSize);
  };

  auto e_receive_from_socket = [&]() {
    return Eventual<void>()
        .start([&](auto& k) {
          winapi_thread = std::thread([&]() {
            size_t bytes_to_read = TCPTest::kTestDataSize;
            size_t bytes_read = 0;

            while (bytes_read < bytes_to_read) {
              char* buffer_ptr = buffer
                  + bytes_read;

              int bytes_read_this_recv = recv(
                  accepted_winapi,
                  buffer_ptr,
                  bytes_to_read - bytes_read,
                  0);
              EXPECT_NE(bytes_read_this_recv, SOCKET_ERROR)
                  << "Error code: "
                  << WSAGetLastError();

              bytes_read += bytes_read_this_recv;
            }
            EXPECT_EQ(bytes_read, bytes_to_read);

            k.Start();
          });
        });
  };

  auto [future_send_to_winapi, k_send_to_winapi] =
      PromisifyForTest(e_send_to_winapi());
  auto [future_receive_from_socket, k_receive_from_socket] =
      PromisifyForTest(e_receive_from_socket());

  k_send_to_winapi.Register(interrupt_send_to_winapi);

  k_send_to_winapi.Start();
  k_receive_from_socket.Start();

  EventLoop::Default().RunUntil(
      future_send_to_winapi,
      future_receive_from_socket);

  EXPECT_NO_THROW(future_send_to_winapi.get());
  EXPECT_NO_THROW(future_receive_from_socket.get());

  winapi_thread.join();

  EXPECT_STREQ(buffer, TCPTest::kTestData);

  // ---------------------------------------------------------------------
  // Send and receive data (winapi -> socket).
  // ---------------------------------------------------------------------
  eventuals::Interrupt interrupt_receive_from_winapi;

  memset(buffer, 0, sizeof(buffer));

  auto e_send_to_socket = [&]() {
    return Eventual<void>()
        .start([&](auto& k) {
          winapi_thread = std::thread([&]() {
            size_t bytes_to_write = TCPTest::kTestDataSize;
            size_t bytes_written = 0;

            while (bytes_written < bytes_to_write) {
              const char* data_to_send_ptr = TCPTest::kTestData
                  + bytes_written;

              int bytes_written_this_send = send(
                  accepted_winapi,
                  data_to_send_ptr,
                  bytes_to_write - bytes_written,
                  0);
              EXPECT_NE(bytes_written_this_send, SOCKET_ERROR)
                  << "Error code: "
                  << WSAGetLastError();

              bytes_written += bytes_written_this_send;
            }
            EXPECT_EQ(bytes_written, bytes_to_write);

            k.Start();
          });
        });
  };

  auto e_receive_from_winapi = [&]() {
    return socket.Receive(buffer, sizeof(buffer), TCPTest::kTestDataSize);
  };

  auto [future_send_to_socket, k_send_to_socket] =
      PromisifyForTest(e_send_to_socket());
  auto [future_receive_from_winapi, k_receive_from_winapi] =
      PromisifyForTest(e_receive_from_winapi());

  k_receive_from_winapi.Register(interrupt_receive_from_winapi);

  k_send_to_socket.Start();
  k_receive_from_winapi.Start();

  EventLoop::Default().RunUntil(
      future_send_to_socket,
      future_receive_from_winapi);

  EXPECT_NO_THROW(future_send_to_socket.get());
  EXPECT_NO_THROW(future_receive_from_winapi.get());

  winapi_thread.join();

  EXPECT_STREQ(buffer, TCPTest::kTestData);

  // ---------------------------------------------------------------------
  // Cleanup section.
  // ---------------------------------------------------------------------
  eventuals::Interrupt interrupt_cleanup;

  auto e_cleanup = [&]() {
    int error = closesocket(socket_winapi);
    EXPECT_NE(error, SOCKET_ERROR)
        << "Error code: "
        << WSAGetLastError();

    error = closesocket(accepted_winapi);
    EXPECT_NE(error, SOCKET_ERROR)
        << "Error code: "
        << WSAGetLastError();

    return socket.Close();
  };

  auto [future_cleanup, k_cleanup] = PromisifyForTest(e_cleanup());

  k_cleanup.Register(interrupt_cleanup);

  k_cleanup.Start();

  EventLoop::Default().RunUntil(future_cleanup);

  EXPECT_NO_THROW(future_cleanup.get());
}

} // namespace
} // namespace eventuals::test

#endif
