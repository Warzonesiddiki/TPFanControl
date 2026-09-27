#!/usr/bin/env sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CXX=${CXX:-g++}
BUILD_DIR=${TMPDIR:-/tmp}/tpfancontrol-core-test
mkdir -p "$BUILD_DIR"

"$CXX" -std=c++17 -Wall -Wextra -Werror -pedantic \
  -I "$ROOT/fancontrol/core" \
  "$ROOT/fancontrol/core/core_types.cpp" \
  "$ROOT/fancontrol/core/sensor_validation.cpp" \
  "$ROOT/fancontrol/core/fan_curve.cpp" \
  "$ROOT/fancontrol/core/controller.cpp" \
  "$ROOT/fancontrol/core/io_backend.cpp" \
  "$ROOT/tests/fake_backend.cpp" \
  "$ROOT/tests/core_tests.cpp" \
  -o "$BUILD_DIR/core_tests"

"$BUILD_DIR/core_tests"
