#if defined(_WIN32)

#include "tcp.h"

namespace eventuals::test {
namespace {

using eventuals::ip::tcp::Protocol;
using eventuals::ip::tcp::Socket;

TEST_F(TCPTest, SocketConnectToWinApiSocket) {
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

  EventLoop::Default().RunUntil(future_connect);

  EXPECT_NO_THROW(future_connect.get());

  winapi_thread.join();

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
