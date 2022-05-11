"""Bind all submodules needed for eventuals."""

load("@com_github_3rdparty_eventuals//bazel:submodule_repository.bzl", "submodule_repository")

def submodules(external):
    """Creates repositories for each submodule.

    Args:
          external: whether or not we're invoking this function as though
            though we're an external dependency
    """
    submodule_repository(
        name = "com_github_reboot_dev_pyprotoc_plugin",
        path = "submodules/pyprotoc-plugin",
        external = external,
    )

    submodule_repository(
        name = "com_github_3rdparty_stout",
        path = "submodules/stout",
        external = external,
    )
