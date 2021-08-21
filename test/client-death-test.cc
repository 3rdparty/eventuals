#include "examples/protos/helloworld.grpc.pb.h"
#include "gtest/gtest.h"
#include "stout/grpc/client.h"
#include "stout/grpc/server.h"
#include "stout/head.h"
#include "stout/terminal.h"
#include "stout/then.h"
#include "test/test.h"

using helloworld::Greeter;
using helloworld::HelloReply;
using helloworld::HelloRequest;

using stout::borrowable;

using stout::eventuals::Head;
using stout::eventuals::Terminate;
using stout::eventuals::Then;

using stout::eventuals::grpc::Client;
using stout::eventuals::grpc::CompletionPool;
using stout::eventuals::grpc::Server;
using stout::eventuals::grpc::ServerBuilder;

TEST_F(StoutGrpcTest, ClientDeathTest) {
  // NOTE: need pipes so the client can get the port of the server and
  // we need to start the server after we've forked because grpc isn't
  // fork friendly, see: https://github.com/grpc/grpc/issues/14055
  int pipefd[2];

  auto error = pipe(pipefd);

  ASSERT_NE(-1, error);

  auto client = [&]() {
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
                 .ready([](auto&) {
                   exit(1);
                 }));
    };

    *call();
  };

  std::thread thread([&]() {
    ASSERT_DEATH(client(), "");
  });

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
             return Server::Handler(std::move(context));
           });
  };

  auto [cancelled, k] = Terminate(serve());

  k.Start();

  error = write(pipefd[1], &port, sizeof(int));

  ASSERT_LT(0, error);

  EXPECT_TRUE(cancelled.get());

  thread.join();
}
