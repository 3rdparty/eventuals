#include "test/grpc/streaming/streaming.h"

namespace eventuals::grpc::test {
namespace {

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

} // namespace
} // namespace eventuals::grpc::test