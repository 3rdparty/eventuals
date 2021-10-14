#pragma once

#include "gtest/gtest.h"
#include "stout/catch.h"
#include "stout/grpc/server.h"
#include "stout/just.h"
#include "stout/loop.h"
#include "stout/map.h"
#include "stout/then.h"

class StoutGrpcTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_EQ(1, GetThreadCount());
  }

  void TearDown() override {
    // NOTE: need to wait until all internal threads created by the
    // grpc library have completed because some of our tests are death
    // tests which fork.
    while (GetThreadCount() != 1) {}
  }

  size_t GetThreadCount() {
    // TODO(benh): Don't rely on the internal 'GetThreadCount()'.
    return testing::internal::GetThreadCount();
  }
};


// TODO(benh): Move to stout-stringify.
template <typename T>
std::string stringify(const T& t) {
  std::ostringstream out;
  out << t;
  if (!out.good()) {
    std::cerr << "Failed to stringify!" << std::endl;
    abort();
  }
  return out.str();
}


// Helper that does the writing and finishing for a unary call as well
// as catching failures and handling appropriately.
template <typename Request, typename Response>
auto UnaryEpilogue(
    stout::eventuals::grpc::ServerCall<Request, Response>& call) {
  return stout::eventuals::Then([&](auto&& response) {
           return call.Writer().WriteLast(
               std::forward<decltype(response)>(response));
         })
      | stout::eventuals::Just(::grpc::Status::OK)
      | stout::eventuals::Catch([&](auto&&...) {
           call.context()->TryCancel();
           return stout::eventuals::Just(
               ::grpc::Status(::grpc::UNKNOWN, "error"));
         })
      | stout::eventuals::Then([&](auto&& status) {
           return call.Finish(status)
               | call.WaitForDone();
         });
}


// Helper that does the writing and finishing for a server streaming
// call as well as catching failures and handling appropriately.
template <typename Request, typename Response>
auto StreamingEpilogue(
    stout::eventuals::grpc::ServerCall<Request, Response>& call) {
  return stout::eventuals::Map(stout::eventuals::Then([&](auto&& response) {
           return call.Writer().Write(response);
         }))
      | stout::eventuals::Loop()
      | stout::eventuals::Just(::grpc::Status::OK)
      | stout::eventuals::Catch([&](auto&&...) {
           call.context()->TryCancel();
           return stout::eventuals::Just(
               ::grpc::Status(::grpc::UNKNOWN, "error"));
         })
      | stout::eventuals::Then([&](auto&& status) {
           return call.Finish(status)
               | call.WaitForDone();
         });
}
