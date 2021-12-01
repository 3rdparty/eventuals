#include "tcp.h"
// tcp.h must be included before anything else on Windows
// due to SSL redefinitions done by wincrypt.h.
#include "eventuals/terminal.h"
#include "eventuals/then.h"

namespace eventuals::test {
namespace {

using eventuals::ip::tcp::Acceptor;
using eventuals::ip::tcp::Protocol;
using eventuals::ip::tcp::Socket;

TEST_F(TCPIPV6Test, SocketOpenCloseSuccess) {
  Socket socket;

  eventuals::Interrupt interrupt;

  EXPECT_FALSE(socket.IsOpen());

  auto e = [&]() {
    return socket.Open(Protocol::IPV6)
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

} // namespace
} // namespace eventuals::test