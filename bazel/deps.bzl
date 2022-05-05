"""Dependency specific initialization."""

load("@bazel_skylib//:workspace.bzl", "bazel_skylib_workspace")
load("@com_github_3rdparty_bazel_rules_asio//bazel:deps.bzl", asio_deps = "deps")
load("@com_github_3rdparty_bazel_rules_curl//bazel:deps.bzl", curl_deps = "deps")
load("@com_github_3rdparty_bazel_rules_jemalloc//bazel:deps.bzl", jemalloc_deps = "deps")
load("@com_github_3rdparty_bazel_rules_libuv//bazel:deps.bzl", libuv_deps = "deps")
load("@com_github_3rdparty_stout_atomic_backoff//bazel:deps.bzl", stout_atomic_backoff_deps = "deps")
load("@com_github_3rdparty_stout_stateful_tally//bazel:deps.bzl", stout_stateful_tally_deps = "deps")

# buildifier: disable=out-of-order-load
load("@com_github_3rdparty_stout_borrowed_ptr//bazel:deps.bzl", stout_borrowed_ptr_deps = "deps")
load("@com_github_3rdparty_stout_flags//bazel:deps.bzl", stout_flags_deps = "deps")

# buildifier: disable=out-of-order-load
load("@com_github_3rdparty_stout_notification//bazel:deps.bzl", stout_notification_deps = "deps")

# buildifier: disable=out-of-order-load
load("@com_github_3rdparty_stout//bazel:deps.bzl", stout_deps = "deps")
load("@com_github_grpc_grpc//bazel:grpc_deps.bzl", "grpc_deps")
load("@com_github_reboot_dev_pyprotoc_plugin//bazel:deps.bzl", pyprotoc_plugin_deps = "deps")

def deps(repo_mapping = {}):
    """Propagate all dependencies.

    Args:
        repo_mapping (str): {}.
    """
    asio_deps(
        repo_mapping = repo_mapping,
    )

    bazel_skylib_workspace()

    curl_deps(
        repo_mapping = repo_mapping,
    )

    jemalloc_deps(
        repo_mapping = repo_mapping,
    )

    libuv_deps(
        repo_mapping = repo_mapping,
    )

    stout_atomic_backoff_deps(
        repo_mapping = repo_mapping,
    )

    stout_stateful_tally_deps(
        repo_mapping = repo_mapping,
    )

    stout_borrowed_ptr_deps(
        repo_mapping = repo_mapping,
    )

    stout_flags_deps(
        repo_mapping = repo_mapping,
    )

    stout_notification_deps(
        repo_mapping = repo_mapping,
    )

    stout_deps(
        repo_mapping = repo_mapping,
    )

    pyprotoc_plugin_deps(
        repo_mapping = repo_mapping,
    )

    grpc_deps()
