# Implementation checklist

Use this file as the project execution board. It is the **single source of truth** for
what is actually done. Do not mark an item complete without its evidence.

Read it before estimating progress, scheduling work, or citing what is outstanding.
A task marked `[x]` has evidence in the Evidence column; if the evidence is missing,
the task is not done, whatever the checkbox says.

Legend:

| Mark | Meaning |
|---|---|
| `[ ]` | Not started |
| `[-]` | In progress |
| `[x]` | Complete, with evidence |
| `[!]` | Blocked, or needs a decision before it can start |

## Summary

| Measure | Value |
|---|---|
| Documented safety gaps closed | 6 of 10 (gaps 1, 2, 3, 4, 5, 8) |
| Portable-core test functions | 20 core + 25 backend/EC |
| Work packages complete | 1 of 10 (T0) |
| Critical path | T0 → T1 → T5 → T3 → T4 → T6 → T8 |
| Biggest single risk | The portable core is not connected to the application. It is compiled by the project and tested, but no legacy file includes a core header, so the safety state machine is dead code that can drift from shipped behaviour indefinitely without any signal. |

A **[!] Phase 0** gate is open: no hardware evidence exists. Nothing in T4 may start
until Phase 0 completes.

---

## T0 — Repository hygiene

| ID | Task | Status | Evidence |
|---|---|---|---|
| T0-01 | Add `.gitignore` covering build output, VS state, runtime logs, executables, credentials, and test output | `[x]` | `.gitignore` |
| T0-02 | Expand `.gitattributes` to an explicit LF/CRLF and binary policy | `[x]` | `.gitattributes`; no pattern is both text and binary |
| T0-03 | Untrack all generated Visual Studio artifacts | `[x]` | 67 index deletions: `fancontrol/.vs`, `Debug`, `Release`, `ipch`, `fancontrol.VC.db` |
| T0-04 | Untrack committed runtime logs and the runtime INI copy | `[x]` | `fancontrol/UpgradeLog.htm` untracked; canonical `fancontrol/TPFanControl.ini` kept |
| T0-05 | Confirm source resources and the example profile stay tracked | `[x]` | 25 files under `fancontrol/res/` and the canonical INI verified tracked |
| T0-06 | Add `LICENSE` | `[x]` | Public domain dedication, TVicPort and legacy-sources exceptions, safety notice |
| T0-07 | Add `CONTRIBUTING.md` | `[x]` | Contribution contract, safety rules, test-first workflow, sanitizer instructions |
| T0-08 | Redirect build output to an ignored `out/` directory ([BUILD.md](BUILD.md) §7) | `[x]` | `OutDir`/`IntDir`/`OutputFile`/`ObjectFileName`/`ProgramDataBaseFileName`/`ProgramDatabaseFile`/`AssemblerListingLocation` all point inside `out/`; XML validated |
| T0-09 | Decide whether to rewrite history to purge the leaked username and binaries | `[x]` | ADR-017: do not rewrite; revisit conditions recorded |
| T0-10 | Final documentation audit: no broken links, no requirement stated as a guarantee without a status marker | `[x]` | `scripts/check_links.py` clean across all `.md`; no Windows user paths in tracked files; README no longer advertises the prebuilt binary; governance files linked from both READMEs |

---

## T1 — Build, CI, and test infrastructure

