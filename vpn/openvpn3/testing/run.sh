#!/usr/bin/env bash
# Run only inside disposable Docker containers; never mount a host bus/home.
#
# BUILD_ROOT must be a dedicated scratch directory outside the checkout: this
# script creates it, writes a source copy and a build tree into it, and rsyncs
# over them. See paths.sh for the guards and testing/test-paths.sh for their
# regression tests.
set -euo pipefail
HARNESS_REPO=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
# shellcheck source=paths.sh
. "$(dirname "${BASH_SOURCE[0]}")/paths.sh"

command=${1:-test}
shift || true
case "$command" in
    images|build|test|screenshots) ;;
    *) echo 'Usage: run.sh images|build|test|screenshots [arguments]' >&2; exit 2 ;;
esac

: "${BUILD_ROOT:?Set BUILD_ROOT to a dedicated scratch directory outside the checkout}"
build_root=$(require_dedicated_dir BUILD_ROOT "$BUILD_ROOT")

# Image tags are overridable so a release-backport tree can be verified without
# overwriting the images of another checkout.
image_build=${IMAGE_BUILD:-plasma-nm-env}
image_test=${IMAGE_TEST:-plasma-nm-test-env}
# Built with the distro feature set by default; OpenConnect is part of what the
# distribution's own plasma-nm package ships, so it is built and kept working.
build_openconnect=${BUILD_OPENCONNECT:-ON}

# The two directories under BUILD_ROOT that are rsynced over or rebuilt.
plasma_src="$build_root/plasma-src"
plasma_build="$build_root/plasma-build"

case "$command" in
    images)
        : "${BACKEND_ROOT:?Set BACKEND_ROOT to the companion backend checkout (read only)}"
        backend_root=$(require_existing_dir BACKEND_ROOT "$BACKEND_ROOT")
        require_outside BACKEND_ROOT "$backend_root" BUILD_ROOT/plasma-src "$plasma_src"
        require_outside BACKEND_ROOT "$backend_root" BUILD_ROOT/plasma-build "$plasma_build"
        # Optional: pinned, detached-signed dependency archives (openvpn3 and
        # gdbuspp are in no official Arch repository). DEPS_DIR must also hold
        # signing-key.pub, and DEPS_FINGERPRINT pins who may have signed them.
        deps_dir=
        if [[ -n ${DEPS_DIR:-} ]]; then
            : "${DEPS_FINGERPRINT:?Set DEPS_FINGERPRINT to the pinned signing fingerprint when using DEPS_DIR}"
            deps_dir=$(require_existing_dir DEPS_DIR "$DEPS_DIR")
            require_outside DEPS_DIR "$deps_dir" BUILD_ROOT/plasma-src "$plasma_src"
            require_outside DEPS_DIR "$deps_dir" BUILD_ROOT/plasma-build "$plasma_build"
        fi

        # The image build context is a temporary directory this script creates
        # and removes, so nothing under BUILD_ROOT has to be deleted to make
        # room for it.
        context=$(mktemp -d "${TMPDIR:-/tmp}/openvpn3-image-context-XXXXXX")
        trap 'rm -rf -- "$context"' EXIT
        mkdir -p "$context/backend-src" "$context/deps"
        rsync -a --exclude /.git/ --exclude /.hermes/ "$backend_root/" "$context/backend-src/"
        if [[ -n $deps_dir ]]; then
            rsync -a "$deps_dir/" "$context/deps/"
        fi
        cp "$HARNESS_REPO/vpn/openvpn3/testing/Dockerfile.test" "$context/Dockerfile.test"

        docker build -t "$image_build" -f "$HARNESS_REPO/vpn/openvpn3/testing/Dockerfile.build" \
            "$HARNESS_REPO/vpn/openvpn3/testing"
        docker build --build-arg TEST_UID="$(id -u)" \
            --build-arg BASE_IMAGE="$image_build" \
            --build-arg DEPS_FINGERPRINT="${DEPS_FINGERPRINT:-}" \
            -t "$image_test" -f "$context/Dockerfile.test" "$context"
        ;;
    build)
        mkdir -p "$plasma_src" "$plasma_build"
        # CMake installs git hooks, hence this disposable copy includes .git.
        rsync -a --delete --exclude /.hermes/ "$HARNESS_REPO/" "$plasma_src/"
        docker run --rm --network none --cap-drop ALL --security-opt no-new-privileges \
            --user "$(id -u):$(id -g)" -e HOME=/tmp -e XDG_CACHE_HOME=/tmp \
            -v "$plasma_src:/src" -v "$plasma_build:/build" \
            -w /build "$image_build" sh -ec '
                openconnect=$1; shift
                cmake -S /src -B /build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON \
                    -DBUILD_OPENCONNECT="$openconnect"
                cmake --build /build -- "$@"
            ' sh "$build_openconnect" "$@"
        ;;
    test|screenshots)
        mkdir -p "$build_root/screenshots"
        args=(ctest --output-on-failure "$@")
        if [[ "$command" == screenshots ]]; then
            args=(/build/bin/openvpn3screenshot /screenshots)
        fi
        docker run --rm --network none --cap-drop ALL --security-opt no-new-privileges \
            --user "$(id -u):$(id -g)" \
            -e HOME=/tmp -e XDG_CACHE_HOME=/tmp -e XDG_RUNTIME_DIR=/tmp -e QT_QPA_PLATFORM=offscreen \
            -v "$plasma_src:/src:ro" -v "$plasma_build:/build" \
            -v "$build_root/screenshots:/screenshots" \
            -w /build "$image_test" "${args[@]}"
        ;;
esac
