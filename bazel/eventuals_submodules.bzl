"""Bind all submodules needed for eventuals."""

load("@com_github_3rdparty_eventuals//bazel:submodule_repository.bzl", "submodule_repository")

def eventuals_submodules():
    """Creating symlinks to specific submodule's path."""

    submodule_repository(
        name = "com_github_reboot_dev_pyprotoc_plugin",
        path = "submodules/pyprotoc-plugin",
    )

    submodule_repository(
        name = "com_github_3rdparty_stout_atomic_backoff",
        path = "submodules/stout/stout-atomic-backoff",
    )

    submodule_repository(
        name = "com_github_3rdparty_stout_stateful_tally",
        path = "submodules/stout/stout-stateful-tally",
    )

    submodule_repository(
        name = "com_github_3rdparty_stout_borrowed_ptr",
        path = "submodules/stout/stout-borrowed-ptr",
    )

    submodule_repository(
        name = "com_github_3rdparty_stout_flags",
        path = "submodules/stout/stout-flags",
    )

    submodule_repository(
        name = "com_github_3rdparty_stout_notification",
        path = "submodules/stout/stout-notification",
    )

    submodule_repository(
        name = "com_github_3rdparty_stout",
        path = "submodules/stout",
    )
