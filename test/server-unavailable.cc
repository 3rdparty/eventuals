#include "eventuals/grpc/client.h"
#include "eventuals/terminal.h"
#include "examples/protos/keyvaluestore.grpc.pb.h"
#include "gtest/gtest.h"
#include "test/test.h"

using stout::Borrowable;

using eventuals::grpc::Client;
using eventuals::grpc::CompletionPool;
using eventuals::grpc::Stream;

TEST_F(EventualsGrpcTest, ServerUnavailable) {
  Borrowable<CompletionPool> pool;

  // NOTE: we use 'getpid()' to create a _unique_ UNIX domain socket
  // path that should never have a server listening on for this test.
  Client client(
      "unix:eventuals-grpc-test-server-unavailable-" + stringify(getpid()),
      grpc::InsecureChannelCredentials(),
      pool.Borrow());

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
