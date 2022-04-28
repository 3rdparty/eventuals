#include "eventuals/grpc/client.h"
#include "eventuals/grpc/server.h"
#include "eventuals/let.h"
#include "eventuals/loop.h"
#include "eventuals/map.h"
#include "eventuals/then.h"
#include "gtest/gtest.h"
#include "test/grpc/helloworld.eventuals.h"
#include "test/grpc/test.h"

namespace eventuals::grpc::test {
namespace {

using helloworld::HelloReply;
using helloworld::HelloRequest;

using helloworld::eventuals::Greeter;

using stout::Borrowable;

class GreeterServiceImpl final : public Greeter::Service<GreeterServiceImpl> {
 public:
  auto SayHello(::grpc::ServerContext* context, HelloRequest&& request) {
    std::string prefix("Hello ");
    HelloReply reply;
    reply.set_message(prefix + request.name());
    return reply;
  }
};

TEST(GreeterServerTest, SayHello) {
  std::string server_address("0.0.0.0:50051");
  GreeterServiceImpl service;

  ServerBuilder builder;

  int port = 0;

  builder.AddListeningPort(
      "0.0.0.0:0",
      ::grpc::InsecureServerCredentials(),
      &port);

  builder.RegisterService(&service);

  auto build = builder.BuildAndStart();

  ASSERT_TRUE(build.status.ok()) << build.status;

  auto server = std::move(build.server);

  ASSERT_TRUE(server);

  Borrowable<CompletionPool> pool;

  Client client(
      "0.0.0.0:" + std::to_string(port),
      ::grpc::InsecureChannelCredentials(),
      pool.Borrow());

  auto call = [&]() {
    return client.Call<Greeter, HelloRequest, HelloReply>("SayHello")
        | Then(Let([](auto& call) {
             HelloRequest request;
             request.set_name("emily");
             return call.Writer().WriteLast(request)
                 | call.Reader().Read()
                 | Map([](auto&& response) {
                      EXPECT_EQ("Hello emily", response.message());
                    })
                 | Loop()
                 | call.Finish();
           }));
  };

  auto status = *call();

  EXPECT_TRUE(status.ok()) << status.error_code()
                           << ": " << status.error_message();
  ;
}

} // namespace
} // namespace eventuals::grpc::test
