"""Bind all submodules needed for eventuals."""

load("@bazel_tools//tools/build_defs/repo:utils.bzl", "maybe")
load("@com_github_3rdparty_eventuals//bazel:submodule_repository.bzl", "submodule_repository")

def submodules(external = True):
    """Creates repositories for each submodule.

    Args:
          external: whether or not we're invoking this function as though
            though we're an external dependency
    """
    maybe(
        submodule_repository,
        name = "com_github_reboot_dev_pyprotoc_plugin",
        path = "submodules/pyprotoc-plugin",
        external = external,
    )

    maybe(
        submodule_repository,
        name = "com_github_3rdparty_stout",
        path = "submodules/stout",
        external = external,
    )

    maybe(
        submodule_repository,
        name = "com_github_3rdparty_bazel_rules_jemalloc",
        path = "submodules/bazel-rules-jemalloc",
        external = external,
    )
