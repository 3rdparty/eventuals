#include "glog/logging.h"
#include "gtest/gtest.h"

class SignalHandlerEnvironment : public ::testing::Environment {
 public:
  ~SignalHandlerEnvironment() override {}

  void SetUp() override {
    google::InstallFailureSignalHandler();
  }

  void TearDown() override {}
};
