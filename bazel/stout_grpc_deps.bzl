"""Load dependencies needed to compile and test the library as a 3rd-party consumer."""

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

def stout_grpc_deps():
  # NOTE: glog references gflags but since grpc depends on gflags we
  # don't need to explicitly add them here. If you don't include grpc
  # as we do in WORKSPACE.bazel then you'll need to add gflags
  # yourself _before_ glog.
  if "com_github_google_glog" not in native.existing_rules():
    http_archive(
        name = "com_github_google_glog",
        url = "https://github.com/google/glog/archive/v0.4.0.tar.gz",
        sha256 = "f28359aeba12f30d73d9e4711ef356dc842886968112162bc73002645139c39c",
        strip_prefix = "glog-0.4.0",
    )

  if "com_google_absl" not in native.existing_rules():
    http_archive(
        name = "com_google_absl",
        url = "https://github.com/abseil/abseil-cpp/archive/ca9856cabc23d771bcce634677650eb6fc4363ae.tar.gz",
        sha256 = "cd477bfd0d19f803f85d118c7943b7908930310d261752730afa981118fee230",
        strip_prefix = "abseil-cpp-ca9856cabc23d771bcce634677650eb6fc4363ae",
    )

  if "stout-notification" not in native.existing_rules():
    http_archive(
        name = "stout-notification",
        url = "https://github.com/3rdparty/stout-notification/archive/0.2.0.tar.gz",
        sha256 = "613dd13d1cf86d095da4d62e25e873a1365f1aec353521dc1c7068c83eee3ab3",
        strip_prefix = "stout-notification-0.2.0",
    )

  if "stout-borrowed-ptr" not in native.existing_rules():
    http_archive(
        name = "stout-borrowed-ptr",
        url = "https://github.com/3rdparty/stout-borrowed-ptr/archive/0.2.0.tar.gz",
        sha256 = "ae9a0b124922a3cf404e4f3acc6011e2fa46697b7923304e37b3ec48f3a908b1",
        strip_prefix = "stout-borrowed-ptr-0.2.0",
    )