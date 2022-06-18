#include "tcp.h"

namespace eventuals::test {
namespace {

using testing::StrEq;
using testing::ThrowsMessage;

using eventuals::ip::tcp::Protocol;
using eventuals::ip::tcp::Socket;

TEST_F(TCPTest, SocketOpenCloseSuccess) {
  Socket socket(Protocol::IPV4);

  eventuals::Interrupt interrupt;

  EXPECT_FALSE(socket.IsOpen());

  auto e = [&]() {
    return socket.Open()
        >> Then([&socket]() {
             EXPECT_TRUE(socket.IsOpen());
           })
        >> socket.Close()
        >> Then([&socket]() {
             EXPECT_FALSE(socket.IsOpen());
           });
  };

  auto [future, k] = PromisifyForTest(e());

  k.Register(interrupt);

  k.Start();

  EventLoop::Default().RunUntil(future);

  EXPECT_NO_THROW(future.get());
}


TEST_F(TCPTest, SocketCloseClosedFail) {
  Socket socket(Protocol::IPV4);

  eventuals::Interrupt interrupt;

  EXPECT_FALSE(socket.IsOpen());

  auto e = [&]() {
    return socket.Close();
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


TEST_F(TCPTest, SocketOpenInterrupt) {
  Socket socket(Protocol::IPV4);

  eventuals::Interrupt interrupt;

  EXPECT_FALSE(socket.IsOpen());

  auto e = [&]() {
    return socket.Open();
  };

  auto [future, k] = PromisifyForTest(e());

  k.Register(interrupt);

  interrupt.Trigger();

  k.Start();

  EventLoop::Default().RunUntil(future);

  EXPECT_THROW(future.get(), eventuals::StoppedException);

  EXPECT_FALSE(socket.IsOpen());
}


TEST_F(TCPTest, SocketCloseInterrupt) {
  // ---------------------------------------------------------------------
  // Main test section.
  // ---------------------------------------------------------------------
  Socket socket(Protocol::IPV4);

  eventuals::Interrupt interrupt;

  EXPECT_FALSE(socket.IsOpen());

  auto e = [&]() {
    return socket.Open()
        >> Then([&]() {
             EXPECT_TRUE(socket.IsOpen());
             interrupt.Trigger();
           })
        >> socket.Close();
  };

  auto [future, k] = PromisifyForTest(e());

  k.Register(interrupt);

  k.Start();

  EventLoop::Default().RunUntil(future);

  EXPECT_THROW(future.get(), eventuals::StoppedException);

  EXPECT_TRUE(socket.IsOpen());

  // ---------------------------------------------------------------------
  // Cleanup section.
  // ---------------------------------------------------------------------
  eventuals::Interrupt interrupt_cleanup;

  auto e_cleanup = [&]() {
    return socket.Close()
        >> Then([&]() {
             EXPECT_FALSE(socket.IsOpen());
           });
  };

  auto [future_cleanup, k_cleanup] = PromisifyForTest(e_cleanup());

  k_cleanup.Register(interrupt_cleanup);

  k_cleanup.Start();

  EventLoop::Default().RunUntil(future_cleanup);

  EXPECT_NO_THROW(future_cleanup.get());
}

} // namespace
} // namespace eventuals::test
