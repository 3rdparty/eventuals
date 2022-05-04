"""Adds external repositories/archives."""

load("@bazel_tools//tools/build_defs/repo:git.bzl", "git_repository")
load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")
load("@bazel_tools//tools/build_defs/repo:utils.bzl", "maybe")
load("@com_github_3rdparty_stout_atomic_backoff//bazel:repos.bzl", stout_atomic_backoff_repos = "repos")
load("@com_github_3rdparty_stout_stateful_tally//bazel:repos.bzl", stout_stateful_tally = "repos")

# buildifier: disable=out-of-order-load
load("@com_github_3rdparty_stout_borrowed_ptr//bazel:repos.bzl", stout_borrowed_ptr_repos = "repos")
load("@com_github_3rdparty_stout_flags//bazel:repos.bzl", stout_flags_repos = "repos")

# buildifier: disable=out-of-order-load
load("@com_github_3rdparty_stout_notification//bazel:repos.bzl", stout_notification_repos = "repos")

# buildifier: disable=out-of-order-load
load("@com_github_3rdparty_stout//bazel:repos.bzl", stout_repos = "repos")
load("@com_github_reboot_dev_pyprotoc_plugin//bazel:repos.bzl", pyprotoc_plugin_repos = "repos")

def repos(external = True, repo_mapping = {}):
    """Adds external repositories/archives needed by eventuals

    Args:
        external: whether or not we're invoking this function as though
            we're an external dependency
        repo_mapping: passed through to all other functions that expect/use
            repo_mapping, e.g., 'git_repository'
    """

    # Avoid buildifier warning about unused `external` variable.
    _ = external  # @unused

    stout_atomic_backoff_repos()

    stout_stateful_tally()

    stout_borrowed_ptr_repos()

    stout_flags_repos()

    stout_notification_repos()

    stout_repos()

    pyprotoc_plugin_repos()

    #Loading rules_foreign_cc repo.
    maybe(
        http_archive,
        name = "rules_foreign_cc",
        url = "https://github.com/bazelbuild/rules_foreign_cc/archive/0.5.1.tar.gz",
        sha256 = "33a5690733c5cc2ede39cb62ebf89e751f2448e27f20c8b2fbbc7d136b166804",
        strip_prefix = "rules_foreign_cc-0.5.1",
        repo_mapping = repo_mapping,
    )

    # Loading asio repo. In future we gonna add script which checks
    # for updated sha/commit if some repo had any changes.
    maybe(
        git_repository,
        name = "com_github_3rdparty_bazel_rules_asio",
        remote = "https://github.com/3rdparty/bazel-rules-asio",
        commit = "257c93cbaf94703f1b0668b7693267bebea52b37",
        shallow_since = "1650559794 +0200",
        repo_mapping = repo_mapping,
    )

    # Loading curl repo.
    maybe(
        git_repository,
        name = "com_github_3rdparty_bazel_rules_curl",
        remote = "https://github.com/3rdparty/bazel-rules-curl",
        commit = "5748da4b2594fab9410db9b5e6619b47cb5688e0",
        shallow_since = "1651700487 +0300",
        repo_mapping = repo_mapping,
    )

    # Loading jemalloc repo.
    maybe(
        git_repository,
        name = "com_github_3rdparty_bazel_rules_jemalloc",
        remote = "https://github.com/3rdparty/bazel-rules-jemalloc",
        commit = "c82c0c3856f07d53c1b76e89beeb8abab8c2d0ad",
        shallow_since = "1634918242 -0700",
        repo_mapping = repo_mapping,
    )

    # Loading libuv repo.
    maybe(
        git_repository,
        name = "com_github_3rdparty_bazel_rules_libuv",
        remote = "https://github.com/3rdparty/bazel-rules-libuv",
        commit = "f8aeba82e40cda94d6227c67d114ecc732b30be5",
        shallow_since = "1638359550 +0300",
        repo_mapping = repo_mapping,
    )

    # Loading grpc repo.
    maybe(
        http_archive,
        name = "com_github_grpc_grpc",
        urls = ["https://github.com/grpc/grpc/archive/refs/tags/v1.45.0.tar.gz"],
        strip_prefix = "grpc-1.45.0",
        sha256 = "ec19657a677d49af59aa806ec299c070c882986c9fcc022b1c22c2a3caf01bcd",
    )

    # Loading skylib repo.
    maybe(
        http_archive,
        name = "bazel_skylib",
        urls = [
            "https://mirror.bazel.build/github.com/bazelbuild/bazel-skylib/releases/download/1.2.1/bazel-skylib-1.2.1.tar.gz",
            "https://github.com/bazelbuild/bazel-skylib/releases/download/1.2.1/bazel-skylib-1.2.1.tar.gz",
        ],
        sha256 = "f7be3474d42aae265405a592bb7da8e171919d74c16f082a5457840f06054728",
    )
