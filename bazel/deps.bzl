"""Dependency specific initialization."""

load("@bazel_tools//tools/build_defs/repo:git.bzl", "git_repository")
load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

load("@com_github_3rdparty_stout_eventuals//bazel:deps.bzl", stout_eventuals_deps="deps")

load("@com_github_3rdparty_stout_borrowed_ptr//bazel:deps.bzl", stout_borrowed_ptr_deps="deps")

load("@com_github_grpc_grpc//bazel:grpc_deps.bzl", "grpc_deps")

def deps(repo_mapping = {}):
    stout_eventuals_deps(
        repo_mapping = repo_mapping
    )

    stout_borrowed_ptr_deps(
        repo_mapping = repo_mapping
    )

    grpc_deps()

    if "com_github_gflags_gflags" not in native.existing_rules():
        http_archive(
            name = "com_github_gflags_gflags",
            url = "https://github.com/gflags/gflags/archive/v2.2.2.tar.gz",
            sha256 = "34af2f15cf7367513b352bdcd2493ab14ce43692d2dcd9dfc499492966c64dcf",
            strip_prefix = "gflags-2.2.2",
        )

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
            url = "https://github.com/3rdparty/stout-notification/archive/0.3.0.tar.gz",
            sha256 = "789af33b1a1ae1682cee6aade0846d50ce6a69773684b8a30633fc97a890b767",
            strip_prefix = "stout-notification-0.3.0",
        )

    if "stout-borrowed-ptr" not in native.existing_rules():
        git_repository(
            name = "stout-borrowed-ptr",
            remote = "https://github.com/3rdparty/stout-borrowed-ptr",
            commit = "1b6e3b1f6c8bbcb4ca1f4d093b901713aa845ff6",
            shallow_since = "1619992476 -0700",
        )

    if "com_github_google_googletest" not in native.existing_rules():
        http_archive(
            name = "com_github_google_googletest",
            url = "https://github.com/google/googletest/archive/release-1.10.0.tar.gz",
            sha256 = "9dc9157a9a1551ec7a7e43daea9a694a0bb5fb8bec81235d8a1e6ef64c716dcb",
            strip_prefix = "googletest-release-1.10.0",
        )