| ID | Task | Status | Evidence |
|---|---|---|---|
| T1-01 | Clean build from source with Visual Studio 2022 | `[-]` | Config complete; no MSVC locally. Awaiting the `windows-build` CI job's log. **Not verified.** |
| T1-02 | Migrate the solution to the `v143` toolset | `[-]` | `v142`→`v143` in both projects, solution bumped to format 17. Awaiting a VS2022 build. **Not verified.** |
| T1-03 | Add `Debug\|x64` and `Release\|x64`; x64 is primary (ADR-005) | `[-]` | 4 configurations in both `.vcxproj` files and the `.sln`; `TargetMachine` `MachineX86`/`MachineX64`; x64 PCH conditions added. Awaiting `msbuild /p:Platform=x64`. **Not verified.** |
| T1-04 | Raise core sources to `/W4` with warnings-as-errors; add `/permissive-` and `/EHsc` ([BUILD.md](BUILD.md) §6) | `[-]` | Core items carry `/W4` + `/WX` + `/permissive-` + `/EHsc`. GCC side already proven (`-Wall -Wextra -Werror -pedantic`). MSVC side awaits the `release-warnings-as-errors` job. **Not verified.** |
| T1-05 | Fix baseline legacy warnings so the whole project can reach `/W4` | `[-]` | Legacy raised `/W3`→`/W4` (warning-only until clean). The actual warning list is only observable under MSVC; the `windows-build` job archives the log to drive the fixes. **Not verified.** |
| T1-06 | UTF-8 source handling; explicit runtime library choice | `[x]` | ADR-018. All tracked text files are valid UTF-8; UTF-8 BOM on every source. `/source-charset:utf-8` on legacy, `/utf-8` on ASCII-only core. `/MT` app, `/MD` tests. Verified locally. |
| T1-07 | Audit and fix pointer-sized Win32 APIs: `SetWindowLong`/`GetWindowLong` in `fancontrol.cpp` and `SystemTraySDK.cpp`; `ULONG` thread callback in `misc.cpp` | `[x]` | `SetWindowLongPtr`/`GetWindowLongPtr`/`GWLP_USERDATA`/`LONG_PTR` applied across all three code trees. Thread callback changed `ULONG`→`LPVOID` and `CreateThread`→`_beginthreadex` (the worker is created and destroyed every data cycle, so `CreateThread` leaked a CRT block per cycle). Repo-wide grep finds zero remaining 32-bit window APIs. Verified locally. |
| T1-08 | Add CI: run `tests/run_core_tests.sh` on Linux, macOS, and Windows | `[-]` | `.github/workflows/ci.yml` job `portable-tests`, matrix `ubuntu`/`macos`/`windows`, plus an ASan+UBSan leg. Workflow YAML validated; script runs locally. Awaiting a green run. **Not verified.** |
| T1-09 | Add CI: Release build with warnings-as-errors on the core | `[-]` | Job `release-warnings-as-errors` builds both test projects Release x64 and runs them, then asserts `/W4 /WX /permissive- /EHsc` actually reached every core unit. Awaiting a green run. **Not verified.** |
| T1-10 | Add CI: `git diff --check` and a link check | `[-]` | Job `hygiene` runs `git diff --check`, `scripts/check_links.py`, a no-tracked-artifacts check, and a UTF-8 check. The link checker was negative-tested (3 seeded defects, all caught) and passes locally. Awaiting a green run. **Not verified.** |
| T1-11 | Add a Windows test target so the suite runs on Windows | `[-]` | `tests/core_tests.vcxproj` and `tests/ec_protocol_tests.vcxproj` build the same sources as `run_core_tests.sh`, 4 configurations each, `/W4` `/WX`, both in the solution. XML validated. Awaiting execution in CI. **Not verified.** |
| T1-12 | Add static analysis to CI (clang-tidy or MSVC `/analyze`) | `[-]` | Job `static-analysis` runs MSVC `/analyze` on the core (enforced) and the app (reported, `continue-on-error` pending T1-05), and archives a summary. Awaiting a green run. **Not verified.** |

**T1 environment note.** No Windows toolchain is available in the authoring environment
(`g++ 12.2`, Python 3.11 only; no `msbuild`, `cl.exe`, `dotnet`, CMake, or `clang++`).
Every Windows-only claim above is therefore marked `[-]` with its verification path
named, rather than reported as done. The portable-core and documentation claims in T1
are verified locally. Tasks are promoted to `[x]` only when a CI run supplies evidence.

---

## T2 — Remaining portable-core correctness

