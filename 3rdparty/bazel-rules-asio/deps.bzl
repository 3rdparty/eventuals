"""
"""

load("@bazel_tools//tools/build_defs/repo:git.bzl", "new_git_repository")
load("@bazel_tools//tools/build_defs/repo:utils.bzl", "maybe")

def deps(repo_mapping = {}):
    maybe(
        new_git_repository,
        name = "com_github_chriskohlhoff_asio",
        remote = "https://github.com/chriskohlhoff/asio",
        commit = "51d74ca77c81eaa98856804b71c72c31baf6edb2",
        shallow_since = "1637099264 +1100",
        repo_mapping = repo_mapping,
        build_file = "//3rdparty/bazel-rules-asio:BUILD.bazel",
    )
