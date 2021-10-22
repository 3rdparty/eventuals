#include "eventuals/tcp.h"

#include "eventuals/eventual.h"
#include "eventuals/just.h"
#include "eventuals/let.h"
#include "eventuals/terminal.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "test/event-loop-test.h"

using eventuals::EventLoop;
using eventuals::Interrupt;
using eventuals::Just;
using eventuals::Let;
using eventuals::Terminate;
using eventuals::Then;

using eventuals::ip::tcp::AcceptOnce;

class TCPTest : public EventLoopTest {};

// NOTE: Windows has different socket API, so we have 2 sets of tests.
#if !defined(_WIN32)

TEST_F(TCPTest, AcceptOnceSucceed) {
  auto e = AcceptOnce("0.0.0.0", 50000)
      | Then([](auto&& client_socket) {
             return client_socket.Close();
           });

  auto [future, k] = Terminate(std::move(e));
  k.Start();

  EventLoop::Waiter waiter(&EventLoop::Default(), "connect");

  EventLoop::Default().Submit(
      []() {
        struct sockaddr_in addr = {0};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(50000);
        addr.sin_addr.s_addr = inet_addr("127.0.0.1");

        int socket_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        EXPECT_NE(socket_fd, -1) << strerror(errno);
        int status = connect(socket_fd, (struct sockaddr*) &addr, sizeof(addr));
        EXPECT_EQ(status, 0) << strerror(errno);
        status = close(socket_fd);
        EXPECT_EQ(status, 0) << strerror(errno);
      },
      &waiter);

  EXPECT_EQ(
      std::future_status::timeout,
      future.wait_for(std::chrono::seconds(0)));

  EventLoop::Default().Run();

  future.get();
}


TEST_F(TCPTest, AcceptOnceInvalidPortFail) {
  auto e = AcceptOnce("0.0.0.0", 80000);

  auto [future, k] = Terminate(std::move(e));
  k.Start();

  EventLoop::Default().Run();

  EXPECT_THROW(future.get(), const char*);
}


TEST_F(TCPTest, AcceptOnceInvalidAddressFail) {
  auto e = AcceptOnce("0.0.0.256", 80000);

  auto [future, k] = Terminate(std::move(e));
  k.Start();

  EventLoop::Default().Run();

  EXPECT_THROW(future.get(), const char*);
}


TEST_F(TCPTest, AcceptOnceInterrupt) {
  auto e = AcceptOnce("0.0.0.0", 50000);

  auto [future, k] = Terminate(std::move(e));

  Interrupt interrupt;

  k.Register(interrupt);

  k.Start();

  EXPECT_EQ(
      std::future_status::timeout,
      future.wait_for(std::chrono::seconds(0)));

  interrupt.Trigger();

  EventLoop::Default().Run();

  EXPECT_THROW(future.get(), eventuals::StoppedException);
}


TEST_F(TCPTest, WriteSucceed) {
  std::string some_data = "Hello World!";
  int socket_fd = -1;

  auto e = AcceptOnce("0.0.0.0", 50000)
      | Then(Let([&](auto& client_socket) {
             return client_socket.Write(some_data)
                 | Then([&]() {
                      std::string data;
                      data.resize(some_data.size());
                      int bytes_to_read = some_data.size();
                      while (bytes_to_read > 0) {
                        char* buffer_ptr = const_cast<char*>(data.data())
                            + some_data.size()
                            - bytes_to_read;
                        int bytes_read = read(
                            socket_fd,
                            buffer_ptr,
                            bytes_to_read);
                        EXPECT_NE(bytes_read, -1) << strerror(errno);
                        bytes_to_read -= bytes_read;
                      }
                      EXPECT_EQ(bytes_to_read, 0);
                      EXPECT_EQ(data, some_data);
                      int status = close(socket_fd);
                      EXPECT_EQ(status, 0) << strerror(errno);
                    })
                 | client_socket.Close();
           }));

  auto [future, k] = Terminate(std::move(e));
  k.Start();

  EventLoop::Waiter waiter(&EventLoop::Default(), "connect");

  EventLoop::Default().Submit(
      [&socket_fd]() {
        struct sockaddr_in addr = {0};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(50000);
        addr.sin_addr.s_addr = inet_addr("127.0.0.1");

        socket_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        EXPECT_NE(socket_fd, -1) << strerror(errno);
        int status = connect(socket_fd, (struct sockaddr*) &addr, sizeof(addr));
        EXPECT_EQ(status, 0) << strerror(errno);
      },
      &waiter);

  EXPECT_EQ(
      std::future_status::timeout,
      future.wait_for(std::chrono::seconds(0)));

  EventLoop::Default().Run();

  future.get();
}


