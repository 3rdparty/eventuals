#include "tcp.h"

namespace eventuals::test {
namespace {

using testing::StrEq;
using testing::ThrowsMessage;

using eventuals::ip::tcp::Acceptor;
using eventuals::ip::tcp::Protocol;
using eventuals::ip::tcp::Socket;

TEST_F(TCPTest, SocketBindSuccess) {
  Socket socket(Protocol::IPV4);

  eventuals::Interrupt interrupt;

  auto e = [&]() {
    return socket.Open()
        >> socket.Bind(TCPTest::kLocalHostIPV4, TCPTest::kAnyPort)
        >> socket.Close();
  };

  auto [future, k] = PromisifyForTest(e());

  k.Register(interrupt);

  k.Start();

  EventLoop::Default().RunUntil(future);

  EXPECT_NO_THROW(future.get());
}


TEST_F(TCPTest, SocketBindAnyIPSuccess) {
  Socket socket(Protocol::IPV4);

  eventuals::Interrupt interrupt;

  auto e = [&]() {
    return socket.Open()
        >> socket.Bind(TCPTest::kAnyIPV4, TCPTest::kAnyPort)
        >> socket.Close();
  };

  auto [future, k] = PromisifyForTest(e());

  k.Register(interrupt);

  k.Start();

  EventLoop::Default().RunUntil(future);

  EXPECT_NO_THROW(future.get());
}


TEST_F(TCPTest, SocketBindBadIPFail) {
  // ---------------------------------------------------------------------
  // Main section.
  // ---------------------------------------------------------------------
  Socket socket(Protocol::IPV4);

  eventuals::Interrupt interrupt;

  auto e = socket.Open()
      >> socket.Bind("0.0.0.256", TCPTest::kAnyPort);

  auto [future, k] = PromisifyForTest(std::move(e));

  k.Register(interrupt);

  k.Start();

  EventLoop::Default().RunUntil(future);

  // Not using EXPECT_THAT since
  // the message depends on the language set in the OS.
  EXPECT_THROW(future.get(), std::runtime_error);

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


TEST_F(TCPTest, SocketBindClosedFail) {
  Socket socket(Protocol::IPV4);

  eventuals::Interrupt interrupt;

  auto e = [&]() {
    return socket.Bind(TCPTest::kAnyIPV4, TCPTest::kAnyPort);
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


TEST_F(TCPTest, SocketBindWhileConnectedFail) {
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
  // Main test section.
  // ---------------------------------------------------------------------
  eventuals::Interrupt interrupt;

  auto e = [&]() {
    return socket.Bind(TCPTest::kLocalHostIPV4, TCPTest::kAnyPort);
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
      ThrowsMessage<std::runtime_error>(StrEq("Bind call is forbidden "
                                              "while socket is connected")));

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


// NOTE: we don't need to do separate tests for
// calling interrupt.Trigger() before and after k.Start()
// since Bind operation is not asynchronous.
TEST_F(TCPTest, SocketBindInterrupt) {
  // ---------------------------------------------------------------------
  // Main section.
  // ---------------------------------------------------------------------
  Socket socket(Protocol::IPV4);

  eventuals::Interrupt interrupt;

  auto e = [&]() {
    return socket.Open()
        >> Then([&]() {
             interrupt.Trigger();
           })
        >> socket.Bind(TCPTest::kAnyIPV4, TCPTest::kAnyPort);
  };

  auto [future, k] = PromisifyForTest(e());

  k.Register(interrupt);

  k.Start();

  EventLoop::Default().RunUntil(future);

  EXPECT_THROW(future.get(), eventuals::StoppedException);

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

} // namespace
} // namespace eventuals::test
