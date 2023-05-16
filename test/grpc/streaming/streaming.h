#pragma once

#include "eventuals/closure.h"
#include "eventuals/grpc/client.h"
#include "eventuals/grpc/server.h"
#include "eventuals/head.h"
#include "eventuals/iterate.h"
#include "eventuals/let.h"
#include "eventuals/loop.h"
#include "eventuals/map.h"
#include "eventuals/task.h"
#include "eventuals/then.h"
#include "examples/protos/keyvaluestore.grpc.pb.h"
#include "gtest/gtest.h"
#include "test/grpc/test.h"
#include "test/promisify-for-test.h"

namespace eventuals::grpc::test {

using keyvaluestore::Request;
using keyvaluestore::Response;
using stout::Borrowable;

// We can vary the usage of the streaming API on three dimensions, each of which
// leads to different concurrency situations in `client.h`:
// 1. Do we use WriteLast() or WritesDone() to close the gRPC stream?
// 2. Do we close the gRPC stream before or after receiving a reply?
// 3. Do we send one, or multiple requests before closing the stream?
//
// This leads to 2*2*2=8 different possible test cases. Of those, one
// combination is nonsensical: if we...
//   ... use WriteLast(), which sends a request
//   ... after receiving a reply to a request
//   ... we MUST therefore be sending more than one request before closing.
//
// All other 7 test cases are important; we've had unique bugs in each of them!
//
// Test naming is structured as follows:
//   Streaming
//     _[WriteLast|WritesDone]
//     _[AfterReply|BeforeReply]
//     _[OneRequest|TwoRequests]

void test_client_behavior(
    Task::From<
        ClientCall<
            Stream<Request>,
            Stream<Response>>>::To<::grpc::Status>::Raises<RuntimeError>&&
        handler);

} // namespace eventuals::grpc::test