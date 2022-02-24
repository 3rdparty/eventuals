#include "eventuals/http.h"

#include "event-loop-test.h"
#include "eventuals/eventual.h"
#include "eventuals/interrupt.h"
#include "eventuals/let.h"
#include "eventuals/scheduler.h"
#include "eventuals/terminal.h"
#include "eventuals/then.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "test/http-mock-server.h"

namespace http = eventuals::http;

using eventuals::EventLoop;
using eventuals::Interrupt;
using eventuals::Let;
using eventuals::Scheduler;
using eventuals::Terminate;
using eventuals::Then;

class HttpTest
  : public EventLoopTest,
    public ::testing::WithParamInterface<const char*> {};

const char* schemes[] = {"http://", "https://"};

INSTANTIATE_TEST_SUITE_P(Schemes, HttpTest, testing::ValuesIn(schemes));

TEST_P(HttpTest, Get) {
  std::string scheme = GetParam();

  HttpMockServer server(scheme);

  // NOTE: using an 'http::Client' configured to work for the server.
  http::Client client = server.Client();

  EXPECT_CALL(server, ReceivedHeaders)
      .WillOnce([](auto socket, const std::string& data) {
        socket->Send(
            "HTTP/1.1 200 OK\r\n"
            "Foo: Bar\r\n"
            "Content-Length: 25\r\n"
            "\r\n"
            "<html>Hello World!</html>\r\n"
            "\r\n");

        socket->Close();
      });

  auto e = client.Get(server.uri());
  auto [future, k] = Terminate(std::move(e));
  k.Start();

  EventLoop::Default().RunUntil(future);

  auto response = future.get();

  EXPECT_EQ(200, response.code());
  EXPECT_THAT(
      response.headers(),
      testing::Contains(http::Header("Foo", "Bar")));
  EXPECT_THAT(
      response.headers(),
      testing::Contains(http::Header("Content-Length", "25")));
  EXPECT_EQ("<html>Hello World!</html>", response.body());
}


TEST_P(HttpTest, GetGet) {
  std::string scheme = GetParam();

  HttpMockServer server(scheme);

  // NOTE: using an 'http::Client' configured to work for the server.
  http::Client client = server.Client();

  EXPECT_CALL(server, ReceivedHeaders)
      .WillOnce([](auto socket, const std::string& data) {
        socket->Send(
            "HTTP/1.1 200 OK\r\n"
            "Content-Length: 26\r\n"
            "\r\n"
            "<html>Hello Nikita!</html>\r\n"
            "\r\n");

        socket->Close();
      })
      .WillOnce([](auto socket, const std::string& data) {
        socket->Send(
            "HTTP/1.1 200 OK\r\n"
            "Content-Length: 23\r\n"
            "\r\n"
            "<html>Hello Ben!</html>\r\n"
            "\r\n");

        socket->Close();
      });

  auto e = client.Get(server.uri())
      | Then(Let([&](auto& response1) {
             return client.Get(server.uri())
                 | Then([&](auto&& response2) {
                      return std::tuple{response1, response2};
                    });
           }));

  auto [future, k] = Terminate(std::move(e));

  k.Start();

  EventLoop::Default().RunUntil(future);

  auto [response1, response2] = future.get();

  EXPECT_EQ(200, response1.code());
  EXPECT_THAT(
      response1.headers(),
      testing::Contains(http::Header("Content-Length", "26")));
  EXPECT_EQ("<html>Hello Nikita!</html>", response1.body());

  EXPECT_EQ(200, response2.code());
  EXPECT_THAT(
      response2.headers(),
      testing::Contains(http::Header("Content-Length", "23")));
  EXPECT_EQ("<html>Hello Ben!</html>", response2.body());
}


TEST_P(HttpTest, GetFailTimeout) {
  std::string scheme = GetParam();

  auto e = http::Get(scheme + "example.com", std::chrono::milliseconds(1));
  auto [future, k] = Terminate(std::move(e));
  k.Start();

  EventLoop::Default().RunUntil(future);

  EXPECT_THROW(future.get(), const char*);
}


TEST_P(HttpTest, PostFailTimeout) {
  std::string scheme = GetParam();

  auto e = http::Post(
      scheme + "jsonplaceholder.typicode.com/posts",
      {{"title", "test"},
       {"body", "message"}},
      std::chrono::milliseconds(1));
  auto [future, k] = Terminate(std::move(e));
  k.Start();

  EventLoop::Default().RunUntil(future);

  EXPECT_THROW(future.get(), const char*);
}


TEST_P(HttpTest, GetInterrupt) {
  std::string scheme = GetParam();

  auto e = http::Get(scheme + "example.com");
  auto [future, k] = Terminate(std::move(e));

  Interrupt interrupt;

  k.Register(interrupt);

  k.Start();

  interrupt.Trigger();

  EventLoop::Default().RunUntil(future);

  EXPECT_THROW(future.get(), eventuals::StoppedException);
}