| ID | Task | Status | Evidence |
|---|---|---|---|
| T2-01 | Close safety gap 6: require N consecutive samples to agree sufficiently, not merely be individually valid | `[ ]` | Depends on T2-12 |
| T2-02 | Decide what happens when one sensor source is lost while others remain | `[ ]` | Depends on T2-12 |
| T2-03 | Failsafe cooldown and acknowledgement recovery so a latch does not require a process restart (§13 gap 7) | `[!]` | Needs a product decision — see T2-12 |
| T2-04 | Repeated-suspect-sample escalation for fan health (§13 gap 9) | `[ ]` | Half of gap 9 is addressed in T5-08; the escalation rule remains |
| T2-05 | Structured event output in the portable core (§13 gap 10) | `[ ]` | See T2-13 |
| T2-06 | Test: oscillation cannot pump the fan (the 10→70→20→75→40 °C case) | `[ ]` | Requires T2-01 |
| T2-07 | Test: a single-source loss degrades to monitor-only rather than continuing on partial data | `[ ]` | Requires T2-02 |
| T2-08 | Test: failsafe latch survives a latch-and-clear attempt without acknowledgement | `[ ]` | Requires T2-03 |
| T2-09 | Test: every restore path reports truthfully whether a BIOS command was issued | `[x]` | `testFailsafeReportsUnverifiedRestore`, `testShutdownReportsUnverifiedRestore`, `testVerifiedRestoreStillReportsBiosAutomatic` |
| T2-10 | Test: manual override expires and is cancelled by invalid sensor data | `[x]` | `testControllerGatesAndManualExpiry`, `testCapabilityAndFailureGates` |
| T2-11 | Test: emergency threshold is authoritative and validated | `[x]` | `testEmergencyThresholdValidation` |
| T2-12 | Record the sensor-agreement and failsafe-recovery policy decisions | `[!]` | Blocks T2-01, T2-02, T2-03. Needs an ADR |
| T2-13 | Define the portable core's event/logging interface | `[ ]` | Blocks T2-05 and much of T7 |
| T2-14 | Keep the curve validator and the controller's runtime curve in agreement | `[x]` | `testCurveShapeValidation`, `testCurveValidationAndHysteresis` |

---

## T3 — Core integration into the application

| ID | Task | Status | Evidence |
|---|---|---|---|
| T3-01 | Make the legacy application include a core header, so the safety state machine is no longer dead code | `[ ]` | Currently zero legacy files include a core header |
| T3-02 | Route the application's EC reads through `IIoBackend` | `[ ]` | See [EC_REGISTER_MAP.md](EC_REGISTER_MAP.md) §5 |
| T3-03 | Route the application's EC writes through the `EcBus` transaction layer | `[ ]` | Must preserve bounded timeouts and readback |
| T3-04 | Drive `Controller` from the application's data cycle instead of the legacy decision path | `[ ]` | The core must become the decision authority, not advisory |
| T3-05 | Delete or quarantine the legacy decision code once the core is authoritative | `[ ]` | Two decision paths is how they diverge |
| T3-06 | Serialise the EC transaction against the application's worker thread | `[ ]` | `EcBus` holds a `std::mutex`; that does not serialise against the legacy thread. This is a real integration hazard, not a formality |
| T3-07 | Map `SafetyState` and `ControlMode` to UI text without bypassing transitions | `[ ]` | [ARCHITECTURE.md](ARCHITECTURE.md) |
| T3-08 | Build the application's state each cycle and assert no fan command without core approval | `[ ]` | Test that fails if the legacy path can command |
| T3-09 | Keep monitor-only operation working when the backend is absent, end to end | `[ ]` | ADR-007 |
| T3-10 | Report backend failure, write failure and readback mismatch into the controller | `[ ]` | T2-13 |
| T3-11 | Do not reintroduce the legacy unconditional `0x31` write on the dual-fan path | `[ ]` | [EC_REGISTER_MAP.md](EC_REGISTER_MAP.md) §10 records this defect |
| T3-12 | Record the integration in an ADR | `[ ]` | Blocks T6 |

---

