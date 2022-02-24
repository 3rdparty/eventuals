#include "eventuals/closure.h"
#include "eventuals/grpc/client.h"
#include "eventuals/grpc/server.h"
#include "eventuals/head.h"
#include "eventuals/iterate.h"
#include "eventuals/let.h"
#include "eventuals/loop.h"
#include "eventuals/map.h"
#include "eventuals/then.h"
#include "examples/protos/keyvaluestore.grpc.pb.h"
#include "gtest/gtest.h"
#include "test/test.h"

using stout::Borrowable;

using eventuals::Closure;
using eventuals::Head;
using eventuals::Iterate;
using eventuals::Let;
using eventuals::Loop;
using eventuals::Map;
using eventuals::Terminate;
using eventuals::Then;

using eventuals::grpc::Client;
using eventuals::grpc::CompletionPool;
using eventuals::grpc::Server;
using eventuals::grpc::ServerBuilder;
using eventuals::grpc::Stream;

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

template <typename Handler>
void test_client_behavior(Handler handler) {
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
    return server->Accept<
               Stream<keyvaluestore::Request>,
               Stream<keyvaluestore::Response>>(
               "keyvaluestore.KeyValueStore.GetValues")
        | Head()
        | Then(Let([](auto& call) {
             return call.Reader().Read()
                 | Map([&](auto&& request) {
                      keyvaluestore::Response response;
                      response.set_value(request.key());
                      return call.Writer().Write(response);
                    })
                 | Loop()
                 | Closure([]() {
                      std::vector<keyvaluestore::Response> responses;
                      for (size_t i = 0; i < 3; i++) {
                        keyvaluestore::Response response;
                        response.set_value(std::to_string(i + 10));
                        responses.push_back(response);
                      }
                      return Iterate(std::move(responses));
                    })
                 | StreamingEpilogue(call);
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
    return client.Call<
               Stream<keyvaluestore::Request>,
               Stream<keyvaluestore::Response>>(
               "keyvaluestore.KeyValueStore.GetValues")
        | std::move(handler);
  };

  auto status = *call();

  EXPECT_TRUE(status.ok()) << status.error_message();

  EXPECT_FALSE(cancelled.get());
}

TEST_F(EventualsGrpcTest, Streaming_WriteLast_AfterReply_TwoRequests) {
  test_client_behavior(
      Then(Let([](auto& call) {
        keyvaluestore::Request request;
        request.set_key("1");
        return call.Writer().Write(request)
            | call.Reader().Read()
            | Head()
            | Then([&](auto&& response) {
                 EXPECT_EQ("1", response.value());
                 keyvaluestore::Request request;
                 request.set_key("2");
                 return call.Writer().WriteLast(request);
               })
            | call.Reader().Read()
            | Head()
            | Then([](auto&& response) {
                 EXPECT_EQ("2", response.value());
               })
            | call.Reader().Read()
            | Head()
            | Then([](auto&& response) {
                 EXPECT_EQ("10", response.value());
               })
            | call.Reader().Read()
            | Head()
            | Then([](auto&& response) {
                 EXPECT_EQ("11", response.value());
               })
            | call.Reader().Read()
            | Head()
            | Then([](auto&& response) {
                 EXPECT_EQ("12", response.value());
               })
            | call.Finish();
      })));
}

TEST_F(EventualsGrpcTest, Streaming_WriteLast_BeforeReply_OneRequest) {
  test_client_behavior(
      Then(Let([](auto& call) {
        keyvaluestore::Request request;
        request.set_key("1");
        return call.Writer().WriteLast(request)
            | call.Reader().Read()
            | Head()
            | Then([](auto&& response) {
                 EXPECT_EQ("1", response.value());
               })
            | call.Reader().Read()
            | Head()
            | Then([](auto&& response) {
                 EXPECT_EQ("10", response.value());
               })
            | call.Reader().Read()
            | Head()
            | Then([](auto&& response) {
                 EXPECT_EQ("11", response.value());
               })
            | call.Reader().Read()
            | Head()
            | Then([](auto&& response) {
                 EXPECT_EQ("12", response.value());
               })
            | call.Finish();
      })));
}

TEST_F(EventualsGrpcTest, Streaming_WriteLast_BeforeReply_TwoRequests) {
  test_client_behavior(
      Then(Let([](auto& call) {
        keyvaluestore::Request request1;
        request1.set_key("1");
        return call.Writer().Write(request1)
            | Then([&]() {
                 keyvaluestore::Request request2;
                 request2.set_key("2");
                 return call.Writer().WriteLast(request2);
               })
            | call.Reader().Read()
            | Head()
            | Then([](auto&& response) {
                 EXPECT_EQ("1", response.value());
               })
            | call.Reader().Read()
            | Head()
            | Then([](auto&& response) {
                 EXPECT_EQ("2", response.value());
               })
            | call.Reader().Read()
            | Head()
            | Then([](auto&& response) {
                 EXPECT_EQ("10", response.value());
               })
            | call.Reader().Read()
            | Head()
            | Then([](auto&& response) {
                 EXPECT_EQ("11", response.value());
               })
            | call.Reader().Read()
            | Head()
            | Then([](auto&& response) {
                 EXPECT_EQ("12", response.value());
               })
            | call.Finish();
      })));
}

TEST_F(EventualsGrpcTest, Streaming_WritesDone_AfterReply_OneRequest) {
  test_client_behavior(
      Then(Let([](auto& call) {
        keyvaluestore::Request request;
        request.set_key("1");
        return call.Writer().Write(request)
            | call.Reader().Read()
            | Head()
            | Then([&](auto&& response) {
                 EXPECT_EQ("1", response.value());
                 return call.WritesDone();
               })
            | call.Reader().Read()
            | Head()
            | Then([](auto&& response) {
                 EXPECT_EQ("10", response.value());
               })
            | call.Reader().Read()
            | Head()
            | Then([](auto&& response) {
                 EXPECT_EQ("11", response.value());
               })
            | call.Reader().Read()
            | Head()
            | Then([](auto&& response) {
                 EXPECT_EQ("12", response.value());
               })
            | call.Finish();
      })));
}

TEST_F(EventualsGrpcTest, Streaming_WritesDone_AfterReply_TwoRequests) {
  test_client_behavior(
      Then(Let([](auto& call) {
        keyvaluestore::Request request;
        request.set_key("1");
        return call.Writer().Write(request)
            | call.Reader().Read()
            | Head()
            | Then([&](auto&& response) {
                 EXPECT_EQ("1", response.value());
                 keyvaluestore::Request request;
                 request.set_key("2");
                 return call.Writer().Write(request);
               })
            | call.Reader().Read()
            | Head()
            | Then([&](auto&& response) {
                 EXPECT_EQ("2", response.value());
                 return call.WritesDone();
               })
            | call.Reader().Read()
            | Head()
            | Then([](auto&& response) {
                 EXPECT_EQ("10", response.value());
               })
            | call.Reader().Read()
            | Head()
            | Then([](auto&& response) {
                 EXPECT_EQ("11", response.value());
               })
            | call.Reader().Read()
            | Head()
            | Then([](auto&& response) {
                 EXPECT_EQ("12", response.value());
               })
            | call.Finish();
      })));
}

TEST_F(EventualsGrpcTest, Streaming_WritesDone_BeforeReply_OneRequest) {
  test_client_behavior(
      Then(Let([](auto& call) {
        keyvaluestore::Request request1;
        request1.set_key("1");
        return call.Writer().Write(request1)
            | call.WritesDone()
            | call.Reader().Read()
            | Head()
            | Then([](auto&& response) {
                 EXPECT_EQ("1", response.value());
               })
            | call.Reader().Read()
            | Head()
            | Then([](auto&& response) {
                 EXPECT_EQ("10", response.value());
               })
            | call.Reader().Read()
            | Head()
            | Then([](auto&& response) {
                 EXPECT_EQ("11", response.value());
               })
            | call.Reader().Read()
            | Head()
            | Then([](auto&& response) {
                 EXPECT_EQ("12", response.value());
               })
            | call.Finish();
      })));
}

TEST_F(EventualsGrpcTest, Streaming_WritesDone_BeforeReply_TwoRequests) {
  test_client_behavior(
      Then(Let([](auto& call) {
        keyvaluestore::Request request1;
        request1.set_key("1");
        return call.Writer().Write(request1)
            | Then([&]() {
                 keyvaluestore::Request request2;
                 request2.set_key("2");
                 return call.Writer().Write(request2)
                     | call.WritesDone();
               })
            | call.Reader().Read()
            | Head()
            | Then([](auto&& response) {
                 EXPECT_EQ("1", response.value());
               })
            | call.Reader().Read()
            | Head()
            | Then([](auto&& response) {
                 EXPECT_EQ("2", response.value());
               })
            | call.Reader().Read()
            | Head()
            | Then([](auto&& response) {
                 EXPECT_EQ("10", response.value());
               })
            | call.Reader().Read()
            | Head()
            | Then([](auto&& response) {
                 EXPECT_EQ("11", response.value());
               })
            | call.Reader().Read()
            | Head()
            | Then([](auto&& response) {
                 EXPECT_EQ("12", response.value());
               })
            | call.Finish();
      })));
}
