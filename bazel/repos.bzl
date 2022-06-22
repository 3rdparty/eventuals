"""Adds repositories/archives."""

load("@bazel_tools//tools/build_defs/repo:git.bzl", "git_repository")
load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")
load("@bazel_tools//tools/build_defs/repo:utils.bzl", "maybe")
load("@com_github_3rdparty_stout//bazel:repos.bzl", stout_repos = "repos")
load("@com_github_reboot_dev_pyprotoc_plugin//bazel:repos.bzl", pyprotoc_plugin_repos = "repos")

def repos(repo_mapping = {}):
    """Adds external repositories/archives needed by eventuals (phase 1).

    Args:
        repo_mapping: passed through to all other functions that expect/use
            repo_mapping, e.g., 'git_repository'
    """

    stout_repos(
        external = False,
        repo_mapping = repo_mapping,
    )

    pyprotoc_plugin_repos(
        external = False,
        repo_mapping = repo_mapping,
    )

    maybe(
        http_archive,
        name = "rules_foreign_cc",
        url = "https://github.com/bazelbuild/rules_foreign_cc/archive/0.8.0.tar.gz",
        sha256 = "6041f1374ff32ba711564374ad8e007aef77f71561a7ce784123b9b4b88614fc",
        strip_prefix = "rules_foreign_cc-0.8.0",
        repo_mapping = repo_mapping,
    )

    maybe(
        git_repository,
        name = "com_github_3rdparty_bazel_rules_asio",
        remote = "https://github.com/3rdparty/bazel-rules-asio",
        commit = "257c93cbaf94703f1b0668b7693267bebea52b37",
        shallow_since = "1650559794 +0200",
        repo_mapping = repo_mapping,
    )

    maybe(
        git_repository,
        name = "com_github_3rdparty_bazel_rules_curl",
        remote = "https://github.com/3rdparty/bazel-rules-curl",
        commit = "5748da4b2594fab9410db9b5e6619b47cb5688e0",
        shallow_since = "1651700487 +0300",
        repo_mapping = repo_mapping,
    )

    maybe(
        git_repository,
        name = "com_github_3rdparty_bazel_rules_jemalloc",
        remote = "https://github.com/3rdparty/bazel-rules-jemalloc",
        commit = "c82c0c3856f07d53c1b76e89beeb8abab8c2d0ad",
        shallow_since = "1634918242 -0700",
        repo_mapping = repo_mapping,
    )

    maybe(
        git_repository,
        name = "com_github_3rdparty_bazel_rules_libuv",
        remote = "https://github.com/3rdparty/bazel-rules-libuv",
        commit = "f8aeba82e40cda94d6227c67d114ecc732b30be5",
        shallow_since = "1638359550 +0300",
        repo_mapping = repo_mapping,
    )

    maybe(
        git_repository,
        name = "com_github_3rdparty_bazel_rules_tl_expected",
        remote = "https://github.com/3rdparty/bazel-rules-expected",
        commit = "c703632657bf4ec9177d9aea0447166d424b3b74",
        shallow_since = "1654243887 +0300",
        repo_mapping = repo_mapping,
    )

    maybe(
        http_archive,
        name = "com_github_grpc_grpc",
        urls = ["https://github.com/grpc/grpc/archive/refs/tags/v1.45.0.tar.gz"],
        strip_prefix = "grpc-1.45.0",
        sha256 = "ec19657a677d49af59aa806ec299c070c882986c9fcc022b1c22c2a3caf01bcd",
    )

    maybe(
        http_archive,
        name = "bazel_skylib",
        urls = [
            "https://mirror.bazel.build/github.com/bazelbuild/bazel-skylib/releases/download/1.2.1/bazel-skylib-1.2.1.tar.gz",
            "https://github.com/bazelbuild/bazel-skylib/releases/download/1.2.1/bazel-skylib-1.2.1.tar.gz",
        ],
        sha256 = "f7be3474d42aae265405a592bb7da8e171919d74c16f082a5457840f06054728",
    )
