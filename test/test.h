#pragma once

#include <filesystem>

#include "eventuals/grpc/server.h"
#include "gtest/gtest.h"

////////////////////////////////////////////////////////////////////////

class EventualsGrpcTest : public ::testing::Test {
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

////////////////////////////////////////////////////////////////////////

// Helper which returns a path for the specified runfile. This is a
// wrapper around 'bazel::tools::cpp::runfiles::Runfiles' which
// amongst other things uses 'std::filesystem::path' instead of just
// 'std::string'.
std::filesystem::path GetRunfilePathFor(const std::filesystem::path& runfile);

////////////////////////////////////////////////////////////////////////
