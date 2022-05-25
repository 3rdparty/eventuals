#include "glog/logging.h"
#include "gtest/gtest.h"

class InstallFailureSignalHandlerEnvironment : public ::testing::Environment {
 public:
  ~InstallFailureSignalHandlerEnvironment() override {}

  void SetUp() override {
    google::InstallFailureSignalHandler();
  }

  void TearDown() override {}
};
