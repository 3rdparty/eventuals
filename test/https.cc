#include "event-loop-test.h"
#include "gtest/gtest.h"
#include "http-server-mock.h"
#include "stout/eventual.h"
#include "stout/http.h"
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

class HTTPSTest : public EventLoopTest {};

TEST_F(HTTPSTest, GetFailTimeout) {
  auto e = Get("https://example.com", std::chrono::milliseconds(1));
  auto [future, k] = Terminate(std::move(e));
  k.Start();

  EventLoop::Default().Run();

  EXPECT_THROW(future.get(), const char*);
}

TEST_F(HTTPSTest, PostFailTimeout) {
  PostFields fields = {
      {"title", "test"},
      {"body", "message"}};

  auto e = Post(
      "https://jsonplaceholder.typicode.com/posts",
      fields,
      std::chrono::milliseconds(1));
  auto [future, k] = Terminate(std::move(e));
  k.Start();

  EventLoop::Default().Run();

  EXPECT_THROW(future.get(), const char*);
}

TEST_F(HTTPSTest, GetInterrupt) {
  auto e = Get("https://example.com");
  auto [future, k] = Terminate(std::move(e));

  Interrupt interrupt;

  k.Register(interrupt);

  k.Start();

  interrupt.Trigger();

  EventLoop::Default().Run();

  EXPECT_THROW(future.get(), eventuals::StoppedException);
}

TEST_F(HTTPSTest, PostInterrupt) {
  PostFields fields = {
      {"title", "test"},
      {"body", "message"}};

  auto e = Post(
      "https://jsonplaceholder.typicode.com/posts",
      fields);
  auto [future, k] = Terminate(std::move(e));

  Interrupt interrupt;

  k.Register(interrupt);

  k.Start();

  interrupt.Trigger();

  EventLoop::Default().Run();

  EXPECT_THROW(future.get(), eventuals::StoppedException);
}

TEST_F(HTTPSTest, GetInterruptAfterStart) {
  auto e = Get("https://example.com");
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

TEST_F(HTTPSTest, PostInterruptAfterStart) {
  PostFields fields = {
      {"title", "test"},
      {"body", "message"}};

  auto e = Post(
      "https://jsonplaceholder.typicode.com/posts",
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