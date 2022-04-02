"""Adds repositories/archives."""

########################################################################
# DO NOT EDIT THIS FILE unless you are inside the
# https://github.com/reboot-dev/pyprotoc-plugin repository. If you
# encounter it anywhere else it is because it has been copied there in
# order to simplify adding transitive dependencies. If you want a
# different version of pyprotoc-plugin follow the Bazel build
# instructions at https://github.com/reboot-dev/pyprotoc-plugin.
########################################################################

load("@bazel_tools//tools/build_defs/repo:git.bzl", "git_repository")
load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

def repos(external = True, repo_mapping = {}):
    http_archive(
        name = "rules_python",
        url = "https://github.com/bazelbuild/rules_python/releases/download/0.5.0/rules_python-0.5.0.tar.gz",
        sha256 = "cd6730ed53a002c56ce4e2f396ba3b3be262fd7cb68339f0377a45e8227fe332",
        repo_mapping = repo_mapping,
    )

    if external and "com_github_reboot_dev_pyprotoc_plugin" not in native.existing_rules():
        git_repository(
            name = "com_github_reboot_dev_pyprotoc_plugin",
            remote = "https://github.com/reboot-dev/pyprotoc-plugin",
            commit = "a0efa3f2517e58d8c0fca897dbe7a1233b178c07",
            shallow_since = "1648863256 +0000",
            repo_mapping = repo_mapping,
        )