TEST_F(TCPTest, WriteInterrupt) {
  std::string some_data = "Hello World!";
  int socket_fd = -1;

  auto e1 = AcceptOnce("0.0.0.0", 50000);
  auto [future1, k1] = Terminate(std::move(e1));

  k1.Start();

  EventLoop::Waiter waiter(&EventLoop::Default(), "connect");

  EventLoop::Default().Submit(
      [&socket_fd]() {
        struct sockaddr_in addr = {0};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(50000);
        addr.sin_addr.s_addr = inet_addr("127.0.0.1");

        socket_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        EXPECT_NE(socket_fd, -1) << strerror(errno);
        int status = connect(socket_fd, (struct sockaddr*) &addr, sizeof(addr));
        EXPECT_EQ(status, 0) << strerror(errno);
      },
      &waiter);

  EXPECT_EQ(
      std::future_status::timeout,
      future1.wait_for(std::chrono::seconds(0)));

  EventLoop::Default().Run();

  auto client_socket = future1.get();

  auto e2 = client_socket.Write(some_data);
  auto [future2, k2] = Terminate(std::move(e2));

  Interrupt interrupt;

  k2.Register(interrupt);

  k2.Start();

  interrupt.Trigger();

  EventLoop::Default().Run();

  EXPECT_THROW(future2.get(), eventuals::StoppedException);

  int status = close(socket_fd);
  EXPECT_EQ(status, 0) << strerror(errno);

  auto e3 = client_socket.Close();
  auto [future3, k3] = Terminate(std::move(e3));

  k3.Start();

  EventLoop::Default().Run();

  future3.get();
}


TEST_F(TCPTest, ReadSucceed) {
  std::string some_data = "Hello World!";
  int socket_fd = -1;

  auto e = AcceptOnce("0.0.0.0", 50000)
      | Then(Let([&](auto& client_socket) {
             return Then([&]() {
                      int bytes_to_write = some_data.size();
                      while (bytes_to_write > 0) {
                        char* buffer_ptr = some_data.data()
                            + some_data.size()
                            - bytes_to_write;
                        int bytes_written = write(
                            socket_fd,
                            buffer_ptr,
                            bytes_to_write);
                        EXPECT_NE(bytes_written, -1) << strerror(errno);
                        bytes_to_write -= bytes_written;
                      }
                      EXPECT_EQ(bytes_to_write, 0);
                    })
                 | client_socket.Read(some_data.size())
                 | Then([&](auto&& data) {
                      EXPECT_EQ(data, some_data);
                      int status = close(socket_fd);
                      EXPECT_EQ(status, 0) << strerror(errno);
                    })
                 | client_socket.Close();
           }));

  auto [future, k] = Terminate(std::move(e));
  k.Start();

  EventLoop::Waiter waiter(&EventLoop::Default(), "connect");

  EventLoop::Default().Submit(
      [&socket_fd]() {
        struct sockaddr_in addr = {0};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(50000);
        addr.sin_addr.s_addr = inet_addr("127.0.0.1");

        socket_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        EXPECT_NE(socket_fd, -1) << strerror(errno);
        int status = connect(socket_fd, (struct sockaddr*) &addr, sizeof(addr));
        EXPECT_EQ(status, 0) << strerror(errno);
      },
      &waiter);

  EXPECT_EQ(
      std::future_status::timeout,
      future.wait_for(std::chrono::seconds(0)));

  EventLoop::Default().Run();

  future.get();
}


