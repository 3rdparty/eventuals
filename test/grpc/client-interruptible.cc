#include <sys/socket.h>
#include <sys/un.h>

#include "eventuals/grpc/client.hh"
#include "eventuals/grpc/completion-thread-pool.hh"
#include "eventuals/then.hh"
#include "examples/protos/helloworld.grpc.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "stout/tests/utils.h"
#include "test/grpc/test.hh"
#include "test/promisify-for-test.hh"

namespace eventuals::grpc::test {
namespace {

using helloworld::Greeter;
using helloworld::HelloReply;
using helloworld::HelloRequest;

using testing::MockFunction;

using stout::Borrowable;

// Create test fixture that allows us to create a (temporary) domain socket for
// use in our test.
class ClientInterruptibleTest : public TemporaryDirectoryTest {
 protected:
  std::string domain_socket_path() {
    auto path = test_directory_path() / std::filesystem::path("socket.sock");
    return path.string();
  }
};


TEST_F(ClientInterruptibleTest, Interrupt) {
  // We create a mock function that represents that the client successfully
  // managed to call the server. This will however never happen as we don't
  // give it a server to call. Consequently, we EXPECT_CALL this to zero times.
  MockFunction<void()> client_call_success;

  EXPECT_CALL(client_call_success, Call())
      .Times(0);

  // Low key, create a domain socket and start listening on it.
  sockaddr_un addr;
  addr.sun_family = AF_UNIX;
  strcpy(addr.sun_path, domain_socket_path().c_str());

  int server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
  ASSERT_GE(server_fd, 0);
  ASSERT_EQ(bind(server_fd, (sockaddr*) (&addr), sizeof(addr)), 0);
  ASSERT_EQ(listen(server_fd, 100), 0);

  // Set up the client and prepare to connect to the domain socket and attempt
  // to call the service.
  Borrowable<ClientCompletionThreadPool> pool;
  Client client(
      "unix:" + domain_socket_path(),
      ::grpc::InsecureChannelCredentials(),
      pool.Borrow());

  auto client_call = [&]() {
    // This should block on `client.Call` and the Then-clause should never be
    // reached.
    return client.Call<Greeter, HelloRequest, HelloReply>("SayHello")
        >> Then([&](ClientCall<HelloRequest, HelloReply>&& call) {
             client_call_success.Call();
           });
  };

  // Create the background job.
  auto [future, k] = PromisifyForTest(client_call());

  // Register the interrupt that we will later trigger.
  Interrupt interrupt;
  k.Register(interrupt);

  // Start the job in the background.
  k.Start();

  // Wait for the grpc client to attempt to connect on the socket and accept
  // the connection. The client should now be talking grpc to the very quiet
  // socket.
  socklen_t addr_len = sizeof(addr);
  ASSERT_GT(
      accept(server_fd, reinterpret_cast<sockaddr*>(&addr), &addr_len),
      -1);

  // Trigger the interrupt, cancelling the grpc call.
  interrupt.Trigger();
  EXPECT_THROW(future.get(), std::runtime_error);
}

} // namespace
} // namespace eventuals::grpc::test
