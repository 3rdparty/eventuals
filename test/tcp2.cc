#include "eventuals/tcp2.h"

#include "eventuals/catch.h"
#include "eventuals/let.h"
#include "eventuals/loop.h"
#include "eventuals/map.h"
#include "eventuals/repeat.h"
#include "eventuals/terminal.h"
#include "eventuals/then.h"
#include "eventuals/until.h"
#include "gtest/gtest.h"
#include "test/event-loop-test.h"

class TCPTest : public EventLoopTest {};

using eventuals::ip::tcp::Socket;

using eventuals::Catch;
using eventuals::EventLoop;
using eventuals::Interrupt;
using eventuals::Let;
using eventuals::Loop;
using eventuals::Map;
using eventuals::Repeat;
using eventuals::Terminate;
using eventuals::Then;
using eventuals::Until;

TEST_F(TCPTest, InitializeClose) {
  Socket server;

  auto e = server.Initialize()
      | server.Close();

  auto [future, k] = Terminate(std::move(e));
  k.Start();

  EventLoop::Default().Run();

  future.get();
}


//TEST_F(TCPTest, InitializeInterrupt) {
//  Socket server;
//
//  auto e = server.Initialize();
//
//  auto [future, k] = Terminate(std::move(e));
//
//  Interrupt interrupt;
//
//  k.Register(interrupt);
//
//  interrupt.Trigger();
//
//  k.Start();
//
//  EventLoop::Default().Run();
//
//  EXPECT_THROW(future.get(), eventuals::StoppedException);
//}


TEST_F(TCPTest, Bind) {
  Socket server;

  auto e = server.Initialize()
      | server.Bind("127.0.0.1", 50000)
      | server.Close();

  auto [future, k] = Terminate(std::move(e));
  k.Start();

  EventLoop::Default().Run();

  future.get();
}


TEST_F(TCPTest, BindFail) {
  Socket socket;

  auto e = socket.Initialize()
      | socket.Bind("256.0.0.1", 50000)
      | Catch([&socket](auto&& error) {
             return socket.Close();
           });

  auto [future, k] = Terminate(std::move(e));
  k.Start();

  EventLoop::Default().Run();

  future.get();
}


//TEST_F(TCPTest, BindInterrupt) {
//  Socket server;
//
//  auto e = server.Initialize()
//      | server.Bind("127.0.0.1", 50000)
//      | server.Close();
//
//  auto [future, k] = Terminate(std::move(e));
//  k.Start();
//
//  EventLoop::Default().Run();
//
//  future.get();
//}


TEST_F(TCPTest, ServerAndClient) {
  Socket server;
  Socket accepted;

  Socket client;

  auto e = server.Initialize()
      | client.Initialize()
      | server.Bind("127.0.0.1", 50000)
      | server.Listen()
      | client.Connect("127.0.0.1", 50000)
      | server.Accept(&accepted)
      | server.Close()
      | accepted.Close()
      | client.Close();

  auto [future, k] = Terminate(std::move(e));
  k.Start();

  EventLoop::Default().RunUntil(future);
}


/*
TEST_F(TCPTest, ServerAndMultipleClients_STACKOVERFLOW) {
  Socket server;
  Socket accepted_1;
  Socket accepted_2;

  Socket client_1;
  Socket client_2;

  auto e = server.Initialize()
      | client_1.Initialize()
      | client_2.Initialize()
      | server.Bind("127.0.0.1", 50000)
      | server.Listen()
      | client_1.Connect("127.0.0.1", 50000)
      | client_2.Connect("127.0.0.1", 50000)
      | server.Accept(&accepted_1)
      | server.Accept(&accepted_2)
      | server.Close()
      | accepted_1.Close()
      | accepted_2.Close()
      | client_1.Close()
      | client_2.Close();

  auto [future, k] = Terminate(std::move(e));
  k.Start();

  EventLoop::Default().RunUntil(future);
}


TEST_F(TCPTest, ServerAndClient_SendAndReceiveToBuffer_STACKOVERFLOW) {
  std::string transferred_data = "Hello World!";
  std::string buffer(transferred_data.size(), '\0');
  size_t total_received_bytes = 0;

  Socket server;
  Socket accepted;

  Socket client;

  auto e = server.Initialize()
      | client.Initialize()
      | server.Bind("127.0.0.1", 50000)
      | server.Listen()
      | client.Connect("127.0.0.1", 50000)
      | server.Accept(&accepted)
      | client.Send(transferred_data)
      | Repeat()
      | Map([&total_received_bytes, &buffer, &accepted]() {
             return accepted.Receive(const_cast<char*>(buffer.data()) + total_received_bytes, buffer.size() - total_received_bytes);
           })
      | Loop<void>()
            .body([&total_received_bytes](auto& k, auto&& received_bytes) {
              total_received_bytes += received_bytes;
              if (total_received_bytes == received_bytes) {
                k.Done();
              } else {
                k.Next();
              }
            })
            .ended([](auto& k) {
              k.Start();
            })
      | server.Close()
      | accepted.Close()
      | client.Close();

  auto [future, k] = Terminate(std::move(e));
  k.Start();

  EventLoop::Default().RunUntil(future);

  CHECK_EQ(transferred_data, buffer);
}
*/


