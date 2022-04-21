#include "eventuals/poll.h"

#include "event-loop-test.h"
#include "eventuals/do-all.h"
#include "eventuals/event-loop.h"
#include "eventuals/loop.h"
#include "eventuals/map.h"
#include "eventuals/reduce.h"
#include "eventuals/terminal.h"
#include "eventuals/then.h"
#include "eventuals/type-traits.h"
#include "eventuals/unpack.h"
#include "eventuals/until.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace eventuals::test {
namespace {

class PollTest : public EventLoopTest {};

TEST_F(PollTest, Succeed) {
  // NOTE: using uv_os_socket_t so we can run this test on *nix (e.g.,
  // macOS, Linux), and Windows.
  uv_os_sock_t sockets[2];
  ASSERT_EQ(0, uv_socketpair(SOCK_STREAM, IPPROTO_IP, sockets, 0, 0));

  // Helper for closing sockets which differs on Windows.
  auto Close = [](uv_os_sock_t socket) {
#ifdef _WIN32
    EXPECT_EQ(0, closesocket(socket));
#else
    EXPECT_EQ(0, close(socket));
#endif
  };

  uv_os_sock_t server = sockets[0];
  uv_os_sock_t client = sockets[1];

  static const std::string data1 = "Hello ";
  static const std::string data2 = "World!";

  auto e = [&]() {
    return DoAll(
               // Server:
               Poll(server, PollEvents::Readable) | Reduce(
                   /* data = */ std::string(),
                   [&](auto& data) {
                     return Then([&](PollEvents events) {
                       EXPECT_EQ(
                           events & PollEvents::Readable,
                           PollEvents::Readable);
                       char buffer[1024];
                       int size = read(server, buffer, 1024);
                       if (size > 0) {
                         data += std::string(buffer, size);
                         return /* continue = */ true;
                       } else {
                         // Reached EOF!
                         return /* continue = */ false;
                       }
                     });
                   }),
               // Client:
               Poll(client, PollEvents::Writable)
                   | Map([&, first = true](PollEvents events) mutable {
                       if (first) {
                         first = false;
                         EXPECT_EQ(PollEvents::Writable, events);
                         EXPECT_EQ(
                             data1.size(),
                             write(client, data1.data(), data1.size()));
                         return /* done = */ false;
                       } else {
                         EXPECT_EQ(PollEvents::Writable, events);
                         EXPECT_EQ(
                             data2.size(),
                             write(client, data2.data(), data2.size()));
                         return /* done = */ true;
                       }
                     })
                   | Until([](bool done) {
                       return done;
                     })
                   | Loop()
                   | Then([&]() {
                       Close(client);
                     }))
        | Then(Unpack([](std::string&& data, std::monostate) {
             return std::move(data);
           }));
  };

  auto [future, k] = Terminate(e());

  k.Start();

  EventLoop::Default().RunUntil(future);

  EXPECT_EQ(data1 + data2, future.get());
}

} // namespace
} // namespace eventuals::test
