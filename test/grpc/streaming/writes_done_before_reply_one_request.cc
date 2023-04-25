#include "test/grpc/streaming/streaming.h"

namespace eventuals::grpc::test {
namespace {

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

} // namespace
} // namespace eventuals::grpc::test