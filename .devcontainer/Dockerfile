###############################################################################
# Use a specific ubuntu version so we're not suprised by silent changes to gcc
# versions (and thus C++ feature support). Jammy comes with gcc 11:
# https://packages.ubuntu.com/jammy/gcc
FROM ubuntu:jammy-20230804

# Register relevant parameters for user creation.
ARG UNAME=vscode
ARG UID=1000
ARG GID=1000

# Installs `wget` and `curl` to aid debugging and to install other tools, with
# ca-certificates to make https calls.
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    wget \
    curl \
    && rm -rf /var/lib/apt/lists/*

ARG CLANG_VERSION=14

RUN apt-get update \
    # Install various prerequisite packages need for building as well as
    # packages that aid developing and debugging.
    #
    # As the list/version of packages is not changing too often, we optimize
    # for image layer size and run all `apt` related commands (that includes
    # the invokation of `llvm.sh`) in one `RUN` statement.
    #
    # If you are adding packages to the list below, please add a comment here
    # as well.
    #
    # Additional mandatory packages we install:
    #  * autoconf: required to build cc_image targets (alexmc: I think!).
    #  * build-essential: get gcc and std headers.
    #  * ca-certificates: dependency for curl to make https calls.
    #  * curl: not strictly needed (while-false: I think) outside of image
    #    building but small (~100kb) and useful for debugging. Might be used
    #    internally by bazel to fetch `http_archives`.
    #  * gnupg: for image and package signing.
    #  * software-properties-common: required to add the llvm apt repository.
    #  * zlib1g-dev: required to build cc_image targets.
    #  * git: Used by `bazel` to fetch a `git_repository`.
    #  * lsb-release: to allow install scripts/(debugging )developers to figure
    #    out what system they are on.
    #  * make: required to build cc_image targets (alexmc: I think!).
    #  * wget: see `curl`.
    #
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    # Mandatory packages:
    autoconf \
    build-essential \
    gnupg \
    git \
    lsb-release \
    software-properties-common \
    zlib1g-dev \
    make \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    # Install clang. Instructions:
    # https://apt.llvm.org/
    && wget -O /tmp/llvm.sh "https://apt.llvm.org/llvm.sh" \
    && chmod +x /tmp/llvm.sh \
    && /tmp/llvm.sh ${CLANG_VERSION} \
    && rm /tmp/llvm.sh \
    # Make clang mean clang-xx
    && ln -s /usr/bin/clang-${CLANG_VERSION} /usr/bin/clang \
    # As of ~2023 we also need to explicitly include
    # 'libclang-rt-${CLANG_VERSION}-dev' so that we can
    # build with '--config=asan'.
    && apt-get install -y libclang-rt-${CLANG_VERSION}-dev \
    # Cleanup.
    && apt-get purge --auto-remove -y \
    && rm -rf /var/lib/apt/lists/*

# Install Bazel.
ARG BAZELISK_VERSION=v1.18.0

RUN wget -O /usr/local/bin/bazel https://github.com/bazelbuild/bazelisk/releases/download/${BAZELISK_VERSION}/bazelisk-linux-amd64 \
    && chmod +x /usr/local/bin/bazel

# Install buildifier from this releases page:
#   https://github.com/bazelbuild/buildtools/releases
#
# We use the latest Buildifier version that matches our Bazel major version.
ARG BUILDIFIER_VERSION=5.1.0
RUN wget https://github.com/bazelbuild/buildtools/releases/download/${BUILDIFIER_VERSION}/buildifier-linux-amd64 \
    && chmod +x ./buildifier-linux-amd64 \
    && mv ./buildifier-linux-amd64 /usr/local/bin/buildifier

# Codespace'ify this image
# Download script and run it with vscode options.
# For reference of options, see:
#   https://github.com/microsoft/vscode-dev-containers/blob/v0.209.6/containers/ubuntu/.devcontainer/base.Dockerfile
# For reference on script run, see:
#   https://github.com/microsoft/vscode-dev-containers/tree/main/script-library
# For further info, see:
#   https://github.com/microsoft/vscode-dev-containers/blob/main/script-library/docs/docker.md
#
# Default to bash shell and set env var for the docker-init we are about to
# start.
ENV SHELL=/bin/bash \
    DOCKER_BUILDKIT=1

ARG VSCODE_SCRIPTS_COMMIT=ef146121026c67d41bbca80d9af482f20f89f9e0
RUN apt-get update && export DEBIAN_FRONTEND=noninteractive  \
    && apt-get -y install --no-install-recommends curl ca-certificates \
    && bash -c "$(curl -fsSL "https://raw.githubusercontent.com/microsoft/vscode-dev-containers/${VSCODE_SCRIPTS_COMMIT}/script-library/common-debian.sh")" -- "true" "${UNAME}" "${UID}" "${GID}" "true" \
    && apt-get clean -y && rm -rf /var/lib/apt/lists/*

# Set entrypoint and command as part of the codespaceifying. See links above.
ENTRYPOINT ["/usr/local/share/ssh-init.sh", "/usr/local/share/docker-init.sh"]
CMD ["sleep", "infinity"]
