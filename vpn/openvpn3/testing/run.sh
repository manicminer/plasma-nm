#!/usr/bin/env bash
# Run only inside disposable Docker containers; never mount a host bus/home.
set -euo pipefail
repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
: "${BUILD_ROOT:?Set BUILD_ROOT to a dedicated scratch directory outside the checkout}"
case "$BUILD_ROOT" in
    /*) ;;
    *) echo 'BUILD_ROOT must be absolute' >&2; exit 2 ;;
esac
case "$BUILD_ROOT/" in
    "$repo/"*|/) echo 'BUILD_ROOT must be outside the checkout' >&2; exit 2 ;;
esac
mkdir -p "$BUILD_ROOT"
command=${1:-test}
shift || true
case "$command" in
    images)
        : "${BACKEND_ROOT:?Set BACKEND_ROOT to the companion backend checkout (read only)}"
        mkdir -p "$BUILD_ROOT/backend-src"
        rsync -a --delete --exclude /.git/ --exclude /.hermes/ "$BACKEND_ROOT/" "$BUILD_ROOT/backend-src/"
        cp "$repo/vpn/openvpn3/testing/Dockerfile.test" "$BUILD_ROOT/Dockerfile.test"
        docker build -t plasma-nm-env -f "$repo/vpn/openvpn3/testing/Dockerfile.build" "$repo/vpn/openvpn3/testing"
        docker build --build-arg TEST_UID="$(id -u)" -t plasma-nm-test-env -f "$BUILD_ROOT/Dockerfile.test" "$BUILD_ROOT"
        ;;
    build)
        mkdir -p "$BUILD_ROOT/plasma-src" "$BUILD_ROOT/plasma-build"
        # CMake installs git hooks, hence this disposable copy includes .git.
        rsync -a --delete --exclude /.hermes/ "$repo/" "$BUILD_ROOT/plasma-src/"
        docker run --rm --network none --cap-drop ALL --security-opt no-new-privileges \
            --user "$(id -u):$(id -g)" -e HOME=/tmp -e XDG_CACHE_HOME=/tmp \
            -v "$BUILD_ROOT/plasma-src:/src" -v "$BUILD_ROOT/plasma-build:/build" \
            -w /build plasma-nm-env sh -ec '
                cmake -S /src -B /build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DBUILD_OPENCONNECT=OFF
                cmake --build /build -- "$@"
            ' sh "$@"
        ;;
    test|screenshots)
        mkdir -p "$BUILD_ROOT/screenshots"
        args=(ctest --output-on-failure "$@")
        if [[ "$command" == screenshots ]]; then
            args=(/build/bin/openvpn3screenshot /screenshots)
        fi
        docker run --rm --network none --cap-drop ALL --security-opt no-new-privileges \
            --user "$(id -u):$(id -g)" \
            -e HOME=/tmp -e XDG_CACHE_HOME=/tmp -e XDG_RUNTIME_DIR=/tmp -e QT_QPA_PLATFORM=offscreen \
            -v "$BUILD_ROOT/plasma-src:/src:ro" -v "$BUILD_ROOT/plasma-build:/build" \
            -v "$BUILD_ROOT/screenshots:/screenshots" \
            -w /build plasma-nm-test-env "${args[@]}"
        ;;
    *) echo 'Usage: run.sh images|build|test|screenshots [arguments]' >&2; exit 2 ;;
esac
