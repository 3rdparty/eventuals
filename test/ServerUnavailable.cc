#include "gtest/gtest.h"

#include "stout/grpc/client.h"
#include "stout/grpc/server.h"

// https://github.com/grpc/grpc/blob/master/examples/protos/keyvaluestore.proto
#include "examples/protos/keyvaluestore.grpc.pb.h"

#include "stringify.h"

using stout::Notification;

using stout::grpc::Client;
using stout::grpc::Stream;


TEST(GrpcTest, ServerUnavailable)
{
  // NOTE: we use 'getpid()' to create a _unique_ UNIX domain socket
  // path that should never have a server listening on for this test.
  Client client(
      "unix:stout-grpc-test-server-unavailable-" + stringify(getpid()),
      grpc::InsecureChannelCredentials());

  keyvaluestore::Request request;
  request.set_key("0");

  Notification<grpc::Status> finished;

  auto status = client.Call<
    Stream<keyvaluestore::Request>,
    Stream<keyvaluestore::Response>>(
        "keyvaluestore.KeyValueStore.GetValues",
        &request,
        [&](auto* call, auto&& response) {
        },
        [&](auto*, const grpc::Status& status) {
          finished.Notify(status);
        });

  ASSERT_TRUE(status.ok());

  ASSERT_EQ(grpc::UNAVAILABLE, finished.Wait().error_code());
}
