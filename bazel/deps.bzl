"""Dependency specific initialization."""

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

load("@com_github_3rdparty_bazel_rules_libuv//bazel:deps.bzl", libuv_deps="deps")

load("@com_github_3rdparty_bazel_rules_libcurl//bazel:deps.bzl", libcurl_deps="deps")

def deps(repo_mapping = {}):
    libuv_deps(
        repo_mapping = repo_mapping
    )
    
    libcurl_deps(
    	repo_mapping = repo_mapping
    )

    if "com_github_gflags_gflags" not in native.existing_rules():
        http_archive(
            name = "com_github_gflags_gflags",
            url = "https://github.com/gflags/gflags/archive/v2.2.2.tar.gz",
            sha256 = "34af2f15cf7367513b352bdcd2493ab14ce43692d2dcd9dfc499492966c64dcf",
            strip_prefix = "gflags-2.2.2",
            repo_mapping = repo_mapping,
        )

    if "com_github_google_glog" not in native.existing_rules():
        # NOTE: using glog version 0.5.0 since older versions failed
        # to compile on Windows, see:
        # https://github.com/google/glog/issues/472
        http_archive(
            name = "com_github_google_glog",
            url = "https://github.com/google/glog/archive/refs/tags/v0.5.0.tar.gz",
            sha256 = "eede71f28371bf39aa69b45de23b329d37214016e2055269b3b5e7cfd40b59f5",
            strip_prefix = "glog-0.5.0",
            repo_mapping = repo_mapping,
        )

    if "com_github_google_googletest" not in native.existing_rules():
        http_archive(
            name = "com_github_google_googletest",
            url = "https://github.com/google/googletest/archive/release-1.10.0.tar.gz",
            sha256 = "9dc9157a9a1551ec7a7e43daea9a694a0bb5fb8bec81235d8a1e6ef64c716dcb",
            strip_prefix = "googletest-release-1.10.0",
            repo_mapping = repo_mapping,
        )
