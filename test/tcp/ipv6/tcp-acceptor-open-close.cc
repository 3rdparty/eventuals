#include "tcp.h"

namespace eventuals::test {
namespace {

using testing::StrEq;
using testing::ThrowsMessage;

using eventuals::ip::tcp::Acceptor;
using eventuals::ip::tcp::Protocol;

TEST_F(TCPIPV6Test, AcceptorOpenCloseSuccess) {
  Acceptor acceptor(Protocol::IPV6);

  Interrupt interrupt;

  EXPECT_FALSE(acceptor.IsOpen());

  auto e = [&]() {
    return acceptor.Open()
        >> Then([&acceptor]() {
             EXPECT_TRUE(acceptor.IsOpen());
           })
        >> acceptor.Close()
        >> Then([&acceptor]() {
             EXPECT_FALSE(acceptor.IsOpen());
           });
  };

  auto [future, k] = PromisifyForTest(e());

  k.Register(interrupt);

  k.Start();

  EventLoop::Default().RunUntil(future);

  EXPECT_NO_THROW(future.get());
}


TEST_F(TCPIPV6Test, AcceptorCloseClosedFail) {
  Acceptor acceptor(Protocol::IPV6);

  Interrupt interrupt;

  EXPECT_FALSE(acceptor.IsOpen());

  auto e = [&]() {
    return acceptor.Close();
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


// NOTE: we don't need to do separate tests for
// calling interrupt.Trigger() before and after k.Start()
// since Open operation is not asynchronous.
TEST_F(TCPIPV6Test, AcceptorOpenInterrupt) {
  Acceptor acceptor(Protocol::IPV6);

  eventuals::Interrupt interrupt;

  EXPECT_FALSE(acceptor.IsOpen());

  auto e = [&]() {
    return acceptor.Open();
  };

  auto [future, k] = PromisifyForTest(e());

  k.Register(interrupt);

  interrupt.Trigger();

  k.Start();

  EventLoop::Default().RunUntil(future);

  EXPECT_THROW(future.get(), StoppedException);

  EXPECT_FALSE(acceptor.IsOpen());
}


// NOTE: we don't need to do separate tests for
// calling interrupt.Trigger() before and after k.Start()
// since Close operation is not asynchronous.
TEST_F(TCPIPV6Test, AcceptorCloseInterrupt) {
  // ---------------------------------------------------------------------
  // Main test section.
  // ---------------------------------------------------------------------
  Acceptor acceptor(Protocol::IPV6);

  eventuals::Interrupt interrupt;

  EXPECT_FALSE(acceptor.IsOpen());

  auto e = [&]() {
    return acceptor.Open()
        >> Then([&]() {
             EXPECT_TRUE(acceptor.IsOpen());
             interrupt.Trigger();
           })
        >> acceptor.Close();
  };

  auto [future, k] = PromisifyForTest(e());

  k.Register(interrupt);

  k.Start();

  EventLoop::Default().RunUntil(future);

  EXPECT_THROW(future.get(), StoppedException);

  EXPECT_TRUE(acceptor.IsOpen());

  // ---------------------------------------------------------------------
  // Cleanup section.
  // ---------------------------------------------------------------------
  eventuals::Interrupt interrupt_cleanup;

  auto e_cleanup = [&]() {
    return acceptor.Close()
        >> Then([&]() {
             EXPECT_FALSE(acceptor.IsOpen());
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
