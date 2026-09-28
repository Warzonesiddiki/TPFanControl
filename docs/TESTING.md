# Testing strategy

## 1. Testing principles

Hardware-control code must be tested at three levels:

1. deterministic tests with no hardware;
2. Windows integration tests with a fake or unavailable backend;
3. controlled physical tests on the exact target laptop.

A green build is not evidence that EC writes are safe.

## 2. Unit-test targets

Create hardware-independent tests for:

### Configuration

- valid profile loads;
- missing required fields are rejected;
- unknown topology is rejected;
- invalid commands are rejected;
- invalid register values cannot enable control;
- profile revisions are reported.

### Curves

- points are ordered;
- hysteresis does not oscillate around a boundary;
- dwell time prevents command storms;
- BIOS/failsafe point is reachable;
- high-temperature guard overrides fan-off.

### EC protocol

Use a fake backend to simulate:

- IBF timeout;
- OBF timeout;
- stale output;
- read failure;
- write failure;
- readback mismatch;
- mutex timeout;
- backend disconnect.

### Safety

Prove that every failure path:

- stops normal commands;
- attempts BIOS restore;
- records the failure;
- enters monitor-only/failsafe;
- releases resources.

## 3. Portable-core and feature tests

The hardware-independent core lives under `fancontrol/core/` and is exercised by
**six** suites, all hardware-free and all buildable without Windows headers or a
driver:

| Suite | Source | Tests | Covers |
|---|---|---:|---|
| `core_tests` | `tests/core_tests.cpp` | 11 | curves, sensor validation, stuck/stale sources, controller gates, safety states |
| `ec_protocol_tests` | `tests/ec_protocol_tests.cpp` | 42 | `EcBus` transactions, wire sequence, IBF/OBF waits, timeouts, the `0x31` refusal |
| `app_bridge_tests` | `tests/app_bridge_tests.cpp` | 31 | the seam: a command reaches the EC only if the core authorised it, readback verification, fail-safe on a read failure |
| `ecdiag_tests` | `tests/ecdiag_tests.cpp` | 29 | the read-only diagnostic: a full run through a real `EcBus` writes no register, the integrity checks catch a reader that does, and the JSON report is accepted by a strict parser |
| `legacy_policy_tests` | `tests/legacy_policy_tests.cpp` | 20 | the legacy UI's intent translated to a core request; the refusal to address an individual fan; the monitor-only guard; the final-manual refusal |
| `legacy_backend_tests` | `tests/legacy_backend_tests.cpp` | 16 | the port backend: that port numbers arrive unchanged, that `EcBus` + backend is exactly one transaction, and that register writes are denied by default |

`core_tests` reports no count: it predates the convention and was not renumbered.
The other five print their own count and fail if it is wrong, so a suite that
silently ran half its cases fails rather than passing quietly.

All six are built and run by `tests/run_core_tests.sh`, the single entry point
used by CI on Linux, macOS and Windows. The same script builds the `ecdiag` tool
itself and exercises its modes and exit codes. On Windows the suites also build as
`tests/*.vcxproj`, and the tool as `tools/ecdiag/ecdiag.vcxproj`, in all four
configurations under `/W4 /WX /permissive- /EHsc`.

The `ecdiag` suite is the one that proves a negative: that a complete diagnostic
run writes no register. It does that by reading the bus's own write trace and the
fake's committed-write log rather than by trusting the tool's intent, and the
structural half of the same guarantee — the tool cannot name a write at all — is
checked by `scripts/check_ecdiag_readonly.py`. See [ECDIAG.md](ECDIAG.md).

### 3.0 What the bridge and policy suites deliberately do not prove

Three things, stated because each is easy to assume has been covered:

- **They do not prove the application builds.** `SetFan` is Win32 dialog code.
  The core call sequence it makes is typechecked by a translation unit that
  mirrors it statement for statement, which catches an API mismatch, but
  sprintf_s overload resolution, `MUTEXSEM` semantics and Win32 header
  interactions are only exercised by a real MSVC build.