## T4 — Hardware verification (Phase 0 gate: all blocked)

**Nothing in this section may start until Phase 0 hardware evidence exists.** No
evidence has been collected. These tasks are not estimated, because estimating them
would imply a capability the project does not have.

| ID | Task | Status | Evidence |
|---|---|---|---|
| T4-01 | Run machine identity commands ([HARDWARE_VERIFICATION.md](HARDWARE_VERIFICATION.md) §2) | `[!]` | Needs the target machine |
| T4-02 | Confirm standard T14 Gen 1 Intel (20S0/20S1) vs another model/edition | `[!]` | Needs the target machine |
| T4-03 | Record CPU, graphics, BIOS version and Windows build | `[!]` | Needs the target machine |
| T4-04 | Record Secure Boot and HVCI state | `[!]` | Needs the target machine |
| T4-05 | Record AC/battery baseline temperatures and RPM | `[!]` | Needs the target machine |
| T4-06 | Determine whether TVicPort loads under HVCI | `[!]` | Depends on T5-02 |
| T4-07 | Collect read-only EC evidence | `[!]` | Never write to discover; see ADR-004 |
| T4-08 | Classify candidate EC ports | `[!]` | [EC_REGISTER_MAP.md](EC_REGISTER_MAP.md) §3 |
| T4-09 | Classify the fan-control register | `[!]` | `0x31` is never a valid answer for T14 |
| T4-10 | Classify tachometer registers | `[!]` | Needed before RPM is trusted |
| T4-11 | Classify valid temperature sensors | `[!]` | Replaces every CANDIDATE range |
| T4-12 | Classify fan topology | `[!]` | Single vs dual, read-only |
| T4-13 | Document the BIOS/automatic restore value and verify a readback | `[!]` | Required before any control |
| T4-14 | Complete [`templates/HARDWARE_REPORT.md`](templates/HARDWARE_REPORT.md) | `[!]` | Sanitised report into `docs/reports/` |

---

## T5 — Backend and EC layer

| ID | Task | Status | Evidence |
|---|---|---|---|
| T5-01 | TVicPort adapter implementing `IIoBackend`, for baseline comparison | `[ ]` | Not started. Windows-only; the interface it must satisfy is now covered by tests. |
| T5-02 | PawnIO feasibility spike on a disposable Windows install ([DRIVER_BACKENDS.md](DRIVER_BACKENDS.md) §4) | `[!]` | Genuinely blocked: needs a disposable Windows install, a driver download, and an HVCI-capable machine. No substitute evidence is acceptable. |
| T5-03 | Record backend signature and HVCI result | `[ ]` | Blocked behind T5-02. The matrix location exists and is empty. |
| T5-04 | Monitor-only behavior when the backend is absent | `[-]` | The core already refuses to command without a ready backend (`testCapabilityAndFailureGates`, plus `testBackendNotReadyRefusesBeforeTouchingHardware`). The application-level path is wired in T3. |
| T5-05 | Typed EC protocol layer with IBF/OBF waits, bounded timeouts, retries | `[x]` | `core/ec_protocol.{h,cpp}`. Waits bounded by deadline **and** a derived poll ceiling, so a frozen clock still terminates (`testFrozenClockStillTerminates`). Typed errors preserved across retries (`testPersistentWriteFailureStopsAndKeepsType`). Insane config refused. Nine tests. |
| T5-06 | Fake-backend fault injection: one failed read, repeated failed reads, one failed write, wrong readback, mutex contention, backend shutdown mid-command ([TESTING.md](TESTING.md) §7) | `[x]` | `tests/fake_ec.{h,cpp}` plus eight tests. The contention test runs 4 threads x 200 transactions and the fake rejects any interleaved data-port read. |
| T5-07 | Stuck temperature and stale timestamp fault injection | `[x]` | Three tests. **Documented limitation:** a uniformly stuck sensor set cannot be detected without a second source, so the tests assert the invariant that matters — a stuck source cannot mask a high reading from another — and do not claim a detection that does not exist. |
| T5-08 | Impossible RPM fault injection | `[x]` | Fixed a real defect: `evaluateFanHealth` reported **Healthy** for 65535 RPM. Added `validateFanRpm` with plausibility bounds, sentinel handling (`0xFFFF`, `0x8000`; **0 excluded** so a stopped fan stays distinguishable), and freshness. Two tests. |
| T5-09 | Read-only `ecdiag` tool | `[ ]` | Not started. Depends on a backend; the EC layer it would use is now available. |
| T5-10 | Prove the single-fan path never writes `0x31` | `[x]` | `EcBus::writeRegister` refuses `0x31` **before touching any port**. The opt-in `setFanSelectorWritesAllowed` is **never called anywhere in the tree**, so the single-fan path cannot unlock it by accident — verified by grep, not inspection. Three tests assert against the recorded write trace and the fake's committed-write log, not against intent. |
| T5-11 | Dependency record per [SECURITY.md](SECURITY.md) §3: source, license, version, signature, architecture, uninstall, redistribution | `[ ]` | Not started. |

