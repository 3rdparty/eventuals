"""Dependency specific initialization."""

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")
load("@bazel_tools//tools/build_defs/repo:utils.bzl", "maybe")
load("@rules_foreign_cc//foreign_cc:repositories.bzl", "rules_foreign_cc_dependencies")

def deps(repo_mapping = {}):
    rules_foreign_cc_dependencies()

    # if "com_github_jemalloc_jemalloc" not in native.existing_rules():
    #     http_archive(
    #         name = "com_github_jemalloc_jemalloc",
    #         url = "https://github.com/jemalloc/jemalloc/archive/refs/tags/5.2.1.tar.gz",
    #         sha256 = "ed51b0b37098af4ca6ed31c22324635263f8ad6471889e0592a9c0dba9136aea",
    #         strip_prefix = "jemalloc-5.2.1",
    #         repo_mapping = repo_mapping,
    #         build_file_content = BUILD_JEMALLOC_CONTENT,
    #     )

    maybe(
        http_archive,
        name = "com_github_jemalloc_jemalloc",
        url = "https://github.com/jemalloc/jemalloc/archive/refs/tags/5.2.1.tar.gz",
        sha256 = "ed51b0b37098af4ca6ed31c22324635263f8ad6471889e0592a9c0dba9136aea",
        strip_prefix = "jemalloc-5.2.1",
        repo_mapping = repo_mapping,
        build_file = "//bazel-rules-jemalloc:BUILD.bazel",
    )