TEST_P(HttpTest, PostInterrupt) {
  std::string scheme = GetParam();

  auto e = http::Post(
      scheme + "jsonplaceholder.typicode.com/posts",
      {{"title", "test"},
       {"body", "message"}});
  auto [future, k] = Terminate(std::move(e));

  Interrupt interrupt;

  k.Register(interrupt);

  k.Start();

  interrupt.Trigger();

  EventLoop::Default().RunUntil(future);

  EXPECT_THROW(future.get(), eventuals::StoppedException);
}


TEST_P(HttpTest, GetInterruptAfterStart) {
  std::string scheme = GetParam();

  auto e = http::Get(scheme + "example.com");
  auto [future, k] = Terminate(std::move(e));

  Interrupt interrupt;

  k.Register(interrupt);

  k.Start();

  // NOTE: now that we've started the continuation 'k' we will have
  // submitted a callback to the event loop and thus by explicitly
  // submitting another callback we will ensure there is a
  // happens-before relationship between starting the transfer
  // and triggering the interrupt.
  Scheduler::Context context(&EventLoop::Default(), "interrupt.Trigger()");

  EventLoop::Default().Submit(
      [&interrupt]() {
        interrupt.Trigger();
      },
      &context);

  EventLoop::Default().RunUntil(future);

  EXPECT_THROW(future.get(), eventuals::StoppedException);
}


TEST_P(HttpTest, PostInterruptAfterStart) {
  std::string scheme = GetParam();

  auto e = http::Post(
      scheme + "jsonplaceholder.typicode.com/posts",
      {{"title", "test"},
       {"body", "message"}});
  auto [future, k] = Terminate(std::move(e));

  Interrupt interrupt;

  k.Register(interrupt);

  k.Start();

  // NOTE: now that we've started the continuation 'k' we will have
  // submitted a callback to the event loop and thus by explicitly
  // submitting another callback we will ensure there is a
  // happens-before relationship between starting the transfer
  // and triggering the interrupt.
  Scheduler::Context context(&EventLoop::Default(), "interrupt.Trigger()");

  EventLoop::Default().Submit(
      [&interrupt]() {
        interrupt.Trigger();
      },
      &context);

  EventLoop::Default().RunUntil(future);

  EXPECT_THROW(future.get(), eventuals::StoppedException);
}


TEST_P(HttpTest, GetHeaders) {
  std::string scheme = GetParam();

  HttpMockServer server(scheme);

  // NOTE: using an 'http::Client' configured to work for the server.
  http::Client client = server.Client();

  EXPECT_CALL(server, ReceivedHeaders)
      .WillOnce([](auto socket, const std::string& data) {
        EXPECT_THAT(data, testing::ContainsRegex("foo: bar"));

        socket->Send(
            "HTTP/1.1 200 OK\r\n"
            "Foo: Bar\r\n"
            "Content-Length: 25\r\n"
            "\r\n"
            "<html>Hello World!</html>\r\n"
            "\r\n");

        socket->Close();
      });

  auto e = client.Do(
      http::Request::Builder()
          .uri(server.uri())
          .method(http::GET)
          .header("foo", "bar")
          .Build());
  auto [future, k] = Terminate(std::move(e));
  k.Start();

  EventLoop::Default().RunUntil(future);

  auto response = future.get();

  EXPECT_EQ(200, response.code());
  EXPECT_THAT(
      response.headers(),
      testing::Contains(http::Header("Foo", "Bar")));
  EXPECT_THAT(
      response.headers(),
      testing::Contains(http::Header("Content-Length", "25")));
  EXPECT_EQ("<html>Hello World!</html>", response.body());
}


TEST_P(HttpTest, GetDuplicateHeaders) {
  std::string scheme = GetParam();

  HttpMockServer server(scheme);

  // NOTE: using an 'http::Client' configured to work for the server.
  http::Client client = server.Client();

  EXPECT_CALL(server, ReceivedHeaders)
      .WillOnce([](auto socket, const std::string& data) {
        EXPECT_THAT(data, testing::ContainsRegex("foo: bar1, bar2"));

        socket->Send(
            "HTTP/1.1 200 OK\r\n"
            "Foo: Bar1\r\n"
            "Foo: Bar2\r\n"
            "Content-Length: 25\r\n"
            "\r\n"
            "<html>Hello World!</html>\r\n"
            "\r\n");

        socket->Close();
      });

  auto e = client.Do(
      http::Request::Builder()
          .uri(server.uri())
          .method(http::GET)
          .header("foo", "bar1")
          .header("foo", "bar2")
          .Build());
  auto [future, k] = Terminate(std::move(e));
  k.Start();

  EventLoop::Default().RunUntil(future);

  auto response = future.get();

  EXPECT_EQ(200, response.code());
  EXPECT_THAT(
      response.headers(),
      testing::Contains(http::Header("Foo", "Bar1, Bar2")));
  EXPECT_THAT(
      response.headers(),
      testing::Contains(http::Header("Content-Length", "25")));
  EXPECT_EQ("<html>Hello World!</html>", response.body());
}
