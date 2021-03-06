"""Dependency specific initialization."""

load("@bazel_skylib//:workspace.bzl", "bazel_skylib_workspace")
load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")
load("@bazel_tools//tools/build_defs/repo:utils.bzl", "maybe")
load("@com_github_3rdparty_bazel_rules_asio//bazel:deps.bzl", asio_deps = "deps")
load("@com_github_3rdparty_bazel_rules_backward_cpp//bazel:deps.bzl", backward_deps = "deps")
load("@com_github_3rdparty_bazel_rules_curl//bazel:deps.bzl", curl_deps = "deps")
load("@com_github_3rdparty_bazel_rules_jemalloc//bazel:deps.bzl", jemalloc_deps = "deps")
load("@com_github_3rdparty_bazel_rules_libuv//bazel:deps.bzl", libuv_deps = "deps")
load("@com_github_3rdparty_bazel_rules_tl_expected//bazel:deps.bzl", expected_deps = "deps")
load("@com_github_3rdparty_stout//bazel:deps.bzl", stout_deps = "deps")
load("@com_github_grpc_grpc//bazel:grpc_deps.bzl", "grpc_deps")
load("@com_github_reboot_dev_pyprotoc_plugin//bazel:deps.bzl", pyprotoc_plugin_deps = "deps")
load("@com_google_protobuf//:protobuf_deps.bzl", "protobuf_deps")
load("@hedron_compile_commands//:workspace_setup.bzl", "hedron_compile_commands_setup")

def deps(repo_mapping = {}):
    """Adds external repositories/archives needed by eventuals (phase 2).

    Args:
        repo_mapping: passed through to all other functions that expect/use
            repo_mapping, e.g., 'git_repository'
    """
    hedron_compile_commands_setup()

    asio_deps(
        repo_mapping = repo_mapping,
    )

    backward_deps(
        repo_mapping = repo_mapping,
    )

    bazel_skylib_workspace()

    curl_deps(
        repo_mapping = repo_mapping,
    )

    expected_deps(
        repo_mapping = repo_mapping,
    )

    jemalloc_deps(
        repo_mapping = repo_mapping,
    )

    libuv_deps(
        repo_mapping = repo_mapping,
    )

    maybe(
        http_archive,
        name = "platforms",
        urls = [
            "https://mirror.bazel.build/github.com/bazelbuild/platforms/releases/download/0.0.5/platforms-0.0.5.tar.gz",
            "https://github.com/bazelbuild/platforms/releases/download/0.0.5/platforms-0.0.5.tar.gz",
        ],
        sha256 = "379113459b0feaf6bfbb584a91874c065078aa673222846ac765f86661c27407",
        repo_mapping = repo_mapping,
    )

    maybe(
        http_archive,
        name = "com_github_gflags_gflags",
        url = "https://github.com/gflags/gflags/archive/v2.2.2.tar.gz",
        sha256 = "34af2f15cf7367513b352bdcd2493ab14ce43692d2dcd9dfc499492966c64dcf",
        strip_prefix = "gflags-2.2.2",
        repo_mapping = repo_mapping,
    )

    # NOTE: using glog version 0.5.0 since older versions failed
    # to compile on Windows, see:
    # https://github.com/google/glog/issues/472
    maybe(
        http_archive,
        name = "com_github_google_glog",
        url = "https://github.com/google/glog/archive/refs/tags/v0.5.0.tar.gz",
        sha256 = "eede71f28371bf39aa69b45de23b329d37214016e2055269b3b5e7cfd40b59f5",
        strip_prefix = "glog-0.5.0",
        repo_mapping = repo_mapping,
    )

    maybe(
        http_archive,
        name = "com_github_google_googletest",
        url = "https://github.com/google/googletest/archive/release-1.11.0.tar.gz",
        sha256 = "b4870bf121ff7795ba20d20bcdd8627b8e088f2d1dab299a031c1034eddc93d5",
        strip_prefix = "googletest-release-1.11.0",
        repo_mapping = repo_mapping,
    )

    maybe(
        http_archive,
        name = "com_google_absl",
        url = "https://github.com/abseil/abseil-cpp/archive/refs/tags/20211102.0.tar.gz",
        strip_prefix = "abseil-cpp-20211102.0",
        sha256 = "dcf71b9cba8dc0ca9940c4b316a0c796be8fab42b070bb6b7cab62b48f0e66c4",
        repo_mapping = repo_mapping,
    )

    stout_deps(
        repo_mapping = repo_mapping,
    )

    pyprotoc_plugin_deps(
        repo_mapping = repo_mapping,
    )

    protobuf_deps()

    grpc_deps()
