#!/usr/bin/env sh
# Portable test runner for the TPFanControl core.
#
# Builds and runs ALL SIX portable suites:
#   core_tests         - controller, curves, sensor validation, fan health
#   ec_protocol_tests  - EC bus timeouts, retry policy, the 0x31 refusal
#   app_bridge_tests   - the seam with the legacy application: no fan command
#                        without core approval, and never a 0x31 write
#   legacy_policy_tests - translating the legacy UI's intent into a controller
#                        request, including the refusal to address one fan
#   legacy_backend_tests- the EC access policy, including the read-only state
#                        an unverified machine is held in
#
# This is the suite CI runs on every push, and the one a contributor can run
# without Visual Studio. It is not a substitute for the Windows build: see
# docs/TESTING.md for what each layer does and does not prove.
#
# NOTE: this file must have no UTF-8 BOM. A BOM makes the kernel read the first
# two bytes as the shebang, fail, and hand the file to /bin/sh as source, which
# produces one confusing error and then runs the script anyway. CI checks this.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CXX=${CXX:-g++}
BUILD_DIR=${TMPDIR:-/tmp}/tpfancontrol-core-test
mkdir -p "$BUILD_DIR"

CORE_COMMON="-std=c++17 -Wall -Wextra -Werror -pedantic -I $ROOT/fancontrol/core -I $ROOT/tests"
CORE_SOURCES="$ROOT/fancontrol/core/core_types.cpp
$ROOT/fancontrol/core/sensor_validation.cpp
$ROOT/fancontrol/core/fan_curve.cpp
$ROOT/fancontrol/core/controller.cpp
$ROOT/fancontrol/core/io_backend.cpp
$ROOT/fancontrol/core/ec_protocol.cpp
$ROOT/fancontrol/core/app_bridge.cpp
$ROOT/fancontrol/core/legacy_policy.cpp
$ROOT/fancontrol/core/legacy_backend.cpp"

# $1 = suite name, $2 = extra sources, $3 = extra compile flags, $4 = extra link flags
build_and_run() {
    _name=$1
    _extra_sources=$2
    _extra_flags=$3
    _extra_link=$4

    # shellcheck disable=SC2086  # deliberate word splitting of the flag lists
    # shellcheck disable=SC2086
    # shellcheck disable=SC2086
    "$CXX" $CORE_COMMON $_extra_flags \
        $CORE_SOURCES \
        $_extra_sources \
        -o "$BUILD_DIR/$_name" \
        $_extra_link

    "$BUILD_DIR/$_name"
}

build_and_run core_tests \
    "$ROOT/tests/fake_backend.cpp $ROOT/tests/core_tests.cpp" \
    "" \
    ""

# ec_protocol_tests and app_bridge_tests use std::thread, so they need a
# threading library on POSIX. -pthread is harmless where it is not required.
build_and_run ec_protocol_tests \
    "$ROOT/tests/fake_ec.cpp $ROOT/tests/ec_protocol_tests.cpp" \
    "" \
    "-pthread"

build_and_run app_bridge_tests \
    "$ROOT/tests/fake_ec.cpp $ROOT/tests/app_bridge_tests.cpp" \
    "" \
    "-pthread"

build_and_run legacy_policy_tests \
    "$ROOT/tests/legacy_policy_tests.cpp" \
    "" \
    ""

# Needs fake_ec.cpp for FakeClock, and -pthread: the fake and the bus both use
# std::mutex, so the link needs a threading library.
build_and_run legacy_backend_tests \
    "$ROOT/tests/fake_ec.cpp $ROOT/tests/legacy_backend_tests.cpp" \
    "" \
    "-pthread"
