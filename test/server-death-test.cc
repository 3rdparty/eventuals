#include <filesystem>

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
  int pipefds[2]; // 'port[0]' is for reading, 'port[1]' for writing.

  ASSERT_NE(-1, pipe(pipefds));

  auto WaitForPort = [&]() {
    int port;
    CHECK_GT(read(pipefds[0], &port, sizeof(int)), 0);
    return port;
  };

  // We use a thread to fork/exec the 'death-server' so that we can
  // simultaneously run and wait for the server to die while also
  // running the client.
  std::thread thread([&]() {
    std::string path = GetRunfilePathFor("death-server").string();

    std::string pipe = std::to_string(pipefds[1]);

    // NOTE: doing a 'fork()' when a parent has multiple threads is
    // wrought with potential issues because the child only gets a
    // single one of those threads (the one that called 'fork()') so
    // we ensure here that there is only the single extra thread.
    ASSERT_EQ(GetThreadCount(), 2);

    ASSERT_DEATH(
        [&]() {
          // Conventional wisdom is to do the least amount possible
          // after a 'fork()', ideally just an 'exec*()', and that's
          // what we're doing because when we tried to do more the
          // tests were flaky likely do to some library that doesn't
          // properly work after doing a 'fork()'.
          execl(path.c_str(), path.c_str(), pipe.c_str(), nullptr);

          // NOTE: if 'execve()' failed then this lamdba will return
          // and gtest will see consider this "death" to be a failure.
        }(),
        // NOTE: to avoid false positives we check for a death with
        // the string 'accepted' printed to stderr.
        "accepted");
  });

  // NOTE: we detach the thread so that there isn't a race with the
  // thread completing and attempting to run it's destructor which
  // will call 'std::terminate()' if we haven't yet called
  // 'join()'. We know it's safe to detach because the thread (which
  // acts as the parent process for the server) can destruct itself
  // whenever it wants because it doesn't depend on anything from the
  // test which might have been destructed before it destructs.
  thread.detach();

  int port = WaitForPort();

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
                 | call.Finish();
           }));
  };

  auto status = *call();

  EXPECT_EQ(grpc::UNAVAILABLE, status.error_code());

  close(pipefds[0]);
  close(pipefds[1]);
}
