#!/bin/bash

set -e # Exit if a command exits with an error.
set -u # Treat expanding an unset variable as an error.
set -x # Echo executed commands to help debug failures.

# Prepare credentials that the build uses to access our bazel remote cache.
# GCP credentials are provided via a Codespaces secret.
echo "${GCP_GITHUB_INFRA_REMOTE_CACHE_CREDENTIALS}" > "${GOOGLE_APPLICATION_CREDENTIALS}"