### T5 defects found and fixed while implementing

Found by the tests, not by reading the code:

- **Ambiguous EC command encoding.** `readCommandBase=0x10` / `writeCommandBase=0x11`
  differ only in a low bit, so reading register 1 and writing register 0 emit the same
  byte. Every read silently became a write. The bases must now differ in their high
  nibble, and an ambiguous encoding is refused with a message naming the collision.
- **Typed errors were being flattened into timeouts.** A persistent `ReadFailure` was
  retried and then reported as `Timeout`, losing the information the caller needs. Only
  transient errors are retried now; a standing condition such as `NotInitialized` or
  `AccessDenied` returns immediately.
- **"Attempted" was recorded as "written."** The fake logged a write before checking
  whether it succeeded, so the T5-10 trace evidence would have overstated what reached
  the EC. Committed writes are recorded separately.
- **`assert` is compiled out under `NDEBUG`.** A Release test build would have run no
  assertions at all while still appearing to pass, and the values used only inside
  assertions became unused, breaking `/W4 /WX`. Tests now use `CHECK`, which has no
  conditional compilation. See `tests/test_check.h`.

### T5 limitations, stated rather than papered over

- The EC port mapping and command encoding are **candidate values**, not hardware
  facts. Phase 0 must establish them by read-only measurement before any write is
  attempted. See [EC_REGISTER_MAP.md](EC_REGISTER_MAP.md) §3 and §9.
- A uniformly stuck sensor set is **not detectable** from a single source.
- The `EcBus` mutex is the portable equivalent of the EC access mutex in
  [EC_REGISTER_MAP.md](EC_REGISTER_MAP.md) §5 step 1. It does **not** serialise against
  the legacy application's own thread — that is T3-06.

---

## T6 — T14 profile and temperature providers

| ID | Task | Status | Evidence |
|---|---|---|---|
| T6-01 | Create the T14 Gen 1 Intel profile from measured hardware facts | `[!]` | Blocked on T4-14 |
| T6-02 | Set the profile verification status correctly; a profile is not verified because it loads | `[ ]` | ADR-001 |
| T6-03 | Configure the explicit single-fan topology | `[ ]` | ADR-003 |
| T6-04 | Replace every CANDIDATE temperature range with a measured one | `[ ]` | Blocked on T4-11 |
| T6-05 | Add the EC temperature provider | `[ ]` | Depends on T3-02 |
| T6-06 | Add sensor freshness and validity checks at the provider | `[ ]` | |
| T6-07 | Add the optional CPU MSR temperature provider | `[ ]` | ADR-008; correct TjMax handling required |
| T6-08 | Define the multi-source selection policy | `[!]` | Blocked on T2-12 |
| T6-09 | Test the profile validator rejects an unverified profile | `[ ]` | Depends on T6-02 |
| T6-10 | Document the profile in [PROFILES.md](PROFILES.md) with its evidence links | `[ ]` | |

