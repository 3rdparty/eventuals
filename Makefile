# Makefile for running eventuals tests.

# GCloud credentials will be required to be present for any target that runs
# a bazel command using a remote cache, which (since it's a Google Cloud
# Storage bucket) requires those credentials to function.
#
# We normally have the user do a synchronous authentication and place the
# resulting credentials in a  place where it can get shared with the Docker
# containers.
GOOGLE_APPLICATION_CREDENTIALS ?= /tmp/gcloud-access-token.json
GOOGLE_APPLICATION_CREDENTIALS_DIR := ${shell dirname ${GOOGLE_APPLICATION_CREDENTIALS}}

DAZEL_VOLUMES := ${GOOGLE_APPLICATION_CREDENTIALS_DIR}:${GOOGLE_APPLICATION_CREDENTIALS_DIR}


BAZEL_BUILD_ARGS := --verbose_failures

# Use a remote build cache so we can all share our build artifacts.
# That'll save a new workstation a few minutes of build-time.
BAZEL_BUILD_ARGS += --remote_cache=https://storage.googleapis.com/reboot-workstations-buildcache --google_default_credentials

# Compute the Google Application Credentials; see a description of what they
# are at the variable definition above.
#
# It is recommended to make this an "order-only" target (use "|" before the
# target name) to avoid rebuilding artifacts when our credentials change.
${GOOGLE_APPLICATION_CREDENTIALS}:
	gcloud auth application-default login
	mkdir -p $(dir ${GOOGLE_APPLICATION_CREDENTIALS})
	cp ${HOME}/.config/gcloud/application_default_credentials.json ${GOOGLE_APPLICATION_CREDENTIALS}

.PHONY: test
test: | ${GOOGLE_APPLICATION_CREDENTIALS}
	DAZEL_ENV_VARS="`echo GOOGLE_APPLICATION_CREDENTIALS=${GOOGLE_APPLICATION_CREDENTIALS}`" \
	DAZEL_VOLUMES="${DAZEL_VOLUMES}" \
	dazel test ${BAZEL_BUILD_ARGS} //...

.PHONY: clean
clean:
	docker stop dazel-eventuals
