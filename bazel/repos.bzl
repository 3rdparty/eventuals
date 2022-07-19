"""Adds repositories/archives."""

load("@bazel_tools//tools/build_defs/repo:git.bzl", "git_repository", "new_git_repository")
load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")
load("@bazel_tools//tools/build_defs/repo:utils.bzl", "maybe")
load("@com_github_3rdparty_bazel_rules_jemalloc//bazel:repos.bzl", jemalloc_repos = "repos")
load("@com_github_3rdparty_stout//bazel:repos.bzl", stout_repos = "repos")
load("@com_github_reboot_dev_pyprotoc_plugin//bazel:repos.bzl", pyprotoc_plugin_repos = "repos")

def repos(repo_mapping = {}):
    """Adds external repositories/archives needed by eventuals (phase 1).

    Args:
        repo_mapping: passed through to all other functions that expect/use
            repo_mapping, e.g., 'git_repository'
    """

    jemalloc_repos(
        external = False,
        repo_mapping = repo_mapping,
    )

    stout_repos(
        external = False,
        repo_mapping = repo_mapping,
    )

    pyprotoc_plugin_repos(
        external = False,
        repo_mapping = repo_mapping,
    )

    # Hedron's Compile Commands Extractor for Bazel.
    # Latest version available on 2022/07/18.
    # Follow the link to learn how to set it up for your code editor:
    # https://github.com/hedronvision/bazel-compile-commands-extractor
    maybe(
        git_repository,
        name = "hedron_compile_commands",
        commit = "05610f52a2ea3cda6ac27133b96f71c36358adf9",
        remote = "https://github.com/hedronvision/bazel-compile-commands-extractor",
        shallow_since = "1657677319 -0700",
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
        urls = ["https://github.com/grpc/grpc/archive/refs/tags/v1.46.3.tar.gz"],
        strip_prefix = "grpc-1.46.3",
        sha256 = "d6cbf22cb5007af71b61c6be316a79397469c58c82a942552a62e708bce60964",
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

    maybe(
        git_repository,
        name = "com_github_3rdparty_bazel_rules_backward_cpp",
        remote = "https://github.com/3rdparty/bazel-rules-backward-cpp",
        commit = "be06f4fce52e2327d877493d050f42b9d082af3f",
        shallow_since = "1658132298 +0300",
        repo_mapping = repo_mapping,
    )

    maybe(
        new_git_repository,
        name = "com_github_3rdparty_bazel_rules_backward_cpp_stacktrace",
        remote = "https://github.com/3rdparty/bazel-rules-backward-cpp",
        build_file = "@com_github_3rdparty_bazel_rules_backward_cpp//:BUILD.backward-stacktrace.bazel",
        commit = "be06f4fce52e2327d877493d050f42b9d082af3f",
        shallow_since = "1658132298 +0300",
    )
