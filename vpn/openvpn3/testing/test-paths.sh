#!/usr/bin/env bash
# Regression tests for run.sh's directory guards.
#
# run.sh writes into BUILD_ROOT and rsyncs over directories underneath it, so
# a BUILD_ROOT that resolves to "/", to your home directory or back into the
# checkout is a destructive mistake. These tests pin that down.
#
# Nothing here is allowed to actually delete or copy anything: run.sh is
# invoked with stub docker/rsync/rm/mkdir/cp on PATH, which only record that
# they were called. A guard regression therefore shows up as a failing
# assertion, never as damage to the host. The only real filesystem writes are
# inside one mktemp directory that this script creates and removes itself.
set -euo pipefail

here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
repo=$(cd "$here/../../.." && pwd)
run_sh="$here/run.sh"

work=$(mktemp -d "${TMPDIR:-/tmp}/openvpn3-path-guard-XXXXXX")
trap 'rm -rf -- "$work"' EXIT
# run.sh makes its own temporary directories and removes them with rm, which
# is stubbed out below; point them inside $work so this script still cleans up.
export TMPDIR="$work"

stubs="$work/stubs"
mkdir -p "$stubs"
for tool in docker rsync rm mkdir cp; do
    cat >"$stubs/$tool" <<'STUB'
#!/usr/bin/env bash
printf '%s %s\n' "$(basename "$0")" "$*" >>"$STUB_LOG"
exit 0
STUB
    chmod +x "$stubs/$tool"
done

# A valid dedicated scratch directory, and a companion backend checkout.
good_root="$work/scratch/ovpn3-build"
backend="$work/backend-src"
deps="$work/deps"
mkdir -p "$good_root" "$backend" "$deps"
: >"$deps/signing-key.pub"
# A symlink alias for the checkout: a string-only guard does not see through it.
ln -s "$repo" "$work/repo-link"

pass=0
fail=0