---

## T7 — UI, startup, and observability

| ID | Task | Status | Evidence |
|---|---|---|---|
| T7-01 | Display backend status | `[ ]` | |
| T7-02 | Display sensor source and freshness per reading | `[ ]` | |
| T7-03 | Display safety state and failsafe latch | `[ ]` | Must not bypass transitions (T3-07) |
| T7-04 | Display requested command versus measured RPM | `[ ]` | |
| T7-05 | Display a truthful restore status: commanded vs unavailable | `[x]` | `ControlMode::RestoreUnavailable` exists in the core |
| T7-06 | Add an explicit Return to BIOS/automatic action | `[ ]` | |
| T7-07 | Add bounded status/event telemetry | `[ ]` | Blocked on T2-13 |
| T7-08 | Add rotating logs that never enter the source tree | `[ ]` | [BUILD.md](BUILD.md) §7 |
| T7-09 | Add bounded CSV telemetry | `[ ]` | |
| T7-10 | Add a sanitised diagnostic export | `[ ]` | Must redact paths and serials |
| T7-11 | Add a read-only CLI for status and profile validation | `[ ]` | T5-09 |
| T7-12 | Add per-user delayed Task Scheduler startup | `[ ]` | ADR-009: not as SYSTEM |
| T7-13 | Show a monitor-only banner when control is not active | `[ ]` | ADR-007 |
| T7-14 | Make the UI honest about unverified state at all times | `[ ]` | Definition of a safety requirement |
| T7-15 | Test Explorer restart (tray recovery) | `[ ]` | H-18 |
| T7-16 | Test sleep/resume revalidation | `[ ]` | H-15 |
| T7-17 | No requirement may be stated in the UI without a status marker | `[ ]` | T0-10 rule, applied to the UI |

---

## T8 — Release

| ID | Task | Status | Evidence |
|---|---|---|---|
| T8-01 | Complete the build matrix (Win32/x64 x Debug/Release) | `[-]` | Configured; awaits T1-01 |
| T8-02 | Complete the test matrix on all three operating systems | `[-]` | Configured; awaits T1-08 |
| T8-03 | Complete the hardware report | `[!]` | Blocked on Phase 0 |
| T8-04 | Complete the release candidate checklist | `[ ]` | |
| T8-05 | Verify dependency licensing and signatures | `[ ]` | T5-11, T5-02 |
| T8-06 | Create a clean package from a recorded commit | `[ ]` | ADR-016 |
| T8-07 | Create checksums for any distributed artifact | `[ ]` | |
| T8-08 | Test install and uninstall | `[ ]` | |
| T8-09 | Test rollback to a known-good configuration | `[ ]` | |
| T8-10 | Publish known limitations | `[ ]` | [SAFETY.md](SAFETY.md) §9 is mandatory content |
| T8-11 | State plainly that no crash-safe restoration is claimed | `[ ]` | [SAFETY.md](SAFETY.md) §9 |
| T8-12 | Ensure the release cannot auto-load an unreviewed driver | `[ ]` | ADR-014 |
| T8-13 | Sign the release, or state that it is unsigned | `[ ]` | |
| T8-14 | Record the exact toolchain versions used | `[ ]` | |
| T8-15 | Confirm no build artifact or private path is in the package | `[ ]` | T0-10 |

---

## T9 — Optional enhancements

**Not on the critical path.** Do not start any of these before T8. Each increases
complexity without establishing safe EC access (ADR-012).

