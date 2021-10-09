#include "stout/http.h"

#include "event-loop-test.h"
#include "gtest/gtest.h"
#include "http-server-mock.h"
#include "stout/eventual.h"
#include "stout/interrupt.h"
#include "stout/lambda.h"
#include "stout/terminal.h"

namespace eventuals = stout::eventuals;

using eventuals::EventLoop;
using eventuals::Interrupt;
using eventuals::Terminate;
using eventuals::Then;

using eventuals::http::Get;
using eventuals::http::Post;
using eventuals::http::PostFields;

class HTTPTest : public EventLoopTest {};

TEST_F(HTTPTest, GetSucceedMock) {
  int port_binded;
  HTTPMockServer server;
  server.Run(EventLoop::Default(), &port_binded);

  std::string server_url = "http://127.0.0.1:" + std::to_string(port_binded);

  std::string expected_result("<html></html>");

  auto e = Get(server_url)
      | Then([&expected_result](auto&& response) {
             EXPECT_EQ(response.code, 200);
             EXPECT_EQ(response.body, expected_result);
           });
  auto [future, k] = Terminate(std::move(e));
  k.Start();

  EventLoop::Default().Run();

  future.get();
}

TEST_F(HTTPTest, PostSucceedMock) {
  int port_binded;
  HTTPMockServer server;
  server.Run(EventLoop::Default(), &port_binded);

  std::string server_url = "http://127.0.0.1:" + std::to_string(port_binded);

  std::string expected_result(
      "{\n"
      "  \"body\": \"message\",\n"
      "  \"title\": \"test\",\n"
      "  \"id\": 101\n"
      "}");

  PostFields fields = {
      {"title", "test"},
      {"body", "message"}};

  auto e = Post(server_url, fields)
      | Then([&expected_result](auto&& response) {
             EXPECT_EQ(response.code, 201);
             EXPECT_EQ(response.body, expected_result);
           });
  auto [future, k] = Terminate(std::move(e));
  k.Start();

  EventLoop::Default().Run();

  future.get();
}

TEST_F(HTTPTest, GetFailTimeout) {
  auto e = Get("http://example.com", std::chrono::milliseconds(1));
  auto [future, k] = Terminate(std::move(e));
  k.Start();

  EventLoop::Default().Run();

  EXPECT_THROW(future.get(), const char*);
}

TEST_F(HTTPTest, PostFailTimeout) {
  PostFields fields = {
      {"title", "test"},
      {"body", "message"}};

  auto e = Post(
      "http://jsonplaceholder.typicode.com/posts",
      fields,
      std::chrono::milliseconds(1));
  auto [future, k] = Terminate(std::move(e));
  k.Start();

  EventLoop::Default().Run();

  EXPECT_THROW(future.get(), const char*);
}

TEST_F(HTTPTest, GetInterrupt) {
  auto e = Get("http://example.com");
  auto [future, k] = Terminate(std::move(e));

  Interrupt interrupt;

  k.Register(interrupt);

  k.Start();

  interrupt.Trigger();

  EventLoop::Default().Run();

  EXPECT_THROW(future.get(), eventuals::StoppedException);
}

TEST_F(HTTPTest, PostInterrupt) {
  PostFields fields = {
      {"title", "test"},
      {"body", "message"}};

  auto e = Post(
      "http://jsonplaceholder.typicode.com/posts",
      fields);
  auto [future, k] = Terminate(std::move(e));

  Interrupt interrupt;

  k.Register(interrupt);

  k.Start();

  interrupt.Trigger();

  EventLoop::Default().Run();

  EXPECT_THROW(future.get(), eventuals::StoppedException);
}

TEST_F(HTTPTest, GetInterruptAfterStart) {
  auto e = Get("http://example.com");
  auto [future, k] = Terminate(std::move(e));

  Interrupt interrupt;

  k.Register(interrupt);

  k.Start();

  EventLoop::Waiter waiter(&EventLoop::Default(), "interrupt.Trigger()");

  EventLoop::Default().Submit(
      [&interrupt]() {
        interrupt.Trigger();
      },
      &waiter);

  EventLoop::Default().Run();

  EXPECT_THROW(future.get(), eventuals::StoppedException);
}

TEST_F(HTTPTest, PostInterruptAfterStart) {
  PostFields fields = {
      {"title", "test"},
      {"body", "message"}};

  auto e = Post(
      "http://jsonplaceholder.typicode.com/posts",
      fields);
  auto [future, k] = Terminate(std::move(e));

  Interrupt interrupt;

  k.Register(interrupt);

  k.Start();

  EventLoop::Waiter waiter(&EventLoop::Default(), "interrupt.Trigger()");

  EventLoop::Default().Submit(
      [&interrupt]() {
        interrupt.Trigger();
      },
      &waiter);

  EventLoop::Default().Run();

  EXPECT_THROW(future.get(), eventuals::StoppedException);
}