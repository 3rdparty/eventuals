#include "eventuals/http2.h"

#include "event-loop-test.h"
#include "eventuals/eventual.h"
#include "eventuals/interrupt.h"
#include "eventuals/terminal.h"
#include "gtest/gtest.h"

using eventuals::EventLoop;
using eventuals::Eventual;
using eventuals::Interrupt;
using eventuals::Terminate;
using eventuals::Undefined;

using eventuals::http::Client;
using eventuals::http::Get;
using eventuals::http::Method;
using eventuals::http::Post;
using eventuals::http::Request;

struct HTTPTest
  : public EventLoopTest,
    public ::testing::WithParamInterface<const char*> {
  struct SchemePrettyPrinter {
    template <class ParamType>
    std::string operator()(
        const testing::TestParamInfo<ParamType>& info) const {
      return std::string(info.param);
    }
  };
};

// NOTE: we don't compile https tests on Windows
// because we currently can't reliably compile
// either OpenSSL or boringssl on Windows (see #59).
#ifdef _WIN32
const char* schemes[] = {"http"};
#else
const char* schemes[] = {"http", "https"};
#endif

INSTANTIATE_TEST_SUITE_P(
    Schemes,
    HTTPTest,
    testing::ValuesIn(schemes),
    HTTPTest::SchemePrettyPrinter());

// Current tests implementation rely on the transfers not
// being able to complete within a very short period.
// TODO(folming): Use HTTP mock server to not rely on external hosts.

/*
TEST_P(HTTPTest, GetSuccess) {
  std::string scheme = GetParam() + std::string("://");

  auto e = Get(scheme + "example.com");
  auto [future, k] = Terminate(std::move(e));
  k.Start();

  EventLoop::Default().Run();

  future.get();
}
*/

TEST_P(HTTPTest, GetNoURIFail) {
  std::string scheme = GetParam() + std::string("://");

  auto client = Client::Default();
  Request<
      Undefined,
      Method,
      Undefined,
      Undefined,
      Undefined,
      Undefined>
      request{Undefined(), Method::GET};

  auto e = client.Do(request);
  auto [future, k] = Terminate(std::move(e));
  k.Start();

  EventLoop::Default().Run();

  EXPECT_THROW(future.get(), const char*);
}

/*
TEST_P(HTTPTest, PostSuccess) {
  //std::string scheme = GetParam() + std::string("://");
  const char some_data[] = "Dumped some stuff :)!";

  auto e = Post(
      "http://ptsv2.com/t/c7pvj-1641853263/post",
      "text/plain",
      some_data,
      strlen(some_data) + 1);
  auto [future, k] = Terminate(std::move(e));
  k.Start();

  EventLoop::Default().Run();

  auto response = future.get();
  std::cout << response.code << std::endl;
  std::cout << response.headers << std::endl;
  std::cout << response.body << std::endl;
}
*/