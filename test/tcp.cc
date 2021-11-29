#include "eventuals/tcp.h"

#include "eventuals/compose.h"
#include "eventuals/terminal.h"
#include "eventuals/then.h"
#include "gtest/gtest.h"
#include "test/event-loop-test.h"

class TCPTest : public EventLoopTest {};

using eventuals::ip::tcp::Acceptor;
using eventuals::ip::tcp::Protocol;
//using eventuals::ip::tcp::Socket;

using eventuals::EventLoop;
using eventuals::Interrupt;
using eventuals::Terminal;
using eventuals::Terminate;
using eventuals::Then;
using eventuals::detail::operator|;

TEST_F(TCPTest, Bind) {
  Acceptor acceptor;

  auto e = [&acceptor]() {
    return acceptor.Open(Protocol::IPV4)
        | acceptor.Bind("127.0.0.1", 50000);
  };

  auto [future, k] = Terminate(e());
  k.Start();
  future.get();

  EXPECT_EQ(acceptor.address(), "127.0.0.1");
  EXPECT_EQ(acceptor.port(), 50000);
}


TEST_F(TCPTest, BindFail) {
  Acceptor acceptor;

  auto e = [&acceptor]() {
    return acceptor.Open(Protocol::IPV4)
        | acceptor.Bind("127.0.0.256", 50000);
  };

  auto [future, k] = Terminate(e());
  k.Start();
  EXPECT_THROW(future.get(), std::string);
}

TEST_F(TCPTest, BindInterrupt) {
  Acceptor acceptor;

  auto e = [&acceptor]() {
    return acceptor.Open(Protocol::IPV4)
        | acceptor.Bind("127.0.0.1", 50000);
  };

  Interrupt interrupt;
  auto [future, k] = Terminate(e());
  k.Register(interrupt);

  interrupt.Trigger();

  k.Start();
  EXPECT_THROW(future.get(), eventuals::StoppedException);
}

/*
#if defined(_WIN32)

TEST_F(TCPTest, AcceptWinAPI) {
  Acceptor acceptor;
  Socket accepted;

  SOCKET socket_fd = INVALID_SOCKET;

  auto e = acceptor.Bind("127.0.0.1", 50000)
      | server.Listen()
      | Then([&socket_fd]() {
             struct sockaddr_in addr = {0};
             addr.sin_family = AF_INET;
             addr.sin_port = htons(50000);
             addr.sin_addr.s_addr = inet_addr("127.0.0.1");

             socket_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
             EXPECT_NE(socket_fd, INVALID_SOCKET)
                 << "Error code: "
                 << WSAGetLastError();

             int error = connect(socket_fd, (struct sockaddr*) &addr, 
             sizeof(addr));
             EXPECT_NE(error, SOCKET_ERROR)
                 << "Error code: "
                 << WSAGetLastError();
           })
      | server.Accept(&accepted)
      | Then([&socket_fd]() {
             int error = closesocket(socket_fd);
             EXPECT_NE(error, SOCKET_ERROR)
                 << "Error code: "
                 << WSAGetLastError();
           })
      | server.Close()
      | accepted.Close();

  auto [future, k] = Terminate(std::move(e));
  k.Start();

  EXPECT_EQ(
      std::future_status::timeout,
      future.wait_for(std::chrono::seconds(0)));

  EventLoop::Default().RunUntil(future);
}

#endif
*/