TEST_F(TCPTest, ReadInterrupt) {
  std::string some_data = "Hello World!";
  int socket_fd = -1;

  auto e1 = AcceptOnce("0.0.0.0", 50000);
  auto [future1, k1] = Terminate(std::move(e1));

  k1.Start();

  EventLoop::Waiter waiter(&EventLoop::Default(), "connect");

  EventLoop::Default().Submit(
      [&socket_fd]() {
        struct sockaddr_in addr = {0};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(50000);
        addr.sin_addr.s_addr = inet_addr("127.0.0.1");

        socket_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        EXPECT_NE(socket_fd, -1) << strerror(errno);
        int status = connect(socket_fd, (struct sockaddr*) &addr, sizeof(addr));
        EXPECT_EQ(status, 0) << strerror(errno);
      },
      &waiter);

  EXPECT_EQ(
      std::future_status::timeout,
      future1.wait_for(std::chrono::seconds(0)));

  EventLoop::Default().Run();

  auto client_socket = future1.get();

  auto e2 = client_socket.Read(some_data.size());
  auto [future2, k2] = Terminate(std::move(e2));

  Interrupt interrupt;

  k2.Register(interrupt);

  k2.Start();

  interrupt.Trigger();

  EventLoop::Default().Run();

  EXPECT_THROW(future2.get(), eventuals::StoppedException);

  int status = close(socket_fd);
  EXPECT_EQ(status, 0) << strerror(errno);

  auto e3 = client_socket.Close();
  auto [future3, k3] = Terminate(std::move(e3));

  k3.Start();

  EventLoop::Default().Run();

  future3.get();
}

#else

TEST_F(TCPTest, AcceptSucceed) {
  auto e = AcceptOnce("0.0.0.0", 50000)
      | Then([](auto&& client_socket) {
             return client_socket.Close();
           });

  auto [future, k] = Terminate(std::move(e));
  k.Start();

  EventLoop::Waiter waiter(&EventLoop::Default(), "connect");

  EventLoop::Default().Submit(
      []() {
        struct sockaddr_in addr = {0};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(50000);
        addr.sin_addr.s_addr = inet_addr("127.0.0.1");

        SOCKET socket_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        EXPECT_NE(socket_fd, INVALID_SOCKET)
            << "Error code: "
            << WSAGetLastError();
        int status = connect(socket_fd, (struct sockaddr*) &addr, sizeof(addr));
        EXPECT_NE(status, SOCKET_ERROR)
            << "Error code: "
            << WSAGetLastError();
        status = closesocket(socket_fd);
        EXPECT_NE(status, SOCKET_ERROR)
            << "Error code: "
            << WSAGetLastError();
      },
      &waiter);

  EXPECT_EQ(
      std::future_status::timeout,
      future.wait_for(std::chrono::seconds(0)));

  EventLoop::Default().Run();

  future.get();
}


TEST_F(TCPTest, AcceptOnceInvalidPortFail) {
  auto e = AcceptOnce("0.0.0.0", 80000);

  auto [future, k] = Terminate(std::move(e));
  k.Start();

  EventLoop::Default().Run();

  EXPECT_THROW(future.get(), const char*);
}


TEST_F(TCPTest, AcceptOnceInvalidAddressFail) {
  auto e = AcceptOnce("0.0.0.256", 80000);

  auto [future, k] = Terminate(std::move(e));
  k.Start();

  EventLoop::Default().Run();

  EXPECT_THROW(future.get(), const char*);
}


TEST_F(TCPTest, AcceptInterrupt) {
  auto e = AcceptOnce("0.0.0.0", 50000);

  auto [future, k] = Terminate(std::move(e));

  Interrupt interrupt;

  k.Register(interrupt);

  k.Start();

  EXPECT_EQ(
      std::future_status::timeout,
      future.wait_for(std::chrono::seconds(0)));

  interrupt.Trigger();

  EventLoop::Default().Run();

  EXPECT_THROW(future.get(), eventuals::StoppedException);
}


