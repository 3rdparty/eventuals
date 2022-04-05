"""Get compiler options for the specific OS."""

# Common compiler options for macOS, Linux and Windows.
copts_common = [
    "-Werror",
]

# Get the final list of options composed with common options for the specific
# OS.
def copts():
    return copts_common + select({
        "@bazel_tools//src/conditions:darwin": [
            # We do not want to treat `syscall` warning as an error.
            # Future update of glog lib will fix this warning.
            "-Wno-error=deprecated-declarations",
        ],
        "@bazel_tools//src/conditions:windows": [
            "-Wno-error=microsoft-cast",
            "-Wno-error=invalid-noreturn",
            "-Wno-error=microsoft-include",
        ],
        "//conditions:default": [],
    })
