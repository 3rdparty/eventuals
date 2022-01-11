#include "eventuals/eventual.h"
#include "eventuals/grpc/client.h"
#include "eventuals/grpc/server.h"
#include "eventuals/head.h"
#include "eventuals/let.h"
#include "eventuals/loop.h"
#include "eventuals/terminal.h"
#include "eventuals/then.h"
#include "examples/protos/helloworld.grpc.pb.h"
#include "gtest/gtest.h"
#include "test/test.h"

using helloworld::Greeter;
using helloworld::HelloReply;
using helloworld::HelloRequest;

using stout::Borrowable;

using eventuals::Eventual;
using eventuals::Head;
using eventuals::Let;
using eventuals::Loop;
using eventuals::Terminate;
using eventuals::Then;

using eventuals::grpc::Client;
using eventuals::grpc::CompletionPool;
using eventuals::grpc::Server;
using eventuals::grpc::ServerBuilder;

TEST_F(EventualsGrpcTest, ClientDeathTest) {
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
    CHECK_GT(read(pipes.fork[0], &_, sizeof(int)), 0);
  };

  auto notify_forked = [&]() {
    int _ = 1;
    CHECK_GT(write(pipes.fork[1], &_, sizeof(int)), 0);
  };

  auto wait_for_port = [&]() {
    int port;
    CHECK_GT(read(pipes.port[0], &port, sizeof(int)), 0);
    return port;
  };

  auto send_port = [&](int port) {
    CHECK_GT(write(pipes.port[1], &port, sizeof(port)), 0);
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
          | Then([](auto&& call) {
               exit(1);
             });
    };

    *call();
  };

  std::thread thread([&]() {
    ASSERT_DEATH(client(), "");
  });

  // NOTE: we detach the thread so that there isn't a race with the
  // thread completing and attempting to run it's destructor which
  // will call 'std::terminate()' if we haven't yet called
  // 'join()'. We know it's safe to detach because the thread (which
  // acts as the parent process for the client) can destruct itself
  // whenever it wants because it doesn't depend on anything from the
  // test which might have been destructed before it destructs.
  thread.detach();

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

  close(pipes.fork[0]);
  close(pipes.fork[1]);
  close(pipes.port[0]);
  close(pipes.port[1]);
}
