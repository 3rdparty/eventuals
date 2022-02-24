#include "gtest/gtest.h"
#include "test/test.h"
#include "tools/cpp/runfiles/runfiles.h"

////////////////////////////////////////////////////////////////////////

// NOTE: using a raw pointer here as per Google C++ Style Guide
// because 'bazel::tools::cpp::runfiles::Runfiles' is not trivially
// destructible.
static bazel::tools::cpp::runfiles::Runfiles* runfiles = nullptr;

////////////////////////////////////////////////////////////////////////

// Declared in test.h.
std::filesystem::path GetRunfilePathFor(const std::filesystem::path& runfile) {
  std::string path = CHECK_NOTNULL(runfiles)->Rlocation(runfile);

  CHECK(!path.empty()) << "runfile " << runfile << " does not exist";

  return path;
}

////////////////////////////////////////////////////////////////////////

int main(int argc, char** argv) {
  std::string error;

  // NOTE: using 'Create()' instead of 'CreateForTest()' so that we
  // can support both running the test via 'bazel test' or explicitly
  // (i.e., ./path/to/test --gtest_...).
  runfiles = bazel::tools::cpp::runfiles::Runfiles::Create(
      argv[0],
      std::string(),
      std::filesystem::absolute(argv[0]).parent_path().string(),
      &error);

  if (runfiles == nullptr) {
    std::cerr
        << "Failed to construct 'Runfiles' necessary for getting "
        << "paths to assets needed in order to run tests: "
        << error << std::endl;
    return -1;
  }

  ::testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}

////////////////////////////////////////////////////////////////////////
