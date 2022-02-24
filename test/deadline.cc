#include "eventuals/closure.h"
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

using eventuals::Closure;
using eventuals::Head;
using eventuals::Let;
using eventuals::Terminate;
using eventuals::Then;

using eventuals::grpc::Client;
using eventuals::grpc::CompletionPool;
using eventuals::grpc::Server;
using eventuals::grpc::ServerBuilder;

TEST_F(EventualsGrpcTest, Deadline) {
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

  Borrowable<CompletionPool> pool;

  Client client(
      "0.0.0.0:" + std::to_string(port),
      grpc::InsecureChannelCredentials(),
      pool.Borrow());

  auto call = [&]() {
    return client.Context()
        | Then([&](auto* context) {
             auto now = std::chrono::system_clock::now();
             context->set_deadline(now + std::chrono::milliseconds(100));

             return client.Call<Greeter, HelloRequest, HelloReply>(
                        "SayHello",
                        context)
                 | Then(Let([](auto& call) {
                      HelloRequest request;
                      request.set_name("emily");
                      return call.Writer().WriteLast(request)
                          | call.Finish();
                    }));
           });
  };

  auto status = *call();

  ASSERT_EQ(grpc::DEADLINE_EXCEEDED, status.error_code());

  ASSERT_TRUE(cancelled.get());
}
