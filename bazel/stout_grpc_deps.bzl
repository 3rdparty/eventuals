"""Load dependencies needed to compile and test the library as a 3rd-party consumer."""

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")
load("@bazel_tools//tools/build_defs/repo:git.bzl", "git_repository")

def stout_grpc_deps():
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
    git_repository(
        name = "stout-notification",
        remote = "https://github.com/benh/stout-notification.git",
        commit = "69b1e4cefb823187bcceb5e548ef306178d5cf89",
    )

  if "stout-borrowed-ptr" not in native.existing_rules():
    git_repository(
        name = "stout-borrowed-ptr",
        remote = "https://github.com/benh/stout-borrowed-ptr.git",
        commit = "47ad24d5f0275608012e5ff41a782e9e993a8272",
    )