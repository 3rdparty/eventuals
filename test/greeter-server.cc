#include "eventuals/grpc/client.h"
#include "eventuals/grpc/server.h"
#include "eventuals/sequence.h"
#include "gtest/gtest.h"
#include "test/helloworld.eventuals.h"
#include "test/test.h"

using stout::Borrowable;

using eventuals::Sequence;

using eventuals::grpc::Client;
using eventuals::grpc::CompletionPool;
using eventuals::grpc::ServerBuilder;

using helloworld::HelloReply;
using helloworld::HelloRequest;

using helloworld::eventuals::Greeter;

class GreeterServiceImpl final : public Greeter::Service<GreeterServiceImpl> {
 public:
  auto SayHello(::grpc::ServerContext* context, HelloRequest&& request) {
    std::string prefix("Hello ");
    HelloReply reply;
    reply.set_message(prefix + request.name());
    return reply;
  }
};

TEST_F(EventualsGrpcTest, Greeter) {
  std::string server_address("0.0.0.0:50051");
  GreeterServiceImpl service;

  ServerBuilder builder;

  int port = 0;

  builder.AddListeningPort(
      "0.0.0.0:0",
      grpc::InsecureServerCredentials(),
      &port);

  builder.RegisterService(&service);

  auto build = builder.BuildAndStart();

  ASSERT_TRUE(build.status.ok());

  auto server = std::move(build.server);

  ASSERT_TRUE(server);

  Borrowable<CompletionPool> pool;

  Client client(
      "0.0.0.0:" + stringify(port),
      grpc::InsecureChannelCredentials(),
      pool.Borrow());

  auto call = [&]() {
    return client.Call<Greeter, HelloRequest, HelloReply>("SayHello")
        | (Client::Handler()
               .ready([](auto& call) {
                 HelloRequest request;
                 request.set_name("emily");
                 call.WriteLast(request);
               })
               .body(Sequence()
                         .Once([](auto& call, auto&& response) {
                           EXPECT_EQ("Hello emily", response->message());
                         })
                         .Once([](auto& call, auto&& response) {
                           EXPECT_FALSE(response);
                         })));
  };

  auto status = *call();

  EXPECT_TRUE(status.ok());
}
