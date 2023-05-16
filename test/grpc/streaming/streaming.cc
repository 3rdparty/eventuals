#include "streaming.h"

namespace eventuals::grpc::test {

using keyvaluestore::Request;
using keyvaluestore::Response;

void test_client_behavior(
    Task::From<
        ClientCall<
            Stream<Request>,
            Stream<Response>>>::To<::grpc::Status>::Raises<RuntimeError>&&
        handler) {
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

} // namespace eventuals::grpc::test