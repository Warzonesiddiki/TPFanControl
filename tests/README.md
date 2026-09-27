# Portable core tests

`core_tests.cpp` exercises the hardware-independent curve, sensor-validation, and safety-controller foundation. It does not load Windows headers, a driver, or a physical EC.

## Linux/macOS-like shell

From the repository root:

```sh
mkdir -p /tmp/tpfancontrol-core-test
g++ -std=c++17 -Wall -Wextra -Werror -pedantic \
  -I fancontrol/core \
  fancontrol/core/core_types.cpp \
  fancontrol/core/sensor_validation.cpp \
  fancontrol/core/fan_curve.cpp \
  fancontrol/core/controller.cpp \
  fancontrol/core/io_backend.cpp \
  tests/fake_backend.cpp \
  tests/core_tests.cpp \
  -o /tmp/tpfancontrol-core-test/core_tests
/tmp/tpfancontrol-core-test/core_tests
```

The Windows build should compile the same core sources without the legacy precompiled header. A Visual Studio test project or CTest integration can be added after the first Windows source build is available.

These tests prove only portable decision logic. They do not prove EC register meanings, backend compatibility, fan topology, temperature calibration, or physical safety.
