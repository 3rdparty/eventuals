#include "examples/protos/keyvaluestore.grpc.pb.h"
#include "gtest/gtest.h"
#include "stout/eventuals/grpc/client.h"
#include "stout/terminal.h"
#include "test/test.h"

using stout::borrowable;

using stout::grpc::Stream;

using stout::eventuals::grpc::Client;
using stout::eventuals::grpc::CompletionPool;

TEST_F(StoutEventualsGrpcTest, ServerUnavailable) {
  borrowable<CompletionPool> pool;

  // NOTE: we use 'getpid()' to create a _unique_ UNIX domain socket
  // path that should never have a server listening on for this test.
  Client client(
      "unix:stout-grpc-test-server-unavailable-" + stringify(getpid()),
      grpc::InsecureChannelCredentials(),
      pool.borrow());

  auto call = [&]() {
    return client.Call<
               Stream<keyvaluestore::Request>,
               Stream<keyvaluestore::Response>>(
               "keyvaluestore.KeyValueStore.GetValues")
        | Client::Handler();
  };

  auto status = *call();

  EXPECT_EQ(grpc::UNAVAILABLE, status.error_code());
}