# run_case <expectation: accept|reject> <description> [VAR=VALUE ...] -- <run.sh arguments>
run_case() {
    local expectation=$1 description=$2
    shift 2
    local -a env_args=()
    while [[ $# -gt 0 && $1 != -- ]]; do
        env_args+=("$1")
        shift
    done
    shift || true

    local log="$work/stub.log"
    : >"$log"
    local rc=0
    local output
    output=$(env -u BUILD_ROOT -u BACKEND_ROOT -u DEPS_DIR -u DEPS_FINGERPRINT \
        PATH="$stubs:$PATH" STUB_LOG="$log" \
        "${env_args[@]}" bash "$run_sh" "$@" 2>&1) || rc=$?

    local problem=""
    if [[ $expectation == reject ]]; then
        if [[ $rc -eq 0 ]]; then
            problem="expected a non-zero exit, got 0"
        elif [[ -s $log ]]; then
            # The important half: a rejected directory must be rejected
            # *before* anything touches the filesystem or starts a container.
            problem="rejected, but side effects ran first: $(tr '\n' ';' <"$log")"
        fi
    else
        if [[ $rc -ne 0 ]]; then
            problem="expected success, got exit $rc"
        elif ! grep -q '^docker ' "$log"; then
            problem="accepted, but never reached docker"
        fi
    fi

    if [[ -n $problem ]]; then
        fail=$((fail + 1))
        printf 'FAIL  %s\n        %s\n' "$description" "$problem"
        if [[ -n $output ]]; then
            printf '        output: %s\n' "$(printf '%s' "$output" | tr '\n' ';')"
        fi
    else
        pass=$((pass + 1))
        printf 'ok    %s\n' "$description"
    fi
}

echo "# BUILD_ROOT must name a dedicated directory"
run_case reject 'unset BUILD_ROOT' -- build
run_case reject 'relative BUILD_ROOT' BUILD_ROOT=scratch -- build
run_case reject 'the filesystem root' BUILD_ROOT=/ -- build
run_case reject 'the filesystem root written as //' BUILD_ROOT=// -- build
run_case reject 'the filesystem root written as ///' BUILD_ROOT=/// -- build
run_case reject 'dot-dot that climbs to the root' BUILD_ROOT=/tmp/.. -- build
run_case reject 'dot-dot that climbs to the root, with a trailing slash' BUILD_ROOT=/tmp/../ -- build
run_case reject 'a top-level directory (/tmp)' BUILD_ROOT=/tmp -- build
run_case reject 'a top-level directory (/home)' BUILD_ROOT=/home -- build
run_case reject 'a top-level directory (/usr)' BUILD_ROOT=/usr -- build
run_case reject 'the home directory itself' BUILD_ROOT="$HOME" -- build
run_case reject 'the home directory reached through dot-dot' BUILD_ROOT="$HOME/.cache/.." -- build
run_case reject "another account's home directory" BUILD_ROOT=/home/someone-else -- build

echo
echo "# BUILD_ROOT must not overlap the checkout"
run_case reject 'the checkout itself' BUILD_ROOT="$repo" -- build
run_case reject 'a directory inside the checkout' BUILD_ROOT="$repo/build" -- build
run_case reject 'inside the checkout via dot-dot' BUILD_ROOT="$repo/../$(basename "$repo")/build" -- build
run_case reject 'inside the checkout via a symlink' BUILD_ROOT="$work/repo-link/build" -- build
run_case reject 'a directory that contains the checkout' BUILD_ROOT="$(dirname "$repo")" -- build

echo
echo "# valid dedicated scratch directories keep working"
run_case accept 'a dedicated scratch directory' BUILD_ROOT="$good_root" -- build
run_case accept 'the same directory with a trailing slash' BUILD_ROOT="$good_root/" -- build
run_case accept 'the same directory reached through dot-dot' BUILD_ROOT="$good_root/../ovpn3-build" -- build
run_case accept 'a directory that does not exist yet' BUILD_ROOT="$work/scratch/not-created-yet" -- build
run_case accept 'image name overrides' BUILD_ROOT="$good_root" IMAGE_BUILD=a-env IMAGE_TEST=a-test-env -- build
run_case accept 'the test command' BUILD_ROOT="$good_root" -- test -R openvpn3

echo
echo "# companion inputs are validated too"
run_case reject 'images without BACKEND_ROOT' BUILD_ROOT="$good_root" -- images
run_case reject 'images with a BACKEND_ROOT that does not exist' \
    BUILD_ROOT="$good_root" BACKEND_ROOT="$work/absent" -- images
run_case reject 'DEPS_DIR without a pinned fingerprint' \
    BUILD_ROOT="$good_root" BACKEND_ROOT="$backend" DEPS_DIR="$deps" -- images
run_case reject 'a DEPS_DIR that does not exist' \
    BUILD_ROOT="$good_root" BACKEND_ROOT="$backend" DEPS_DIR="$work/absent" DEPS_FINGERPRINT=AB -- images
run_case reject 'a BACKEND_ROOT inside the directory build overwrites' \
    BUILD_ROOT="$good_root" BACKEND_ROOT="$good_root/plasma-src/backend" -- images
run_case reject 'a DEPS_DIR inside the directory build overwrites' \
    BUILD_ROOT="$good_root" BACKEND_ROOT="$backend" DEPS_DIR="$good_root/plasma-src/deps" DEPS_FINGERPRINT=AB -- images
run_case accept 'images with a separate backend checkout' \
    BUILD_ROOT="$good_root" BACKEND_ROOT="$backend" -- images
run_case accept 'images with pinned dependency archives' \
    BUILD_ROOT="$good_root" BACKEND_ROOT="$backend" DEPS_DIR="$deps" DEPS_FINGERPRINT=AB -- images

echo
echo "# an unknown command is still refused"
run_case reject 'an unknown command' BUILD_ROOT="$good_root" -- frobnicate

echo
printf '%d passed, %d failed\n' "$pass" "$fail"
[[ $fail -eq 0 ]]
