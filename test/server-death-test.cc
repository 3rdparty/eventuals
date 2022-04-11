#include <cstdlib>
#include <filesystem>
#include <iostream>

#include "eventuals/catch.h"
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

using eventuals::Catch;
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
  int pipefds[2]; // 'port[0]' is for reading, 'port[1]' for writing.

  ASSERT_NE(-1, pipe(pipefds));

  auto WaitForPort = [&]() {
    int port;
    CHECK_GT(read(pipefds[0], &port, sizeof(int)), 0);
    return port;
  };

  // Launch the server before creating the client. Run the server in a
  // subprocess so that it can run in parallel with this test without requiring
  // threads. Capture the server's output to a file for debugging.
  const std::string path = GetRunfilePathFor("death-server").string();
  const std::string pipe = std::to_string(pipefds[1]);
  const std::string command = path + " " + pipe + " &";

  std::cout << "Running server with command: " << command << std::endl;
  CHECK_EQ(0, std::system(command.c_str()))
      << "Failed to run command " << command;

  std::cout << "Waiting for server." << std::endl;
  int port = WaitForPort();
  std::cout << "Server active on port " << port << "." << std::endl;

  Borrowable<CompletionPool> pool;

  Client client(
      "0.0.0.0:" + std::to_string(port),
      grpc::InsecureChannelCredentials(),
      pool.Borrow());

  auto call = [&]() {
    return client.Call<Greeter, HelloRequest, HelloReply>("SayHello")
        | Then(Let([](auto& call) {
             HelloRequest request;
             request.set_name("emily");
             return call.Writer().WriteLast(request)
                 | call.Reader().Read()
                 | Head()
                 | Then([](HelloReply&& response) {
                      std::cout << "Got reply" << response.DebugString();
                      EXPECT_EQ("Hello emily", response.message());
                    })
                 // Handle the exception thrown when calling Head() on an
                 // empty stream.
                 // TODO(alexmc, benh): Can we handle 0 to 1 responses more
                 // cleanly by using something like Map()?
                 | Catch()
                       .raised<std::runtime_error>([](auto&& error) {
                         std::cout << "Got error: " << error.what() << std::endl;
                       })
                 | call.Finish();
           }));
  };

  // The first call succeeds.
  {
    std::cout << "Sending first call." << std::endl;
    auto status = *call();
    EXPECT_EQ(grpc::OK, status.error_code());
  }
  // The server exits on the second call.
  {
    std::cout << "Sending second call." << std::endl;
    auto status = *call();
    EXPECT_EQ(grpc::UNAVAILABLE, status.error_code());
  }

  close(pipefds[0]);
  close(pipefds[1]);
}
