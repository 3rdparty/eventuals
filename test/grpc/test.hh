#pragma once

#include <filesystem>

namespace eventuals::grpc::test {

// Helper which returns a path for the specified runfile. This is a
// wrapper around 'bazel::tools::cpp::runfiles::Runfiles' which
// amongst other things uses 'std::filesystem::path' instead of just
// 'std::string'.
std::filesystem::path GetRunfilePathFor(const std::filesystem::path& runfile);

} // namespace eventuals::grpc::test
