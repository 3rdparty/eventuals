#include "examples/protos/helloworld.grpc.pb.h"
#include "gtest/gtest.h"
#include "stout/eventual.h"
#include "stout/grpc/client.h"
#include "stout/grpc/server.h"
#include "stout/head.h"
#include "stout/let.h"
#include "stout/loop.h"
#include "stout/terminal.h"
#include "stout/then.h"
#include "test/test.h"

using helloworld::Greeter;
using helloworld::HelloReply;
using helloworld::HelloRequest;

using stout::Borrowable;

using stout::eventuals::Eventual;
using stout::eventuals::Head;
using stout::eventuals::Let;
using stout::eventuals::Loop;
using stout::eventuals::Terminate;
using stout::eventuals::Then;

using stout::eventuals::grpc::Client;
using stout::eventuals::grpc::CompletionPool;
using stout::eventuals::grpc::Server;
using stout::eventuals::grpc::ServerBuilder;

TEST_F(StoutGrpcTest, ClientDeathTest) {
  // NOTE: need pipes so that (1) the client can tell us when it's
  // forked so we know we can start the server because we can't call
  // into grpc before we've forked (see
  // https://github.com/grpc/grpc/issues/14055) and (2) the server can
  // send the client it's port.
  struct {
    int fork[2];
    int port[2];
  } pipes;

  ASSERT_NE(-1, pipe(pipes.fork));
  ASSERT_NE(-1, pipe(pipes.port));

  auto wait_for_fork = [&]() {
    int _;
    CHECK(read(pipes.fork[0], &_, sizeof(int)) > 0);
  };

  auto notify_forked = [&]() {
    int _ = 1;
    CHECK(write(pipes.fork[1], &_, sizeof(int)) > 0);
  };

  auto wait_for_port = [&]() {
    int port;
    CHECK(read(pipes.port[0], &port, sizeof(int)) > 0);
    return port;
  };

  auto send_port = [&](int port) {
    CHECK(write(pipes.port[1], &port, sizeof(port)) > 0);
  };

  auto client = [&]() {
    notify_forked();

    int port = wait_for_port();

    Borrowable<CompletionPool> pool;

    Client client(
        "0.0.0.0:" + stringify(port),
        grpc::InsecureChannelCredentials(),
        pool.Borrow());

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

  wait_for_fork();

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
        | Then(Let([](auto& call) {
             return call.WaitForDone();
           }));
  };

  auto [cancelled, k] = Terminate(serve());

  k.Start();

  send_port(port);

  EXPECT_TRUE(cancelled.get());

  thread.join();

  close(pipes.fork[0]);
  close(pipes.fork[1]);
  close(pipes.port[0]);
  close(pipes.port[1]);
}
