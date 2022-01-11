#include "eventuals/grpc/client.h"
#include "eventuals/grpc/server.h"
#include "eventuals/head.h"
#include "eventuals/let.h"
#include "eventuals/terminal.h"
#include "eventuals/then.h"
#include "examples/protos/helloworld.grpc.pb.h"
#include "gtest/gtest.h"
#include "test/test.h"

using helloworld::Greeter;
using helloworld::HelloReply;
using helloworld::HelloRequest;

using stout::Borrowable;

using eventuals::Head;
using eventuals::Let;
using eventuals::Terminate;
using eventuals::Then;

using eventuals::grpc::Client;
using eventuals::grpc::CompletionPool;
using eventuals::grpc::Server;
using eventuals::grpc::ServerBuilder;

TEST_F(EventualsGrpcTest, ServerDeathTest) {
  // NOTE: need pipes to get the server's port, this also helps
  // synchronize when the server is ready to have the client connect.
  int pipefds[2];

  ASSERT_NE(-1, pipe(pipefds));

  auto wait_for_port = [&]() {
    int port;
    CHECK_GT(read(pipefds[0], &port, sizeof(int)), 0);
    return port;
  };

  auto send_port = [&](int port) {
    CHECK_GT(write(pipefds[1], &port, sizeof(port)), 0);
  };

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
          | Then([](auto&& call) {
               exit(1);
             });
    };

    auto [future, k] = Terminate(serve());

    k.Start();

    send_port(port);

    future.get();
  };

  std::thread thread([&]() {
    ASSERT_DEATH(server(), "");
  });

  // NOTE: we detach the thread so that there isn't a race with the
  // thread completing and attempting to run it's destructor which
  // will call 'std::terminate()' if we haven't yet called
  // 'join()'. We know it's safe to detach because the thread (which
  // acts as the parent process for the server) can destruct itself
  // whenever it wants because it doesn't depend on anything from the
  // test which might have been destructed before it destructs.
  thread.detach();

  int port = wait_for_port();

  Borrowable<CompletionPool> pool;

  Client client(
      "0.0.0.0:" + stringify(port),
      grpc::InsecureChannelCredentials(),
      pool.Borrow());

  auto call = [&]() {
    return client.Call<Greeter, HelloRequest, HelloReply>("SayHello")
        | Then(Let([](auto& call) {
             HelloRequest request;
             request.set_name("emily");
             return call.Writer().WriteLast(request)
                 | call.Finish();
           }));
  };

  auto status = *call();

  EXPECT_EQ(grpc::UNAVAILABLE, status.error_code());

  close(pipefds[0]);
  close(pipefds[1]);
}
