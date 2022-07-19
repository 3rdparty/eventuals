"""Defines malloc implementations for the specific OS."""

def malloc():
    """Returns malloc implementations for the specific OS."""
    return select({
        # TODO(https://github.com/3rdparty/eventuals/issues/124):
        # Build jemalloc on Windows
        # https://github.com/3rdparty/bazel-rules-jemalloc/tree/ArthurBandaryk.jemalloc-windows
        "@bazel_tools//src/conditions:windows": "@bazel_tools//tools/cpp:malloc",
        "//conditions:default": "@com_github_jemalloc_jemalloc//:jemalloc",
    })
