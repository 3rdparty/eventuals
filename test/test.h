#pragma once

#include "gtest/gtest.h"
#include "stout/grpc/server.h"

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
