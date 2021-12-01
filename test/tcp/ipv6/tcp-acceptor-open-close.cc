#include "tcp.h"
// tcp.h must be included before anything else on Windows
// due to SSL redefinitions done by wincrypt.h.
#include "eventuals/terminal.h"
#include "eventuals/then.h"

namespace eventuals::test {
namespace {

using eventuals::ip::tcp::Acceptor;
using eventuals::ip::tcp::Protocol;

TEST_F(TCPIPV6Test, AcceptorOpenCloseSuccess) {
  Acceptor acceptor;

  eventuals::Interrupt interrupt;

  EXPECT_FALSE(acceptor.IsOpen());

  auto e = acceptor.Open(Protocol::IPV6)
      >> Then([&acceptor]() {
             EXPECT_TRUE(acceptor.IsOpen());
           })
      >> acceptor.Close()
      >> Then([&acceptor]() {
             EXPECT_FALSE(acceptor.IsOpen());
           });

  auto [future, k] = PromisifyForTest(std::move(e));

  k.Register(interrupt);

  k.Start();

  EventLoop::Default().RunUntil(future);

  EXPECT_NO_THROW(future.get());
}

} // namespace
} // namespace eventuals::test