TEST_F(TCPTest, WriteSucceed) {
  std::string some_data = "Hello World!";
  SOCKET socket_fd = INVALID_SOCKET;

  auto e = AcceptOnce("0.0.0.0", 50000)
      | Then(Let([&](auto& client_socket) {
             return client_socket.Write(some_data)
                 | Then([&]() {
                      std::string data;
                      data.resize(some_data.size());
                      int bytes_to_read = some_data.size();
                      while (bytes_to_read > 0) {
                        char* buffer_ptr = const_cast<char*>(data.data())
                            + some_data.size()
                            - bytes_to_read;
                        int bytes_read = recv(
                            socket_fd,
                            buffer_ptr,
                            bytes_to_read,
                            0);
                        EXPECT_NE(bytes_read, SOCKET_ERROR)
                            << "Error code: "
                            << WSAGetLastError();
                        bytes_to_read -= bytes_read;
                      }
                      EXPECT_EQ(bytes_to_read, 0);
                      EXPECT_EQ(data, some_data);
                      int status = closesocket(socket_fd);
                      EXPECT_NE(status, SOCKET_ERROR)
                          << "Error code: "
                          << WSAGetLastError();
                    })
                 | client_socket.Close();
           }));

  auto [future, k] = Terminate(std::move(e));
  k.Start();

  EventLoop::Waiter waiter(&EventLoop::Default(), "connect");

  EventLoop::Default().Submit(
      [&socket_fd]() {
        struct sockaddr_in addr = {0};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(50000);
        addr.sin_addr.s_addr = inet_addr("127.0.0.1");

        socket_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        EXPECT_NE(socket_fd, INVALID_SOCKET)
            << "Error code: "
            << WSAGetLastError();
        int status = connect(socket_fd, (struct sockaddr*) &addr, sizeof(addr));
        EXPECT_NE(status, SOCKET_ERROR)
            << "Error code: "
            << WSAGetLastError();
      },
      &waiter);

  EXPECT_EQ(
      std::future_status::timeout,
      future.wait_for(std::chrono::seconds(0)));

  EventLoop::Default().Run();

  future.get();
}


TEST_F(TCPTest, WriteInterrupt) {
  std::string some_data = "Hello World!";
  SOCKET socket_fd = INVALID_SOCKET;

  auto e1 = AcceptOnce("0.0.0.0", 50000);
  auto [future1, k1] = Terminate(std::move(e1));

  k1.Start();

  EventLoop::Waiter waiter(&EventLoop::Default(), "connect");

  EventLoop::Default().Submit(
      [&socket_fd]() {
        struct sockaddr_in addr = {0};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(50000);
        addr.sin_addr.s_addr = inet_addr("127.0.0.1");

        socket_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        EXPECT_NE(socket_fd, INVALID_SOCKET)
            << "Error code: "
            << WSAGetLastError();
        int status = connect(socket_fd, (struct sockaddr*) &addr, sizeof(addr));
        EXPECT_NE(status, SOCKET_ERROR)
            << "Error code: "
            << WSAGetLastError();
      },
      &waiter);

  EXPECT_EQ(
      std::future_status::timeout,
      future1.wait_for(std::chrono::seconds(0)));

  EventLoop::Default().Run();

  auto client_socket = future1.get();

  auto e2 = client_socket.Write(some_data);
  auto [future2, k2] = Terminate(std::move(e2));

  Interrupt interrupt;

  k2.Register(interrupt);

  k2.Start();

  interrupt.Trigger();

  EventLoop::Default().Run();

  EXPECT_THROW(future2.get(), eventuals::StoppedException);

  int status = closesocket(socket_fd);
  EXPECT_NE(status, SOCKET_ERROR)
      << "Error code: "
      << WSAGetLastError();

  auto e3 = client_socket.Close();
  auto [future3, k3] = Terminate(std::move(e3));

  k3.Start();

  EventLoop::Default().Run();

  future3.get();
}


