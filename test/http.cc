#include "eventuals/http.h"

#include "event-loop-test.h"
#include "eventuals/eventual.h"
#include "eventuals/interrupt.h"
#include "eventuals/terminal.h"
#include "gtest/gtest.h"

using eventuals::EventLoop;
using eventuals::Interrupt;
using eventuals::Terminate;

using eventuals::http::Get;
using eventuals::http::Post;
using eventuals::http::PostFields;

class HTTPTest
  : public EventLoopTest,
    public ::testing::WithParamInterface<const char*> {};

// NOTE: we don't compile https tests on Windows
// because we currently can't reliably compile
// either OpenSSL or boringssl on Windows (see #59).
#ifdef _WIN32
const char* schemes[] = {"http://"};
#else
const char* schemes[] = {"http://", "https://"};
#endif

INSTANTIATE_TEST_SUITE_P(Schemes, HTTPTest, testing::ValuesIn(schemes));

// Current tests implementation rely on the transfers not
// being able to complete within a very short period.
// TODO(folming): Use HTTP mock server to not rely on external hosts.

TEST_P(HTTPTest, GetFailTimeout) {
  std::string scheme = GetParam();

  auto e = Get(scheme + "example.com", std::chrono::milliseconds(1));
  auto [future, k] = Terminate(std::move(e));
  k.Start();

  EventLoop::Default().Run();

  EXPECT_THROW(future.get(), const char*);
}


TEST_P(HTTPTest, PostFailTimeout) {
  std::string scheme = GetParam();

  PostFields fields = {
      {"title", "test"},
      {"body", "message"}};

  auto e = Post(
      scheme + "jsonplaceholder.typicode.com/posts",
      fields,
      std::chrono::milliseconds(1));
  auto [future, k] = Terminate(std::move(e));
  k.Start();

  EventLoop::Default().Run();

  EXPECT_THROW(future.get(), const char*);
}


TEST_P(HTTPTest, GetInterrupt) {
  std::string scheme = GetParam();

  auto e = Get(scheme + "example.com");
  auto [future, k] = Terminate(std::move(e));

  Interrupt interrupt;

  k.Register(interrupt);

  k.Start();

  interrupt.Trigger();

  EventLoop::Default().Run();

  EXPECT_THROW(future.get(), eventuals::StoppedException);
}


TEST_P(HTTPTest, PostInterrupt) {
  std::string scheme = GetParam();

  PostFields fields = {
      {"title", "test"},
      {"body", "message"}};

  auto e = Post(
      scheme + "jsonplaceholder.typicode.com/posts",
      fields);
  auto [future, k] = Terminate(std::move(e));

  Interrupt interrupt;

  k.Register(interrupt);

  k.Start();

  interrupt.Trigger();

  EventLoop::Default().Run();

  EXPECT_THROW(future.get(), eventuals::StoppedException);
}


TEST_P(HTTPTest, GetInterruptAfterStart) {
  std::string scheme = GetParam();

  auto e = Get(scheme + "example.com");
  auto [future, k] = Terminate(std::move(e));

  Interrupt interrupt;

  k.Register(interrupt);

  k.Start();

  // NOTE: now that we've started the continuation 'k' we will have
  // submitted a callback to the event loop and thus by explicitly
  // submitting another callback we will ensure there is a
  // happens-before relationship between starting the transfer
  // and triggering the interrupt.
  EventLoop::Waiter waiter(&EventLoop::Default(), "interrupt.Trigger()");

  EventLoop::Default().Submit(
      [&interrupt]() {
        interrupt.Trigger();
      },
      &waiter);

  EventLoop::Default().Run();

  EXPECT_THROW(future.get(), eventuals::StoppedException);
}


TEST_P(HTTPTest, PostInterruptAfterStart) {
  std::string scheme = GetParam();

  PostFields fields = {
      {"title", "test"},
      {"body", "message"}};

  auto e = Post(
      scheme + "jsonplaceholder.typicode.com/posts",
      fields);
  auto [future, k] = Terminate(std::move(e));

  Interrupt interrupt;

  k.Register(interrupt);

  k.Start();

  // NOTE: now that we've started the continuation 'k' we will have
  // submitted a callback to the event loop and thus by explicitly
  // submitting another callback we will ensure there is a
  // happens-before relationship between starting the transfer
  // and triggering the interrupt.
  EventLoop::Waiter waiter(&EventLoop::Default(), "interrupt.Trigger()");

  EventLoop::Default().Submit(
      [&interrupt]() {
        interrupt.Trigger();
      },
      &waiter);

  EventLoop::Default().Run();

  EXPECT_THROW(future.get(), eventuals::StoppedException);
}
