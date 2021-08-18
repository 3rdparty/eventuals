#include "examples/protos/helloworld.grpc.pb.h"
#include "gtest/gtest.h"
#include "stout/eventuals/grpc/client.h"
#include "stout/eventuals/grpc/server.h"
#include "stout/eventuals/head.h"
#include "stout/terminal.h"
#include "stout/then.h"
#include "test/test.h"

using helloworld::Greeter;
using helloworld::HelloReply;
using helloworld::HelloRequest;

using stout::borrowable;

using stout::grpc::Stream;

using stout::eventuals::Head;
using stout::eventuals::Then;

using stout::eventuals::grpc::Client;
using stout::eventuals::grpc::CompletionPool;
using stout::eventuals::grpc::Server;
using stout::eventuals::grpc::ServerBuilder;

TEST_F(StoutEventualsGrpcTest, ServerDeathTest) {
  // NOTE: need pipes to get the server's port, this also helps
  // synchronize when the server is ready to have the client connect.
  int pipefd[2];

  auto error = pipe(pipefd);

  ASSERT_NE(-1, error);

  auto server = [&]() {
    ServerBuilder builder;

    int port = 0;

    builder.AddListeningPort(
        "0.0.0.0:0",
        grpc::InsecureServerCredentials(),
        &port);

    auto build = builder.BuildAndStart();

    ASSERT_TRUE(build.status.ok());

    auto server = std::move(build.server);

    ASSERT_TRUE(server);

    auto serve = [&]() {
      return server->Accept<Greeter, HelloRequest, HelloReply>("SayHello")
          | Head()
          | Then([](auto&& context) {
               return Server::Handler(std::move(context))
                   .ready([&](auto& call) {
                     exit(1);
                   });
             });
    };

    auto error = write(pipefd[1], &port, sizeof(int));

    ASSERT_LT(0, error);

    *serve();
  };

  std::thread thread([&]() {
    ASSERT_DEATH(server(), "");
  });

  int port = 0;

  error = read(pipefd[0], &port, sizeof(int));

  ASSERT_LT(0, error);

  borrowable<CompletionPool> pool;

  Client client(
      "0.0.0.0:" + stringify(port),
      grpc::InsecureChannelCredentials(),
      pool.borrow());

  auto call = [&]() {
    return client.Call<Greeter, HelloRequest, HelloReply>("SayHello")
        | (Client::Handler()
               .ready([](auto& call) {
                 HelloRequest request;
                 request.set_name("emily");
                 call.WriteLast(request);
               }));
  };

  auto status = *call();

  EXPECT_EQ(grpc::UNAVAILABLE, status.error_code());

  thread.join();

  close(pipefd[0]);
  close(pipefd[1]);
}
