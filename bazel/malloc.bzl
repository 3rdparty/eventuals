"""Defines malloc implementations for the specific OS."""

def malloc():
    """Returns malloc implementations for the specific OS."""
    return select({
        "@platforms//os:linux": "@com_github_jemalloc_jemalloc//:jemalloc",
        # TODO(https://github.com/3rdparty/eventuals/issues/124):
        # Build jemalloc on Windows
        # https://github.com/3rdparty/bazel-rules-jemalloc/tree/ArthurBandaryk.jemalloc-windows
        # TODO: Fix 'jemalloc' on macOS.
        # https://github.com/3rdparty/eventuals/issues/615
        "//conditions:default": "@bazel_tools//tools/cpp:malloc",
    })
