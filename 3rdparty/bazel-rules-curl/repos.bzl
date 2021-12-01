"""Adds repostories/archives."""

########################################################################
# DO NOT EDIT THIS FILE unless you are inside the
# https://github.com/3rdparty/bazel-rules-curl repository. If you
# encounter it anywhere else it is because it has been copied there in
# order to simplify adding transitive dependencies. If you want a
# different version of bazel-rules-curl follow the Bazel build
# instructions at https://github.com/3rdparty/bazel-rules-curl.
########################################################################

load("@bazel_tools//tools/build_defs/repo:git.bzl", "git_repository")
load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")
load("@bazel_tools//tools/build_defs/repo:utils.bzl", "maybe")

def repos(external = True, repo_mapping = {}):
    maybe(
        http_archive,
        name = "rules_foreign_cc",
        url = "https://github.com/bazelbuild/rules_foreign_cc/archive/0.5.1.tar.gz",
        sha256 = "33a5690733c5cc2ede39cb62ebf89e751f2448e27f20c8b2fbbc7d136b166804",
        strip_prefix = "rules_foreign_cc-0.5.1",
        repo_mapping = repo_mapping,
    )

    if external:
        maybe(
            git_repository,
            name = "com_github_3rdparty_bazel_rules_curl",
            remote = "https://github.com/3rdparty/bazel-rules-curl",
            commit = "a5a399e4615fd4a6bb5c547d9831d4d97ed47a3f",
            shallow_since = "1633213624 +0300",
            repo_mapping = repo_mapping,
        )
