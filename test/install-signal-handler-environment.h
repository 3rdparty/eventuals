#include <iostream>

#include "glog/logging.h"
#include "gtest/gtest.h"

class InstallSignalHandlerEnvironment : public ::testing::Environment {
 public:
  ~InstallSignalHandlerEnvironment() override {}

  void SetUp() override {
    std::cout << "hello" << std::endl;
    google::InstallFailureSignalHandler();
  }

  void TearDown() override {}

 private:
  static testing::Environment* const environment_;
};
