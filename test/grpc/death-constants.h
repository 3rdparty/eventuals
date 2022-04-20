// Shared constants used by tests that run binaries that intentionally die.

namespace eventuals::grpc {
// The exit code issued by a process when dying intentionally. The code number
// is an arbitrary value that is unlikely to be produced by any other type of
// unexpected failure (e.g. a CHECK failure). This lets us be confident that
// tests that run binaries that die with this exit code are dying in an
// expected way and location.
constexpr int kProcessIntentionalExitCode = 17;
} // namespace eventuals::grpc
