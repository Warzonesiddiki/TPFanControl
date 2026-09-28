# Portable core tests

Seven suites exercise the hardware-independent core under `fancontrol/core/`. None
of them loads Windows headers, a driver, or a physical EC, and no test issues a
real port I/O operation.

| Suite | Tests | Covers |
|---|---:|---|
| `core_tests` | — | curves, sensor validation, stuck/stale sources, controller gates, safety states |
| `ec_protocol_tests` | 42 | `EcBus` transactions, wire sequence, IBF/OBF waits, timeouts, the `0x31` refusal |
| `app_bridge_tests` | 31 | the seam: a command reaches the EC only if the core authorised it, readback verification, fail-safe on a read failure |
| `ecdiag_tests` | 29 | the read-only diagnostic: a full run writes no register, the integrity checks catch a reader that does, strict JSON parsing |
| `legacy_policy_tests` | 20 | the legacy UI's intent translated to a core request; the single-fan refusal; the monitor-only guard |
| `legacy_backend_tests` | 16 | the port backend: port numbers arrive unchanged, `EcBus` + backend is one transaction, register writes are denied by default |
| `tvicport_backend_tests` | 19 | the TVicPort adapter's lifecycle against a fake DLL: what it opens, what it refuses to open, and the hard-access switch it leaves alone |

`core_tests` reports no count: it predates the convention and was not renumbered.
The others print their own count and fail if it is wrong, so a suite that silently
ran half its cases fails instead of passing quietly.

## Running them

The single entry point builds every suite, runs it, and then builds and
smoke-tests the `ecdiag` tool:

```sh
./tests/run_core_tests.sh
```

It needs only a C++17 compiler (`CXX`, default `g++`). There is no Makefile, no
CMake, and no Visual Studio: the script is the contract, and CI runs the same
script on Linux, macOS and Windows.

To run one suite by hand:

```sh
g++ -std=c++17 -Wall -Wextra -Werror -pedantic \
  -I fancontrol/core -I tests \
  fancontrol/core/*.cpp \
  tests/fake_ec.cpp tests/ec_protocol_tests.cpp \
  -o /tmp/ec_protocol_tests -pthread
/tmp/ec_protocol_tests
```

Which fake a suite needs is not uniform, and the runner's table is the record:
`core_tests` uses `tests/fake_backend.cpp`; the EC suites use `tests/fake_ec.cpp`;
`legacy_policy_tests` and `tvicport_backend_tests` supply their own doubles inline
and need neither. This has
already caused one CI-shaped mistake — a suite that stopped linking because its
fake was moved — which is why `scripts/check_ci_steps.py` executes the workflow's
own portable steps locally.

## On Windows

The suites also build as `tests/*.vcxproj`, each in all four configurations,
under `/W4 /WX /permissive- /EHsc`, and are registered in
`fancontrol/fancontrol.sln`. Because that is a second, independent statement of
which sources each binary is made of, `scripts/check_project_sources.py` compiles
each project's own `<ClCompile>` list here and runs the result: the first run of
that check found four projects unable to link and five projects all writing
`core_tests.exe`.

`tests/ecdiag_tests.cpp` carries its own strict JSON parser, with a built-in
self-test proving the parser rejects what is not JSON, so "the report parses" is
checked rather than hoped for.

These tests prove portable decision logic and the port-level protocol only. They
do not prove register meanings, backend compatibility, fan topology, temperature
calibration, or physical safety — those are Phase 0 work, and until a backend
exists no test here has spoken to a machine.