| ID | Task | Status | Evidence |
|---|---|---|---|
| T9-01 | Named profiles (BIOS, Quiet, Balanced, Performance, Battery saver, Custom) | `[ ]` | Not started; depends on T7 |
| T9-02 | Graphical curve editor with live validation and no direct register editing | `[ ]` | Not started; depends on T7 |
| T9-03 | Foreground-application profiles with an explicit, disclosed allowlist | `[ ]` | Not started; depends on T7 |
| T9-04 | Acoustic optimization via slower changes and minimum dwell | `[ ]` | Not started; depends on T7 |
| T9-05 | Thermal trend warnings | `[ ]` | Not started; depends on T7 |
| T9-06 | Read-only local named-pipe API, disabled by default | `[ ]` | Not started; depends on T7 |
| T9-07 | Separate privileged backend and unprivileged tray processes | `[ ]` | Not started; depends on T7 |
| T9-08 | Additional verified ThinkPad profiles | `[ ]` | Not started; depends on T7 |
| T9-09 | Additional approved I/O backends | `[ ]` | Not started; depends on T7 |
| T9-10 | Windows power-mode integration | `[ ]` | Not started; depends on T7 |
| T9-11 | Temperature/RPM history graph with gap labelling | `[ ]` | Not started; depends on T7 |
| T9-12 | Configuration backup/rollback | `[ ]` | Not started; depends on T7 |

---

## Hardware test matrix (H-01 … H-20)

Every row requires a real machine and a sanitised report in
[`reports/`](reports/README.md). **None has been run.** These are the acceptance tests
for a T14 release; a row is not satisfied by a fake backend, a simulator, or a
passing portable test.

| ID | Test | Expected result | Status |
|---|---|---|---|
| H-01 | Start with backend absent | Monitor-only, no EC writes | `[!]` |
| H-02 | Start with unverified profile | Control refused | `[!]` |
| H-03 | Read current EC state | Valid value or explicit error | `[!]` |
| H-04 | BIOS/automatic restore | Readback matches | `[!]` |
| H-05 | Manual level 1 | Fan/RPM response observed | `[!]` |
| H-06 | Manual level 3 | Fan/RPM response observed | `[!]` |
| H-07 | Manual level 7 | Response observed without overheat | `[!]` |
| H-08 | Single-fan selector safety | `0x31` is never written | `[!]` |
| H-09 | Return to BIOS | BIOS command succeeds on exit | `[!]` |
| H-10 | Smart curve idle | No command oscillation | `[!]` |
| H-11 | Smart curve sustained load | Fan ramps at documented thresholds | `[!]` |
| H-12 | High-temperature guard | BIOS/failsafe latches | `[!]` |
| H-13 | EC read failure | BIOS restore attempted | `[!]` |
| H-14 | Write mismatch | Control stops; failure visible | `[!]` |
| H-15 | Sleep/resume | State revalidated before control | `[!]` |
| H-16 | AC/battery transition | No unsafe profile switch | `[!]` |
| H-17 | Forced application close | Restore behavior documented | `[!]` |
| H-18 | Explorer restart | Tray recovers | `[!]` |
| H-19 | HVCI enabled | Backend result documented | `[!]` |
| H-20 | HVCI/backend blocked | Monitor-only fallback | `[!]` |

---

## Safety gap register

| # | Requirement | Status |
|---|---|---|
| 1 | §4 dwell must not delay a safety-relevant upshift | **Closed** — dwell applies to falling transitions only; a rise applies on the first sample requesting it |
| 2 | §7 fan-off blocked above the manual safety threshold | **Closed** — the guard fails closed when unconfigured |
| 3 | §5 emergency threshold is authoritative | **Closed** — a non-positive or too-low threshold is rejected and the controller stays in monitor-only |
| 4 | §10 reading within a plausible model range | **Closed** — range narrowed to 0…110 °C; the cold end is the severe direction. Values remain CANDIDATE until Phase 0 |
| 5 | §6.2 report whether a restore was actually commanded | **Closed** — `biosRestoreIssued` and `ControlMode::RestoreUnavailable` |
| 6 | §4 at least three consecutive samples agree sufficiently | **Open** — a single oscillating sensor can still pump the fan. Blocked on T2-12 |
| 7 | §6.3 cooldown/acknowledgement recovery | **Open** — the latch is permanent for the process lifetime. Blocked on T2-12 |
| 8 | [PROFILES.md](PROFILES.md) §4 curve rules | **Closed** — minimum point count and first-point ceiling |
| 9 | §6.1 impossible RPM or tachometer behaviour → `Failed` | **Partly closed (T5-08)** — implausible, sentinel and stale readings are rejected. **Open:** there is still no repeated-suspect escalation rule |
| 10 | §11 log each fan command and readback result | **Open** — the portable core has no logging. Blocked on T2-13 |

