#include "eventuals/grpc/client.hh"
#include "eventuals/grpc/server.hh"
#include "eventuals/let.hh"
#include "eventuals/promisify.hh"
#include "examples/protos/helloworld.grpc.pb.h"
#include "gtest/gtest.h"
#include "test/grpc/test.hh"

namespace eventuals::grpc::test {
namespace {

using helloworld::Greeter;
using helloworld::HelloReply;
using helloworld::HelloRequest;

using stout::Borrowable;

TEST(UnimplementedTest, ClientCallsUnimplementedServerMethod) {
  ServerBuilder builder;

  int port = 0;

  builder.AddListeningPort(
      "0.0.0.0:0",
      ::grpc::InsecureServerCredentials(),
      &port);

  auto build = builder.BuildAndStart();

  ASSERT_TRUE(build.status.ok()) << build.status;

  auto server = std::move(build.server);

  ASSERT_TRUE(server);

  Borrowable<ClientCompletionThreadPool> pool;

  Client client(
      "0.0.0.0:" + std::to_string(port),
      ::grpc::InsecureChannelCredentials(),
      pool.Borrow());

  auto call = [&]() {
    return client.Call<Greeter, HelloRequest, HelloReply>("SayHello")
        >> Then(Let([](auto& call) {
             return call.Finish();
           }));
  };

  auto status = *call();

  ASSERT_EQ(::grpc::UNIMPLEMENTED, status.error_code());
}

} // namespace
} // namespace eventuals::grpc::test
