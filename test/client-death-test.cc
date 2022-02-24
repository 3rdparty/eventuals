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

using eventuals::Head;
using eventuals::Let;
using eventuals::Terminate;
using eventuals::Then;

using eventuals::grpc::Server;
using eventuals::grpc::ServerBuilder;

TEST_F(EventualsGrpcTest, ClientDeathTest) {
  // NOTE: need pipes so that (1) the client can tell us when it's
  // forked so we know we can start the server because we can't call
  // into grpc before we've forked (see
  // https://github.com/grpc/grpc/issues/14055) and (2) the server can
  // send the client it's port.
  struct {
    int fork[2]; // 'fork[0]' is for reading, 'fork[1]' for writing.
    int port[2]; // 'port[0]' is for reading, 'port[1]' for writing.
  } pipes;

  ASSERT_NE(-1, pipe(pipes.fork));
  ASSERT_NE(-1, pipe(pipes.port));

  auto WaitForFork = [&]() {
    int _;
    CHECK_GT(read(pipes.fork[0], &_, sizeof(int)), 0);
  };

  auto SendPort = [&](int port) {
    CHECK_GT(write(pipes.port[1], &port, sizeof(port)), 0);
  };

  // We use a thread to fork/exec the 'death-client' so that we can
  // simultaneously run and wait for the client to die while also
  // running the server.
  std::thread thread([&]() {
    std::string path = GetRunfilePathFor("death-client").string();

    std::string pipe_fork = std::to_string(pipes.fork[1]);
    std::string pipe_port = std::to_string(pipes.port[0]);

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
          execl(
              path.c_str(),
              path.c_str(),
              pipe_fork.c_str(),
              pipe_port.c_str(),
              nullptr);

          // NOTE: if 'execve()' failed then this lamdba will return
          // and gtest will see consider this "death" to be a failure.
        }(),
        // NOTE: to avoid false positives we check for a death with
        // the string 'connected' printed to stderr.
        "connected");
  });

  // NOTE: we detach the thread so that there isn't a race with the
  // thread completing and attempting to run it's destructor which
  // will call 'std::terminate()' if we haven't yet called
  // 'join()'. We know it's safe to detach because the thread (which
  // acts as the parent process for the client) can destruct itself
  // whenever it wants because it doesn't depend on anything from the
  // test which might have been destructed before it destructs.
  thread.detach();

  // NOTE: need to wait to call into gRPC till _after_ we've forked
  // (see comment at top of test for more details).
  WaitForFork();

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

  // NOTE: sending this _after_ we start the eventual so that we're
  // ready to accept clients!
  SendPort(port);

  EXPECT_TRUE(cancelled.get());

  close(pipes.fork[0]);
  close(pipes.fork[1]);
  close(pipes.port[0]);
  close(pipes.port[1]);
}
