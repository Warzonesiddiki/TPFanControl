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

## 3. Static and build checks

Every change should run:

```text
Clean build
Unit tests
Static analysis or compiler warnings
git diff --check
```

New warnings are defects unless explicitly documented and accepted.

## 4. Hardware test prerequisites

Before writing to the EC:

- completed Phase 0 report;
- exact profile selected;
- external temperature/RPM monitor running;
- AC adapter connected;
- BIOS restore command verified;
- emergency recovery procedure available;
- test operator approval recorded.

Use [templates/TEST_REPORT.md](templates/TEST_REPORT.md) for every test session.

## 5. Physical test matrix

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

## 6. Fault-injection requirements

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

## 7. Evidence retention

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

## 8. Test completion rule

A test passes only when the expected result and evidence are both present. “It seemed to work” is not a reproducible result.
