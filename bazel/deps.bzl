"""Dependency specific initialization."""

load("@bazel_tools//tools/build_defs/repo:git.bzl", "git_repository")
load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")
load("@com_github_3rdparty_eventuals//bazel:deps.bzl", eventuals_deps = "deps")
load("@com_github_3rdparty_stout_borrowed_ptr//bazel:deps.bzl", stout_borrowed_ptr_deps = "deps")
load("@com_github_3rdparty_stout_notification//bazel:deps.bzl", stout_notification_deps = "deps")
load("@com_github_grpc_grpc//bazel:grpc_deps.bzl", "grpc_deps")
load("@com_github_reboot_dev_pyprotoc_plugin//bazel:deps.bzl", pyprotoc_plugin_deps = "deps")

def deps(repo_mapping = {}):
    """Propagate all dependencies.

    Args:
        repo_mapping: Passed through to all other functions that expect/use
            repo_mapping, e.g., 'git_repository'
    """

    eventuals_deps(
        repo_mapping = repo_mapping,
    )

    stout_borrowed_ptr_deps(
        repo_mapping = repo_mapping,
    )

    stout_notification_deps(
        repo_mapping = repo_mapping,
    )

    pyprotoc_plugin_deps(
        repo_mapping = repo_mapping,
    )

    # !!! Here be dragons !!!
    # grpc is currently (2021/09/06) pulling in a version of absl and boringssl
    # that does not compile on linux with neither gcc (11.1) nor clang (12.0).
    # Here we are front running the dependency loading of grpc to pull
    # compatible versions.
    #
    # First of absl:
    if "com_google_absl" not in native.existing_rules():
        http_archive(
            name = "com_google_absl",
            url = "https://github.com/abseil/abseil-cpp/archive/refs/tags/20210324.2.tar.gz",
            strip_prefix = "abseil-cpp-20210324.2",
            sha256 = "59b862f50e710277f8ede96f083a5bb8d7c9595376146838b9580be90374ee1f",
        )

    # and then boringssl
    if "boringssl" not in native.existing_rules():
        git_repository(
            name = "boringssl",
            commit = "fc44652a42b396e1645d5e72aba053349992136a",
            remote = "https://boringssl.googlesource.com/boringssl",
            shallow_since = "1627579704 +0000",
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

    if "com_github_google_googletest" not in native.existing_rules():
        http_archive(
            name = "com_github_google_googletest",
            url = "https://github.com/google/googletest/archive/release-1.10.0.tar.gz",
            sha256 = "9dc9157a9a1551ec7a7e43daea9a694a0bb5fb8bec81235d8a1e6ef64c716dcb",
            strip_prefix = "googletest-release-1.10.0",
        )
