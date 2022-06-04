#include <sys/wait.h>

#include <cstdlib>
#include <filesystem>
#include <thread>

#include "eventuals/finally.h"
#include "eventuals/grpc/client.h"
#include "eventuals/grpc/server.h"
#include "eventuals/let.h"
#include "eventuals/terminal.h"
#include "eventuals/then.h"
#include "examples/protos/helloworld.grpc.pb.h"
#include "gtest/gtest.h"
#include "test/grpc/death-constants.h"
#include "test/grpc/test.h"

namespace eventuals::grpc::test {
namespace {

using helloworld::Greeter;
using helloworld::HelloReply;
using helloworld::HelloRequest;

using stout::Borrowable;

// Tests that the client receives a ::grpc::UNAVAILABLE status if the server
// dies without cleanly calling call.Finish().
TEST(ServerDeathTest, ClientReceivesUnavailable) {
  // NOTE: need pipes to get the server's port, this also helps
  // synchronize when the server is ready to have the client connect.
  int pipefds[2]; // 'port[0]' is for reading, 'port[1]' for writing.

  ASSERT_NE(-1, pipe(pipefds));

  auto WaitForPort = [&]() {
    int port;
    CHECK_GT(read(pipefds[0], &port, sizeof(int)), 0);
    return port;
  };

  // Launch the server before creating the client. Run the server in a
  // subprocess so that it can run in parallel with this test.
  std::thread thread([&]() {
    const std::string path = GetRunfilePathFor("death-server").string();
    const std::string pipe = std::to_string(pipefds[1]);
    const std::string command = path + " " + pipe;
    // Block on the server until it returns a known return value.
    const int result = std::system(command.c_str());
    // Issue(#329): WEXITSTATUS is Posix-specific. Figure out the correct way
    // to get the application's return code on windows.
    EXPECT_EQ(kProcessIntentionalExitCode, WEXITSTATUS(result));
  });

  int port = WaitForPort();

  Borrowable<CompletionPool> pool;

  Client client(
      "0.0.0.0:" + std::to_string(port),
      ::grpc::InsecureChannelCredentials(),
      pool.Borrow());

  auto call = [&]() {
    return client.Call<Greeter, HelloRequest, HelloReply>("SayHello")
        | Then(Let([](ClientCall<HelloRequest, HelloReply>& call) {
             HelloRequest request;
             request.set_name("emily");
             return call.Writer().WriteLast(request)
                 | Finally(
                        [&](expected<void, std::exception_ptr>&&) {
                          return call.Finish();
                        });
           }));
  };

  const ::grpc::Status status = *call();

  EXPECT_EQ(::grpc::UNAVAILABLE, status.error_code());
  thread.join();

  close(pipefds[0]);
  close(pipefds[1]);
}

} // namespace
} // namespace eventuals::grpc::test
