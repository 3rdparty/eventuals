# variables.bzl
COPTS = select({
    "@bazel_tools//src/conditions:windows": ["/std:c++17", "--compiler='clang-cl'"],
    "//conditions:default": ["-std=c++17"],
})