Six of ten are closed, and all six were hardware-independent. Gaps 6, 7 and 9 are
policy questions rather than defects with an obvious correct answer.

---

## Current blockers

| Blocker | Resolution | Status |
|---|---|---|
| ~~Dwell delays rising fan commands~~ | Fixed 2026-09-27; dwell is downward-only | Closed |
| ~~Manual fan-off has no temperature guard~~ | Fixed 2026-09-27; fails closed when unconfigured | Closed |
| ~~Emergency detection disabled by default~~ | Fixed 2026-09-27; threshold validated, invalid config cannot control | Closed |
| ~~Implausible sensor readings accepted~~ | Fixed 2026-09-27; range narrowed, cold end rejected | Closed |
| ~~Failsafe reported BIOS when nothing was written~~ | Fixed 2026-09-27; restore reporting is now truthful | Closed |
| ~~Single-point curve accepted~~ | Fixed 2026-09-27; minimum point count enforced | Closed |
| No Windows toolchain in the authoring environment | CI provides the evidence; tasks stay `[-]` until it runs | Open |
| Exact machine type unknown | Complete the Phase 0 identity report (T4-01, T4-02) | Open |
| EC map unverified | Read-only hardware report (T4-07 … T4-12) | Open |
| Backend/HVCI compatibility unknown | PawnIO spike (T5-02) and TVicPort load test (T4-06) | Open |
| Final T14 curve unknown | Collect telemetry after control validation | Open |
| Sensor-agreement and failsafe-recovery policy | Product decision, ADR required (T2-12) | Open |

---

## Forbidden practices

These are not style preferences. Each is a way this project could damage hardware or
mislead a reader.

1. **Automatically probing a writable EC register to discover what it does.** Discovery
   is read-only, or it does not happen (ADR-004).
2. **Treating a CANDIDATE value in a source file as hardware evidence.** A number in
   code is a hypothesis until a measurement in `docs/reports/` says otherwise.
3. **Enabling control without verified hardware, profile, topology, backend and
   restore capability** — all five, before the first command.
4. **Accepting a single sensor reading as sufficient** to command a fan.
5. **Failing open.** Any uncertainty, error, or timeout must hand the fan back to BIOS
   behaviour or refuse to touch it.
6. **Claiming crash-safe restoration.** A user-mode process cannot guarantee it
   ([SAFETY.md](SAFETY.md) §9).
7. **Shipping or auto-loading an unreviewed kernel driver** (ADR-014).
8. **Distributing a prebuilt executable** from this repository (ADR-016).
9. **Marking a task complete without its evidence artifact.**
10. **Stating a requirement as a working guarantee** without a status marker.
11. **Presenting a P51/P-series assumption as T14 evidence.** The target is T14 Gen 1
    Intel (20S0/20S1); P-series behaviour is a different machine.

---

## Definition of done

A work package is complete only when **all** of the following hold. This replaces
"it feels finished".

- [ ] Every task in the package is `[x]` with its evidence artifact present.
- [ ] The build is clean from a fresh checkout, with no committed artifacts.
- [ ] CI is green on all three operating systems.
- [ ] No new warnings; core sources build at `/W4` with warnings-as-errors.
- [ ] No raw EC write bypasses the profile/topology gate.
- [ ] Every EC write has a bounded timeout and a readback.
- [ ] No new safety requirement is added without a test that would fail if it regressed.
- [ ] The documentation distinguishes requirements from guarantees.
- [ ] No secrets, serials, usernames, or private paths in any committed artifact.
- [ ] For hardware-touching work: a sanitized report in [reports/](reports/README.md).
- [ ] Known limitations are written down, not implied.