#if defined(_WIN32)

TEST_F(TCPTest, AcceptWinAPI) {
  Socket server;
  Socket accepted;

  SOCKET socket_fd = INVALID_SOCKET;

  auto e = server.Initialize()
      | server.Bind("127.0.0.1", 50000)
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

             int error = connect(socket_fd, (struct sockaddr*) &addr, sizeof(addr));
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


TEST_F(TCPTest, ConnectWinAPI) {
  Socket client;

  SOCKET server_fd = INVALID_SOCKET;
  SOCKET client_fd = INVALID_SOCKET;

  struct sockaddr_in addr = {0};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(50000);
  addr.sin_addr.s_addr = inet_addr("127.0.0.1");

  server_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  EXPECT_NE(server_fd, INVALID_SOCKET)
      << "Error code: "
      << WSAGetLastError();

  int error = bind(
      server_fd,
      reinterpret_cast<sockaddr*>(&addr),
      sizeof(addr));
  EXPECT_NE(error, SOCKET_ERROR)
      << "Error code: "
      << WSAGetLastError();

  error = listen(server_fd, 1);
  EXPECT_NE(error, SOCKET_ERROR)
      << "Error code: "
      << WSAGetLastError();

  // Using different thread because accept is blocking.
  std::thread thread([&server_fd, &client_fd]() {
    client_fd = accept(server_fd, nullptr, nullptr);
    EXPECT_NE(client_fd, INVALID_SOCKET)
        << "Error code: "
        << WSAGetLastError();
  });

  auto e = client.Initialize()
      | client.Connect("127.0.0.1", 50000)
      | client.Close();

  auto [future, k] = Terminate(std::move(e));
  k.Start();

  EXPECT_EQ(
      std::future_status::timeout,
      future.wait_for(std::chrono::seconds(0)));

  EventLoop::Default().RunUntil(future);

  thread.join();

  error = closesocket(server_fd);
  EXPECT_NE(error, SOCKET_ERROR)
      << "Error code: "
      << WSAGetLastError();

  error = closesocket(client_fd);
  EXPECT_NE(error, SOCKET_ERROR)
      << "Error code: "
      << WSAGetLastError();
}


TEST_F(TCPTest, SendWinAPI) {
  std::string transferred_data = "Hello World!";

  Socket server;
  Socket accepted;

  SOCKET socket_fd = INVALID_SOCKET;

  auto e = server.Initialize()
      | server.Bind("127.0.0.1", 50000)
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

             int error = connect(socket_fd, (struct sockaddr*) &addr, sizeof(addr));
             EXPECT_NE(error, SOCKET_ERROR)
                 << "Error code: "
                 << WSAGetLastError();
           })
      | server.Accept(&accepted)
      | accepted.Send(transferred_data)
      | Then([&socket_fd, &transferred_data]() {
             std::string data;
             data.resize(transferred_data.size());
             int bytes_to_read = transferred_data.size();
             while (bytes_to_read > 0) {
               char* buffer_ptr = const_cast<char*>(data.data())
                   + transferred_data.size()
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
             EXPECT_EQ(data, transferred_data);

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


TEST_F(TCPTest, ReceiveToBufferWinAPI) {
  std::string transferred_data = "Hello World!";
  std::string buffer(transferred_data.size(), '\0');
  size_t total_received_bytes = 0;

  Socket server;
  Socket accepted;

  SOCKET socket_fd = INVALID_SOCKET;

  auto e = server.Initialize()
      | server.Bind("127.0.0.1", 50000)
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

             int error = connect(socket_fd, (struct sockaddr*) &addr, sizeof(addr));
             EXPECT_NE(error, SOCKET_ERROR)
                 << "Error code: "
                 << WSAGetLastError();
           })
      | server.Accept(&accepted)
      | Then([&socket_fd, &transferred_data]() {
             int bytes_to_write = transferred_data.size();
             while (bytes_to_write > 0) {
               char* buffer_ptr = transferred_data.data()
                   + transferred_data.size()
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
      | Repeat()
      | Map([&total_received_bytes, &buffer, &accepted]() {
             return accepted.Receive(const_cast<char*>(buffer.data()) + total_received_bytes, buffer.size() - total_received_bytes);
           })
      | Loop<void>()
            .body([&total_received_bytes](auto& k, auto&& received_bytes) {
              total_received_bytes += received_bytes;
              if (total_received_bytes == received_bytes) {
                k.Done();
              } else {
                k.Next();
              }
            })
            .ended([](auto& k) {
              k.Start();
            })
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

  CHECK_EQ(transferred_data, buffer);
}


TEST_F(TCPTest, ReceiveExactlyWinAPI) {
  std::string transferred_data = "Hello World!";

  Socket server;
  Socket accepted;

  SOCKET socket_fd = INVALID_SOCKET;

  auto e = server.Initialize()
      | server.Bind("127.0.0.1", 50000)
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

             int error = connect(socket_fd, (struct sockaddr*) &addr, sizeof(addr));
             EXPECT_NE(error, SOCKET_ERROR)
                 << "Error code: "
                 << WSAGetLastError();
           })
      | server.Accept(&accepted)
      | Then([&socket_fd, &transferred_data]() {
             int bytes_to_write = transferred_data.size();
             while (bytes_to_write > 0) {
               char* buffer_ptr = transferred_data.data()
                   + transferred_data.size()
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
      | accepted.ReceiveExactly(transferred_data.size())
      | Then([&socket_fd, &transferred_data](auto&& string_transferred) {
             CHECK_EQ(string_transferred, transferred_data);

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


#else

TEST_F(TCPTest, AcceptPOSIX) {
  Socket server;
  Socket accepted;

  int socket_fd = -1;

  auto e = server.Initialize()
      | server.Bind("127.0.0.1", 50000)
      | server.Listen()
      | Then([&socket_fd]() {
             struct sockaddr_in addr = {0};
             addr.sin_family = AF_INET;
             addr.sin_port = htons(50000);
             addr.sin_addr.s_addr = inet_addr("127.0.0.1");

             int socket_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
             EXPECT_NE(socket_fd, -1) << strerror(errno);

             int error = connect(socket_fd, (struct sockaddr*) &addr, sizeof(addr));
             EXPECT_EQ(error, 0) << strerror(errno);
           })
      | server.Accept(&accepted)
      | Then([&socket_fd]() {
             int error = close(socket_fd);
             EXPECT_EQ(error, 0) << strerror(errno);
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


TEST_F(TCPTest, ConnectPOSIX) {
  Socket client;

  int server_fd = -1;
  int client_fd = -1;

  struct sockaddr_in addr = {0};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(50000);
  addr.sin_addr.s_addr = inet_addr("127.0.0.1");

  server_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  EXPECT_NE(server_fd, -1) << strerror(errno);

  int error = bind(
      server_fd,
      reinterpret_cast<sockaddr*>(&addr),
      sizeof(addr));
  EXPECT_EQ(error, 0) << strerror(errno);

  error = listen(server_fd, 1);
  EXPECT_EQ(error, 0) << strerror(errno);

  // Using different thread because accept is blocking.
  std::thread thread([&server_fd, &client_fd]() {
    client_fd = accept(server_fd, nullptr, nullptr);
    EXPECT_NE(client_fd, -1) << strerror(errno);
  });

  auto e = client.Initialize()
      | client.Connect("127.0.0.1", 50000)
      | client.Close();

  auto [future, k] = Terminate(std::move(e));
  k.Start();

  EXPECT_EQ(
      std::future_status::timeout,
      future.wait_for(std::chrono::seconds(0)));

  EventLoop::Default().RunUntil(future);

  thread.join();

  error = close(server_fd);
  EXPECT_EQ(error, 0) << strerror(errno);

  error = close(client_fd);
  EXPECT_EQ(error, 0) << strerror(errno);
}


TEST_F(TCPTest, SendPOSIX) {
  std::string transferred_data = "Hello World!";

  Socket server;
  Socket accepted;

  int socket_fd = -1;

  auto e = server.Initialize()
      | server.Bind("127.0.0.1", 50000)
      | server.Listen()
      | Then([&socket_fd]() {
             struct sockaddr_in addr = {0};
             addr.sin_family = AF_INET;
             addr.sin_port = htons(50000);
             addr.sin_addr.s_addr = inet_addr("127.0.0.1");

             socket_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
             EXPECT_NE(socket_fd, -1) << strerror(errno);

             int error = connect(socket_fd, (struct sockaddr*) &addr, sizeof(addr));
             EXPECT_EQ(error, 0) << strerror(errno);
           })
      | server.Accept(&accepted)
      | accepted.Send(transferred_data)
      | Then([&socket_fd, &transferred_data]() {
             std::string data;
             data.resize(transferred_data.size());
             int bytes_to_read = transferred_data.size();
             while (bytes_to_read > 0) {
               char* buffer_ptr = const_cast<char*>(data.data())
                   + transferred_data.size()
                   - bytes_to_read;
               int bytes_read = read(
                   socket_fd,
                   buffer_ptr,
                   bytes_to_read);

               EXPECT_NE(bytes_read, -1) << strerror(errno);

               bytes_to_read -= bytes_read;
             }
             EXPECT_EQ(bytes_to_read, 0);
             EXPECT_EQ(data, transferred_data);

             int error = close(socket_fd);
             EXPECT_EQ(error, 0) << strerror(errno);
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


TEST_F(TCPTest, ReceiveToBufferPOSIX) {
  std::string transferred_data = "Hello World!";
  std::string buffer(transferred_data.size(), '\0');
  size_t total_received_bytes = 0;

  Socket server;
  Socket accepted;

  int socket_fd = -1;

  auto e = server.Initialize()
      | server.Bind("127.0.0.1", 50000)
      | server.Listen()
      | Then([&socket_fd]() {
             struct sockaddr_in addr = {0};
             addr.sin_family = AF_INET;
             addr.sin_port = htons(50000);
             addr.sin_addr.s_addr = inet_addr("127.0.0.1");

             socket_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
             EXPECT_NE(socket_fd, -1) << strerror(errno);

             int error = connect(socket_fd, (struct sockaddr*) &addr, sizeof(addr));
             EXPECT_EQ(error, 0) << strerror(errno);
           })
      | server.Accept(&accepted)
      | Then([&socket_fd, &transferred_data]() {
             int bytes_to_write = transferred_data.size();
             while (bytes_to_write > 0) {
               char* buffer_ptr = transferred_data.data()
                   + transferred_data.size()
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
      | Repeat()
      | Map([&total_received_bytes, &buffer, &accepted]() {
             return accepted.Receive(const_cast<char*>(buffer.data()) + total_received_bytes, buffer.size() - total_received_bytes);
           })
      | Loop<void>()
            .body([&total_received_bytes](auto& k, auto&& received_bytes) {
              total_received_bytes += received_bytes;
              if (total_received_bytes == received_bytes) {
                k.Done();
              } else {
                k.Next();
              }
            })
            .ended([](auto& k) {
              k.Start();
            })
      | Then([&socket_fd]() {
             int error = close(socket_fd);
             EXPECT_EQ(error, 0) << strerror(errno);
           })
      | server.Close()
      | accepted.Close();

  auto [future, k] = Terminate(std::move(e));
  k.Start();

  EXPECT_EQ(
      std::future_status::timeout,
      future.wait_for(std::chrono::seconds(0)));

  EventLoop::Default().RunUntil(future);

  CHECK_EQ(transferred_data, buffer);
}


TEST_F(TCPTest, ReceiveExactlyPOSIX) {
  std::string transferred_data = "Hello World!";

  Socket server;
  Socket accepted;

  int socket_fd = -1;

  auto e = server.Initialize()
      | server.Bind("127.0.0.1", 50000)
      | server.Listen()
      | Then([&socket_fd]() {
             struct sockaddr_in addr = {0};
             addr.sin_family = AF_INET;
             addr.sin_port = htons(50000);
             addr.sin_addr.s_addr = inet_addr("127.0.0.1");

             socket_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
             EXPECT_NE(socket_fd, -1) << strerror(errno);

             int error = connect(socket_fd, (struct sockaddr*) &addr, sizeof(addr));
             EXPECT_EQ(error, 0) << strerror(errno);
           })
      | server.Accept(&accepted)
      | Then([&socket_fd, &transferred_data]() {
             int bytes_to_write = transferred_data.size();
             while (bytes_to_write > 0) {
               char* buffer_ptr = transferred_data.data()
                   + transferred_data.size()
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
      | accepted.ReceiveExactly(transferred_data.size())
      | Then([&socket_fd, &transferred_data](auto&& string_transferred) {
             CHECK_EQ(string_transferred, transferred_data);

             int error = close(socket_fd);
             EXPECT_EQ(error, 0) << strerror(errno);
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
