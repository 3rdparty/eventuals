#include "tcp.h"

namespace eventuals::test {
namespace {

using testing::StrEq;
using testing::ThrowsMessage;

using eventuals::ip::tcp::Acceptor;
using eventuals::ip::tcp::Protocol;

TEST_F(TCPIPV6Test, AcceptorListenSuccess) {
  Acceptor acceptor(Protocol::IPV6);

  eventuals::Interrupt interrupt;

  auto e = [&]() {
    return acceptor.Open()
        >> acceptor.Bind(TCPIPV6Test::kAnyIPV6, TCPIPV6Test::kAnyPort)
        >> acceptor.Listen(1)
        >> acceptor.Close();
  };

  auto [future, k] = PromisifyForTest(e());

  k.Register(interrupt);

  k.Start();

  EventLoop::Default().RunUntil(future);

  EXPECT_NO_THROW(future.get());
}


TEST_F(TCPIPV6Test, AcceptorListenClosedFail) {
  Acceptor acceptor(Protocol::IPV6);

  eventuals::Interrupt interrupt;

  auto e = [&]() {
    return acceptor.Listen(1);
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


TEST_F(TCPIPV6Test, AcceptorListenTwiceFail) {
  // ---------------------------------------------------------------------
  // Main test section.
  // ---------------------------------------------------------------------
  Acceptor acceptor(Protocol::IPV6);

  eventuals::Interrupt interrupt;

  auto e = [&]() {
    return acceptor.Open()
        >> acceptor.Bind(TCPIPV6Test::kAnyIPV6, TCPIPV6Test::kAnyPort)
        >> acceptor.Listen(1)
        >> acceptor.Listen(1);
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
      ThrowsMessage<std::runtime_error>(
          StrEq(
              "Acceptor is already listening")));

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


// NOTE: we don't need to do separate tests for
// calling interrupt.Trigger() before and after k.Start()
// since Listen operation is not asynchronous.
TEST_F(TCPIPV6Test, AcceptorListenInterrupt) {
  // ---------------------------------------------------------------------
  // Main test section.
  // ---------------------------------------------------------------------
  Acceptor acceptor(Protocol::IPV6);

  eventuals::Interrupt interrupt;

  auto e = [&]() {
    return acceptor.Open()
        >> acceptor.Bind(TCPIPV6Test::kAnyIPV6, TCPIPV6Test::kAnyPort)
        >> Then([&interrupt]() {
             interrupt.Trigger();
           })
        >> acceptor.Listen(1);
  };

  auto [future, k] = PromisifyForTest(e());

  k.Register(interrupt);

  k.Start();

  EventLoop::Default().RunUntil(future);

  EXPECT_THROW(future.get(), StoppedException);

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
