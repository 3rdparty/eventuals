#include "eventuals/request-response-channel.h"

#include <deque>
#include <optional>
#include <tuple>

#include "eventuals/do-all.h"
#include "eventuals/loop.h"
#include "eventuals/map.h"
#include "eventuals/take.h"
#include "eventuals/then.h"
#include "gtest/gtest.h"
#include "test/promisify-for-test.h"

using eventuals::DoAll;
using eventuals::Loop;
using eventuals::Map;
using eventuals::RequestResponseChannel;
using eventuals::TakeFirst;
using eventuals::Then;

struct Request {
  std::string data{};
};

struct Response {
  std::string data{};
};

TEST(RequestResponseChannel, BunchOfRequests) {
  RequestResponseChannel<Request, Response> channel;

  auto operation = [&]() {
    return DoAll(
        channel.Read()
            >> TakeFirst(3)
            >> Map([&](Request&& request) {
                Response response{"response for " + request.data};
                return channel.Respond(std::move(response));
              })
            >> Loop(),
        Then([&]() {
          Request request{"request1"};
          return channel.Request(std::move(request))
              >> Then([](std::optional<Response>&& response) {
                   EXPECT_EQ(response->data, "response for request1");
                 });
        }),
        Then([&]() {
          Request request{"request2"};
          return channel.Request(std::move(request))
              >> Then([](std::optional<Response>&& response) {
                   EXPECT_EQ(response->data, "response for request2");
                 });
        }),
        Then([&]() {
          Request request{"request3"};
          return channel.Request(std::move(request))
              >> Then([](std::optional<Response>&& response) {
                   EXPECT_EQ(response->data, "response for request3");
                 });
        }));
  };

  EXPECT_EQ(
      *operation(),
      std::make_tuple(
          std::monostate{},
          std::monostate{},
          std::monostate{},
          std::monostate{}));
}

TEST(RequestResponseChannel, ReadBatch) {
  RequestResponseChannel<Request, Response> channel;

  auto read = [&]() {
    return channel.ReadBatch()
        >> Then([&](std::optional<std::deque<Request>>&& requests) {
             std::deque<Response> responses;
             for (const Request& request : *requests) {
               responses.push_back(Response{"response for " + request.data});
             }
             return channel.RespondBatch(std::move(responses));
           });
  };

  auto [future1, write1] =
      PromisifyForTest(channel.Request(Request{"request1"}));

  write1.Start();

  auto [future2, write2] =
      PromisifyForTest(channel.Request(Request{"request2"}));

  write2.Start();

  *read();

  std::optional<Response> response1 = future1.get();
  std::optional<Response> response2 = future2.get();

  ASSERT_TRUE(response1);
  ASSERT_TRUE(response2);

  EXPECT_EQ(response1->data, "response for request1");
  EXPECT_EQ(response2->data, "response for request2");
}
