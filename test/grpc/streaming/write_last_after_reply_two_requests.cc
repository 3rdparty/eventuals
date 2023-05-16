#include "test/grpc/streaming/streaming.h"

namespace eventuals::grpc::test {
namespace {

TEST(StreamingTest, WriteLast_AfterReply_TwoRequests) {
  auto task = []()
      -> Task::From<
          ClientCall<
              Stream<Request>,
              Stream<Response>>>::To<::grpc::Status>::Raises<RuntimeError> {
    return []() {
      return Then(Let([](auto& call) {
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
      }));
    };
  };

  test_client_behavior(std::move(task));
}

} // namespace
} // namespace eventuals::grpc::test