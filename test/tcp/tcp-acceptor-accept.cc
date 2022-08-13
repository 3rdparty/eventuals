// NOTE: here we don't test the successful Accept operation.
// Connect tests should cover this.

#include "tcp.h"

namespace eventuals::test {
namespace {

using testing::StrEq;
using testing::ThrowsMessage;

using eventuals::ip::tcp::Acceptor;
using eventuals::ip::tcp::Protocol;
using eventuals::ip::tcp::Socket;

TEST_F(TCPTest, AcceptorAcceptClosedFail) {
  Acceptor acceptor(Protocol::IPV4);
  Socket accepted(Protocol::IPV4);

  eventuals::Interrupt interrupt;

  auto e = [&]() {
    return acceptor.Accept(accepted);
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
      ThrowsMessage<std::runtime_error>(StrEq("Acceptor is closed")));
}


TEST_F(TCPTest, AcceptorAcceptNotListeningFail) {
  // ---------------------------------------------------------------------
  // Main section.
  // ---------------------------------------------------------------------
  Acceptor acceptor(Protocol::IPV4);
  Socket accepted(Protocol::IPV4);

  eventuals::Interrupt interrupt;

  auto e = [&]() {
    return acceptor.Open()
        >> acceptor.Bind(TCPTest::kLocalHostIPV4, TCPTest::kAnyPort)
        >> acceptor.Accept(accepted);
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
      ThrowsMessage<std::runtime_error>(StrEq("Acceptor is not listening")));

  // ---------------------------------------------------------------------
  // Cleanup section.
  // ---------------------------------------------------------------------
  eventuals::Interrupt interrupt_cleanup;

  auto e_cleanup = [&]() {
    return acceptor.Close();
  };

  auto [future_cleanup, k_cleanup] = PromisifyForTest(e_cleanup());

  k_cleanup.Register(interrupt_cleanup);

  k_cleanup.Start();

  EventLoop::Default().RunUntil(future_cleanup);

  EXPECT_NO_THROW(future_cleanup.get());
}


TEST_F(TCPTest, AcceptorAcceptPassOpenSocketArgFail) {
  // ---------------------------------------------------------------------
  // Main section.
  // ---------------------------------------------------------------------
  Acceptor acceptor(Protocol::IPV4);
  Socket accepted(Protocol::IPV4);

  eventuals::Interrupt interrupt;

  auto e = [&]() {
    return acceptor.Open()
        >> accepted.Open()
        >> acceptor.Bind(TCPTest::kLocalHostIPV4, TCPTest::kAnyPort)
        >> acceptor.Listen(1)
        >> acceptor.Accept(accepted);
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
      ThrowsMessage<std::runtime_error>(StrEq("Passed socket is not closed")));

  // ---------------------------------------------------------------------
  // Cleanup section.
  // ---------------------------------------------------------------------
  eventuals::Interrupt interrupt_cleanup;

  auto e_cleanup = [&]() {
    return acceptor.Close()
        >> accepted.Close();
  };

  auto [future_cleanup, k_cleanup] = PromisifyForTest(e_cleanup());

  k_cleanup.Register(interrupt_cleanup);

  k_cleanup.Start();

  EventLoop::Default().RunUntil(future_cleanup);

  EXPECT_NO_THROW(future_cleanup.get());
}


// NOTE: we need to do separate tests for
// calling interrupt.Trigger() before and after k.Start()
// since Accept operation is asynchronous.
TEST_F(TCPTest, AcceptorAcceptInterruptBeforeStart) {
  // ---------------------------------------------------------------------
  // Setup section.
  // ---------------------------------------------------------------------
  Acceptor acceptor(Protocol::IPV4);
  Socket accepted(Protocol::IPV4);

  eventuals::Interrupt interrupt_setup;

  auto e_setup = [&]() {
    return acceptor.Open()
        >> acceptor.Bind(TCPTest::kLocalHostIPV4, TCPTest::kAnyPort)
        >> acceptor.Listen(1);
  };

  auto [future_setup, k_setup] = PromisifyForTest(e_setup());

  k_setup.Register(interrupt_setup);

  k_setup.Start();

  EventLoop::Default().RunUntil(future_setup);

  EXPECT_NO_THROW(future_setup.get());

  // ---------------------------------------------------------------------
  // Main section.
  // ---------------------------------------------------------------------
  eventuals::Interrupt interrupt;

  auto e = [&]() {
    return acceptor.Accept(accepted);
  };

  auto [future, k] = PromisifyForTest(e());

  k.Register(interrupt);

  interrupt.Trigger();

  k.Start();

  EventLoop::Default().RunUntil(future);

  EXPECT_THROW(future.get(), eventuals::StoppedException);

  // ---------------------------------------------------------------------
  // Cleanup section.
  // ---------------------------------------------------------------------
  eventuals::Interrupt interrupt_cleanup;

  auto e_cleanup = [&]() {
    return acceptor.Close();
  };

  auto [future_cleanup, k_cleanup] = PromisifyForTest(e_cleanup());

  k_cleanup.Register(interrupt_cleanup);

  k_cleanup.Start();

  EventLoop::Default().RunUntil(future_cleanup);

  EXPECT_NO_THROW(future_cleanup.get());
}


// NOTE: we need to do separate tests for
// calling interrupt.Trigger() before and after k.Start()
// since Accept operation is asynchronous.
TEST_F(TCPTest, AcceptorAcceptInterruptAfterStart) {
  // ---------------------------------------------------------------------
  // Setup section.
  // ---------------------------------------------------------------------
  Acceptor acceptor(Protocol::IPV4);
  Socket accepted(Protocol::IPV4);

  eventuals::Interrupt interrupt_setup;

  auto e_setup = [&]() {
    return acceptor.Open()
        >> acceptor.Bind(TCPTest::kLocalHostIPV4, TCPTest::kAnyPort)
        >> acceptor.Listen(1);
  };

  auto [future_setup, k_setup] = PromisifyForTest(e_setup());

  k_setup.Register(interrupt_setup);

  k_setup.Start();

  EventLoop::Default().RunUntil(future_setup);

  EXPECT_NO_THROW(future_setup.get());

  // ---------------------------------------------------------------------
  // Main section.
  // ---------------------------------------------------------------------
  eventuals::Interrupt interrupt;

  auto e = [&]() {
    return acceptor.Accept(accepted);
  };

  auto [future, k] = PromisifyForTest(e());

  k.Register(interrupt);

  k.Start();

  interrupt.Trigger();

  EventLoop::Default().RunUntil(future);

  EXPECT_THROW(future.get(), eventuals::StoppedException);

  // ---------------------------------------------------------------------
  // Cleanup section.
  // ---------------------------------------------------------------------
  eventuals::Interrupt interrupt_cleanup;

  auto e_cleanup = [&]() {
    return acceptor.Close();
  };

  auto [future_cleanup, k_cleanup] = PromisifyForTest(e_cleanup());

  k_cleanup.Register(interrupt_cleanup);

  k_cleanup.Start();

  EventLoop::Default().RunUntil(future_cleanup);

  EXPECT_NO_THROW(future_cleanup.get());
}

} // namespace
} // namespace eventuals::test
