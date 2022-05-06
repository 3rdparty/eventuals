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
        name = "com_github_3rdparty_stout_atomic_backoff",
        path = "submodules/stout/stout-atomic-backoff",
        external = external,
    )

    submodule_repository(
        name = "com_github_3rdparty_stout_stateful_tally",
        path = "submodules/stout/stout-stateful-tally",
        external = external,
    )

    submodule_repository(
        name = "com_github_3rdparty_stout_borrowed_ptr",
        path = "submodules/stout/stout-borrowed-ptr",
        external = external,
    )

    submodule_repository(
        name = "com_github_3rdparty_stout_flags",
        path = "submodules/stout/stout-flags",
        external = external,
    )

    submodule_repository(
        name = "com_github_3rdparty_stout_notification",
        path = "submodules/stout/stout-notification",
        external = external,
    )

    submodule_repository(
        name = "com_github_3rdparty_stout",
        path = "submodules/stout",
        external = external,
    )