TEST_F(TCPTest, ReadSucceed) {
  std::string some_data = "Hello World!";
  SOCKET socket_fd = INVALID_SOCKET;

  auto e = AcceptOnce("0.0.0.0", 50000)
      | Then(Let([&](auto& client_socket) {
             return Then([&]() {
                      int bytes_to_write = some_data.size();
                      while (bytes_to_write > 0) {
                        char* buffer_ptr = some_data.data()
                            + some_data.size()
                            - bytes_to_write;
                        int bytes_written = send(
                            socket_fd,
                            buffer_ptr,
                            bytes_to_write,
                            0);
                        EXPECT_NE(bytes_written, SOCKET_ERROR)
                            << "Error code: "
                            << WSAGetLastError();
                        bytes_to_write -= bytes_written;
                      }
                      EXPECT_EQ(bytes_to_write, 0);
                    })
                 | client_socket.Read(some_data.size())
                 | Then([&](auto&& data) {
                      EXPECT_EQ(data, some_data);
                      int status = closesocket(socket_fd);
                      EXPECT_NE(status, SOCKET_ERROR)
                          << "Error code: "
                          << WSAGetLastError();
                    })
                 | client_socket.Close();
           }));

  auto [future, k] = Terminate(std::move(e));
  k.Start();

  EventLoop::Waiter waiter(&EventLoop::Default(), "connect");

  EventLoop::Default().Submit(
      [&socket_fd]() {
        struct sockaddr_in addr = {0};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(50000);
        addr.sin_addr.s_addr = inet_addr("127.0.0.1");

        socket_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        EXPECT_NE(socket_fd, INVALID_SOCKET)
            << "Error code: "
            << WSAGetLastError();
        int status = connect(socket_fd, (struct sockaddr*) &addr, sizeof(addr));
        EXPECT_NE(status, SOCKET_ERROR)
            << "Error code: "
            << WSAGetLastError();
      },
      &waiter);

  EXPECT_EQ(
      std::future_status::timeout,
      future.wait_for(std::chrono::seconds(0)));

  EventLoop::Default().Run();

  future.get();
}


TEST_F(TCPTest, ReadInterrupt) {
  std::string some_data = "Hello World!";
  SOCKET socket_fd = INVALID_SOCKET;

  auto e1 = AcceptOnce("0.0.0.0", 50000);
  auto [future1, k1] = Terminate(std::move(e1));

  k1.Start();

  EventLoop::Waiter waiter(&EventLoop::Default(), "connect");

  EventLoop::Default().Submit(
      [&socket_fd]() {
        struct sockaddr_in addr = {0};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(50000);
        addr.sin_addr.s_addr = inet_addr("127.0.0.1");

        socket_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        EXPECT_NE(socket_fd, INVALID_SOCKET)
            << "Error code: "
            << WSAGetLastError();
        int status = connect(socket_fd, (struct sockaddr*) &addr, sizeof(addr));
        EXPECT_NE(status, SOCKET_ERROR)
            << "Error code: "
            << WSAGetLastError();
      },
      &waiter);

  EXPECT_EQ(
      std::future_status::timeout,
      future1.wait_for(std::chrono::seconds(0)));

  EventLoop::Default().Run();

  auto client_socket = future1.get();

  auto e2 = client_socket.Read(some_data.size());
  auto [future2, k2] = Terminate(std::move(e2));

  Interrupt interrupt;

  k2.Register(interrupt);

  k2.Start();

  interrupt.Trigger();

  EventLoop::Default().Run();

  EXPECT_THROW(future2.get(), eventuals::StoppedException);

  int status = closesocket(socket_fd);
  EXPECT_NE(status, SOCKET_ERROR)
      << "Error code: "
      << WSAGetLastError();

  auto e3 = client_socket.Close();
  auto [future3, k3] = Terminate(std::move(e3));

  k3.Start();

  EventLoop::Default().Run();

  future3.get();
}

#endif