- **They do not prove the fan moves.** Everything above `LegacyBackend` is
  exercised against a fake. The first real EC transaction is a Phase 0 event and
  must be read-only.
- **They do not prove the UI is honest.** The status text functions are tested to
  be non-empty and to name the right thing, but that a dialog actually shows them
  is a manual test with a person looking at it.

### 3.1 A guard for code that cannot be executed here

`SetFan` cannot run in this environment, so nothing in `tests/` can execute it.
The regression guard for the defect in `EC_REGISTER_MAP.md` §10 is therefore
static, and lives in `scripts/`:

| Script | Purpose |
|---|---|
| `check_legacy_ec.py` | fails the build on a write to the fan-selector register, or a fan-level write from outside the core, anywhere in the legacy sources |
| `check_legacy_ec_selftest.py` | proves that guard still matches what it claims to, over 17 cases of which half must not match |

The second script exists because the first is only as good as its patterns. It
has already found three real defects in the first: a level rule that matched
`0x2F` but not `0x2f`, an alternation that matched any prefix and so reported a
selector write under the wrong rule, and a signed-char rule that could not fire on
any real line. The last was removed rather than loosened — a guard that matches
nothing reads as protection in the job log while the tree is unprotected.

**A static guard is a floor, not a proof.** It catches the shapes it knows about.
It does not catch a write to `0x31` reached through a helper function it does not
recognise. The stronger guarantee is structural: `AppBridge` has no method that
writes a register except `apply`, and `EcBus` refuses `0x31` before touching the
bus. The static check exists to keep someone from routing around that, not to
replace it.

### 3.2 Assert on what arrives, not on what is returned

This suite was originally written entirely in terms of return values, and it
passed while the system underneath it was wrong. `LegacyBackend` implemented a
port interface by handing port numbers to a register-level API, so a single
register read wrote registers `0x04` and `0x00` on an unverified machine. Every
function returned a plausible `IoResult`; the numbers arriving at the driver were
the problem, and no return value mentions numbers.

The tests that matter here are therefore written against the trace:

- a register read through `EcBus` must arrive at the primitives as
  command byte → address byte → one data-port read, in that order;
- every port that appears in the trace must be a real configured port, never a
  register address in disguise;
- a denied register write must produce **no call at all**, not a call that was
  rolled back.

The fake also models enough of the EC status protocol for a transaction to
complete — a command is accepted, a data write fills the output buffer, a data
read empties it. A fake returning a constant times out on every read, so a
sequence test written against one would be testing the timeout path while
appearing to test the sequence. That is a test that passes for the wrong reason,
which is worse than one that fails.

Both defects were found by asking what number actually reached the layer below,
not by reading the code. That question is now a permanent test.

### 3.3 Assertions must survive `NDEBUG`

Tests use the `CHECK` macro from `tests/test_check.h`, **not** `assert`.

`assert` is removed by `NDEBUG`, which Release defines. A suite written with `assert`
therefore runs zero checks in the Release configuration that CI actually builds, and
still exits 0 — it reports success while testing nothing. It also breaks `/W4 /WX`,
because variables referenced only inside assertions become unused.

`CHECK` has no conditional compilation. CI builds and runs both suites a second time
with `-O2 -DNDEBUG` specifically to prove the assertions are still live. The
difference was verified directly rather than assumed: under `-DNDEBUG`, a deliberately
failing `CHECK` aborts with exit 134, and the same condition in an `assert` exits 0.

### 3.4 Assertions must be against evidence, not intent

Where a test needs to know what reached the hardware, it must read a record of what
actually happened, not what the code intended. The EC fake maintains two logs
deliberately:

- `writeLog()` — every write **attempted**;
- `committedWrites()` — writes that actually **took effect**.

Asserting against the wrong one produces a test that passes while proving nothing. A
write that times out may still have changed the register (the EC received the byte but
never released it), so `testWriteThatNeverCompletesTimesOut` asserts the register *did*
change while the transaction *did* fail. Conflating those would be the easiest way to
make this suite lie.

Cover:

- curve validation and boundary evaluation;
- hysteresis and dwell timing with an injected clock;
- sensor range, freshness, sentinel, and source-validity checks;
- manual override expiry and cancellation;
- safety-state transitions;
- fan-health states when RPM is supported or unavailable;
- capability reports that fail closed;
- diagnostic redaction and bounded telemetry behavior.

No portable-core test may issue a real port I/O operation.

### 3.5 What the EC suite does not prove

The fake models a conventional 0x62/0x66-style EC closely enough to exercise
`EcBus`, and it is the only thing standing between the code and the hardware. It does
**not** prove:

- that the real machine uses these ports or this command encoding. They are
  **transcribed from `fancontrol/portio.cpp`, not measured**; see
  [EC_REGISTER_MAP.md](EC_REGISTER_MAP.md) §3.1 and §9. Phase 0 must confirm them
  read-only before any write is attempted. The suite pins them against
  `portio.cpp` so they cannot drift, which guards consistency with the legacy code
  and says nothing at all about the hardware;
- anything about a real EC's timing, arbitration, or behaviour under concurrent
  access from the legacy application's own thread (`EcBus`'s mutex covers only
  `EcBus` users — that is T3-06).

## 4. Static and build checks

Every change should run:

```text
Clean build
Unit tests
Static analysis or compiler warnings
git diff --check
```

New warnings are defects unless explicitly documented and accepted.

## 5. Hardware test prerequisites

Before writing to the EC:

- completed Phase 0 report;
- exact profile selected;
- external temperature/RPM monitor running;
- AC adapter connected;
- BIOS restore command verified;
- emergency recovery procedure available;
- test operator approval recorded.

Use [templates/TEST_REPORT.md](templates/TEST_REPORT.md) for every test session.

## 6. Physical test matrix

| ID | Test | Expected result | Evidence |
|---|---|---|---|
| H-01 | Start with backend absent | Monitor-only, no EC writes | Log |
| H-02 | Start with unverified profile | Control refused | Log/screenshot |
| H-03 | Read current EC state | Valid or explicit error | Raw report |
| H-04 | BIOS/automatic restore | Readback matches | Raw report |
| H-05 | Manual level 1 | Fan/RPM response observed | Test report |
| H-06 | Manual level 3 | Fan/RPM response observed | Test report |
| H-07 | Manual level 7 | Response observed without overheat | Test report |
| H-08 | Single-fan selector safety | `0x31` is never written | Fake-backend trace |
| H-09 | Return to BIOS | BIOS command succeeds on exit | Log |
| H-10 | Smart curve idle | No command oscillation | CSV |
| H-11 | Smart curve sustained load | Fan ramps at documented thresholds | CSV |
| H-12 | High-temperature guard | BIOS/failsafe latches | Log |
| H-13 | EC read failure | BIOS restore attempted | Fault injection/hardware log |
| H-14 | Write mismatch | Control stops; failure visible | Log |
| H-15 | Sleep/resume | State revalidated before control | Test report |
| H-16 | AC/battery transition | No unsafe profile switch | Test report |
| H-17 | Forced application close | Restore behavior documented | Test report |
| H-18 | Explorer restart | Tray recovers | Screenshot |
| H-19 | HVCI enabled | Backend result documented | Environment report |
| H-20 | HVCI/backend blocked | Monitor-only fallback | Log |

## 7. Fault-injection requirements

Hardware should not be required to test these cases. The fake backend must allow deterministic injection of:

- one failed read;
- repeated failed reads;
- one failed write;
- a wrong readback;
- a stuck temperature;
- a stale timestamp;
- impossible RPM;
- mutex contention;
- backend shutdown during a command.

## 8. Evidence retention

For each release candidate retain:

```text
reports/
  build-log.txt
  unit-test-results.txt
  hardware-report.md
  test-report.md
  tpfancontrol.log
  telemetry.csv
```

Do not commit raw serial numbers, personal paths, or private system information.

## 9. Test completion rule

A test passes only when the expected result and evidence are both present. “It seemed to work” is not a reproducible result.
