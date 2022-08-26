#include "eventuals/closure.hh"
#include "eventuals/grpc/client.hh"
#include "eventuals/grpc/server.hh"
#include "eventuals/head.hh"
#include "eventuals/iterate.hh"
#include "eventuals/let.hh"
#include "eventuals/loop.hh"
#include "eventuals/map.hh"
#include "eventuals/then.hh"
#include "examples/protos/keyvaluestore.grpc.pb.h"
#include "gtest/gtest.h"
#include "test/grpc/test.hh"
#include "test/promisify-for-test.hh"

namespace eventuals::grpc::test {
namespace {

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

template <typename Handler>
void test_client_behavior(Handler handler) {
  ServerBuilder builder;

  int port = 0;

  builder.AddListeningPort(
      "0.0.0.0:0",
      ::grpc::InsecureServerCredentials(),
      &port);

  auto build = builder.BuildAndStart();

  ASSERT_TRUE(build.status.ok()) << build.status;

  auto server = std::move(build.server);

  ASSERT_TRUE(server);

  auto serve = [&]() {
    return server->Accept<
               Stream<Request>,
               Stream<Response>>(
               "keyvaluestore.KeyValueStore.GetValues")
        >> Head()
        >> Then(Let([](auto& call) {
             return call.Reader().Read()
                 >> Map([&](Request&& request) {
                      Response response;
                      response.set_value(request.key());
                      return call.Writer().Write(response);
                    })
                 >> Loop()
                 >> Closure([]() {
                      std::vector<Response> responses;
                      for (size_t i = 0; i < 3; i++) {
                        Response response;
                        response.set_value(std::to_string(i + 10));
                        responses.push_back(response);
                      }
                      return Iterate(std::move(responses));
                    })
                 >> StreamingEpilogue(call);
           }));
  };

  auto [cancelled, k] = PromisifyForTest(serve());

  k.Start();

  Borrowable<ClientCompletionThreadPool> pool;

  Client client(
      "0.0.0.0:" + std::to_string(port),
      ::grpc::InsecureChannelCredentials(),
      pool.Borrow());

  auto call = [&]() {
    return client.Call<
               Stream<Request>,
               Stream<Response>>(
               "keyvaluestore.KeyValueStore.GetValues")
        >> std::move(handler);
  };

  auto status = *call();

  EXPECT_TRUE(status.ok()) << status.error_code()
                           << ": " << status.error_message();

  EXPECT_FALSE(cancelled.get());
}

TEST(StreamingTest, WriteLast_AfterReply_TwoRequests) {
  test_client_behavior(
      Then(Let([](auto& call) {
        Request request;
        request.set_key("1");
        return call.Writer().Write(request)
            >> call.Reader().Read()
            >> Head()
            >> Then([&](Response&& response) {
                 EXPECT_EQ("1", response.value());
                 Request request;
                 request.set_key("2");
                 return call.Writer().WriteLast(request);
               })
            >> call.Reader().Read()
            >> Head()
            >> Then([](Response&& response) {
                 EXPECT_EQ("2", response.value());
               })
            >> call.Reader().Read()
            >> Head()
            >> Then([](Response&& response) {
                 EXPECT_EQ("10", response.value());
               })
            >> call.Reader().Read()
            >> Head()
            >> Then([](Response&& response) {
                 EXPECT_EQ("11", response.value());
               })
            >> call.Reader().Read()
            >> Head()
            >> Then([](Response&& response) {
                 EXPECT_EQ("12", response.value());
               })
            >> call.Finish();
      })));
}

TEST(StreamingTest, WriteLast_BeforeReply_OneRequest) {
  test_client_behavior(
      Then(Let([](auto& call) {
        Request request;
        request.set_key("1");
        return call.Writer().WriteLast(request)
            >> call.Reader().Read()
            >> Head()
            >> Then([](Response&& response) {
                 EXPECT_EQ("1", response.value());
               })
            >> call.Reader().Read()
            >> Head()
            >> Then([](Response&& response) {
                 EXPECT_EQ("10", response.value());
               })
            >> call.Reader().Read()
            >> Head()
            >> Then([](Response&& response) {
                 EXPECT_EQ("11", response.value());
               })
            >> call.Reader().Read()
            >> Head()
            >> Then([](Response&& response) {
                 EXPECT_EQ("12", response.value());
               })
            >> call.Finish();
      })));
}

TEST(StreamingTest, WriteLast_BeforeReply_TwoRequests) {
  test_client_behavior(
      Then(Let([](auto& call) {
        Request request1;
        request1.set_key("1");
        return call.Writer().Write(request1)
            >> Then([&]() {
                 Request request2;
                 request2.set_key("2");
                 return call.Writer().WriteLast(request2);
               })
            >> call.Reader().Read()
            >> Head()
            >> Then([](Response&& response) {
                 EXPECT_EQ("1", response.value());
               })
            >> call.Reader().Read()
            >> Head()
            >> Then([](Response&& response) {
                 EXPECT_EQ("2", response.value());
               })
            >> call.Reader().Read()
            >> Head()
            >> Then([](Response&& response) {
                 EXPECT_EQ("10", response.value());
               })
            >> call.Reader().Read()
            >> Head()
            >> Then([](Response&& response) {
                 EXPECT_EQ("11", response.value());
               })
            >> call.Reader().Read()
            >> Head()
            >> Then([](Response&& response) {
                 EXPECT_EQ("12", response.value());
               })
            >> call.Finish();
      })));
}

TEST(StreamingTest, WritesDone_AfterReply_OneRequest) {
  test_client_behavior(
      Then(Let([](auto& call) {
        Request request;
        request.set_key("1");
        return call.Writer().Write(request)
            >> call.Reader().Read()
            >> Head()
            >> Then([&](Response&& response) {
                 EXPECT_EQ("1", response.value());
                 return call.WritesDone();
               })
            >> call.Reader().Read()
            >> Head()
            >> Then([](Response&& response) {
                 EXPECT_EQ("10", response.value());
               })
            >> call.Reader().Read()
            >> Head()
            >> Then([](Response&& response) {
                 EXPECT_EQ("11", response.value());
               })
            >> call.Reader().Read()
            >> Head()
            >> Then([](Response&& response) {
                 EXPECT_EQ("12", response.value());
               })
            >> call.Finish();
      })));
}

TEST(StreamingTest, WritesDone_AfterReply_TwoRequests) {
  test_client_behavior(
      Then(Let([](auto& call) {
        Request request;
        request.set_key("1");
        return call.Writer().Write(request)
            >> call.Reader().Read()
            >> Head()
            >> Then([&](Response&& response) {
                 EXPECT_EQ("1", response.value());
                 Request request;
                 request.set_key("2");
                 return call.Writer().Write(request);
               })
            >> call.Reader().Read()
            >> Head()
            >> Then([&](Response&& response) {
                 EXPECT_EQ("2", response.value());
                 return call.WritesDone();
               })
            >> call.Reader().Read()
            >> Head()
            >> Then([](Response&& response) {
                 EXPECT_EQ("10", response.value());
               })
            >> call.Reader().Read()
            >> Head()
            >> Then([](Response&& response) {
                 EXPECT_EQ("11", response.value());
               })
            >> call.Reader().Read()
            >> Head()
            >> Then([](Response&& response) {
                 EXPECT_EQ("12", response.value());
               })
            >> call.Finish();
      })));
}

TEST(StreamingTest, WritesDone_BeforeReply_OneRequest) {
  test_client_behavior(
      Then(Let([](auto& call) {
        Request request1;
        request1.set_key("1");
        return call.Writer().Write(request1)
            >> call.WritesDone()
            >> call.Reader().Read()
            >> Head()
            >> Then([](Response&& response) {
                 EXPECT_EQ("1", response.value());
               })
            >> call.Reader().Read()
            >> Head()
            >> Then([](Response&& response) {
                 EXPECT_EQ("10", response.value());
               })
            >> call.Reader().Read()
            >> Head()
            >> Then([](Response&& response) {
                 EXPECT_EQ("11", response.value());
               })
            >> call.Reader().Read()
            >> Head()
            >> Then([](Response&& response) {
                 EXPECT_EQ("12", response.value());
               })
            >> call.Finish();
      })));
}

TEST(StreamingTest, WritesDone_BeforeReply_TwoRequests) {
  test_client_behavior(
      Then(Let([](auto& call) {
        Request request1;
        request1.set_key("1");
        return call.Writer().Write(request1)
            >> Then([&]() {
                 Request request2;
                 request2.set_key("2");
                 return call.Writer().Write(request2)
                     >> call.WritesDone();
               })
            >> call.Reader().Read()
            >> Head()
            >> Then([](Response&& response) {
                 EXPECT_EQ("1", response.value());
               })
            >> call.Reader().Read()
            >> Head()
            >> Then([](Response&& response) {
                 EXPECT_EQ("2", response.value());
               })
            >> call.Reader().Read()
            >> Head()
            >> Then([](Response&& response) {
                 EXPECT_EQ("10", response.value());
               })
            >> call.Reader().Read()
            >> Head()
            >> Then([](Response&& response) {
                 EXPECT_EQ("11", response.value());
               })
            >> call.Reader().Read()
            >> Head()
            >> Then([](Response&& response) {
                 EXPECT_EQ("12", response.value());
               })
            >> call.Finish();
      })));
}

} // namespace
} // namespace eventuals::grpc::test
