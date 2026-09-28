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
| Portable-core test functions | 11 core + 42 EC + 31 bridge + 29 ecdiag + 20 policy + 16 backend = 149, plus a self-test for the three checkers that could otherwise stop matching silently (`check_ecdiag_readonly.py`, `check_project_sources.py`, `check_ci_steps.py`) |
| Work packages complete | 1 of 10 (T0); T1 in progress — 2 of 12 verified locally, 10 awaiting a Windows CI run. T5: 05, 06, 07, 08, 09, 10 complete, 04 in progress, 02 blocked, 01/03/11 not started |
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
| T1-08 | Add CI: run `tests/run_core_tests.sh` on Linux, macOS, and Windows | `[-]` | `ci/ci.yml` job `portable-tests`, matrix `ubuntu`/`macos`/`windows`, plus an ASan+UBSan leg. Workflow YAML validated; script runs locally. Awaiting a green run, and awaiting the workflow being enabled. **Not verified.** |
| T1-09 | Add CI: Release build with warnings-as-errors on the core | `[-]` | Job `release-warnings-as-errors` builds both test projects Release x64 and runs them, then asserts `/W4 /WX /permissive- /EHsc` actually reached every core unit. Awaiting a green run, and awaiting the workflow being enabled. **Not verified.** |
| T1-10 | Add CI: `git diff --check` and a link check | `[-]` | Job `hygiene` runs `git diff --check`, `scripts/check_links.py`, a no-tracked-artifacts check, and a UTF-8 check. The link checker was negative-tested (3 seeded defects, all caught) and passes locally. Awaiting a green run, and awaiting the workflow being enabled. **Not verified.** |
| T1-11 | Add a Windows test target so the suite runs on Windows | `[-]` | Seven portable projects — `tests/*.vcxproj` and `tools/ecdiag/ecdiag.vcxproj` — build the same sources as `run_core_tests.sh`, four configurations each, `/W4` `/WX` `/permissive-` `/EHsc`, all registered in `fancontrol/fancontrol.sln`; the application project gained `core\ec_access.cpp`. `scripts/check_project_sources.py` compiles each project's **own** `<ClCompile>` list and runs the result; its first run found four projects that could not link and a duplicated source item, and `check_projects.py` now also fails a project whose `OutputFile` is not its own. XML validated; `check_projects.py` clean. Awaiting execution in CI. **Not verified.** |
| T1-12 | Add static analysis to CI (clang-tidy or MSVC `/analyze`) | `[-]` | Job `static-analysis` runs MSVC `/analyze` on the core (enforced) and the app (reported, `continue-on-error` pending T1-05), and archives a summary. Awaiting a green run, and awaiting the workflow being enabled. **Not verified.** |

**CI enablement blocker.** The workflow is version-controlled at
[`ci/ci.yml`](../ci/ci.yml), not `ci/ci.yml`'s runtime location
`.github/workflows/ci.yml`, because the push credential in this environment is a
GitHub App token without the `workflows` permission and is refused when creating
that path. The file is therefore tracked rather than left unversioned, and
`ci/README.md` records the one-command enable step for a maintainer with the
right permission. No green run exists, so T1-08 … T1-12 stay `[-]`.

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
| T3-01 | Make the legacy application include a core header, so the safety state machine is no longer dead code | `[x]` | Done. `fancontrol.h` includes `core/app_bridge.h`, `core/legacy_backend.h`, `core/legacy_policy.h`; the namespaces are spelled out at each use rather than opened with a using-directive.
| T3-02 | Route the application's EC reads through `IIoBackend` | `[x]` | Done. `core/legacy_backend.{h,cpp}` adapts the existing `ReadByteFromEC`/`WriteByteToEC` rather than reimplementing the protocol. 16 tests, including that a register read through `EcBus` arrives at the port primitives as exactly one ADR-020 transaction and that no port number is ever truncated into a register address. Built but not run on Windows — see the note under the table.
| T3-03 | Route the application's EC writes through the `EcBus` transaction layer | `[x]` | Done. `SetFan` writes only via `AppBridge::apply`, which verifies by readback. Bounded timeouts are `EcBus`'s; the retry loop that used to sit in `SetFan` is gone with the direct write.
| T3-04 | Drive `Controller` from the application's data cycle instead of the legacy decision path | `[x]` | Done for the command path. `SetFan` runs `Controller::update` and applies only what the core authorises. The periodic *sensor* cycle is still the legacy `HandleData`; see the note under the table.
| T3-05 | Delete or quarantine the legacy decision code once the core is authoritative | `[!]` | Blocked, and the reason is structural rather than environmental. What is done: `evaluateIntent` is the only place a UI request becomes a core request, and `check_legacy_ec.py` fails the build on a fan-level write from outside the core. What remains: the legacy `SmartControl` temperature table still computes a level. It no longer reaches a register — it feeds a core request, which is already a real reduction — but it is not deleted. Deleting it means moving the periodic *sensor* cycle onto the core first (T3-04's remaining half), and that work cannot be verified here: `HandleData` is Win32 timer code and the application does not compile on this machine. Deleting the table before the replacement cycle is verified would leave the application with no decision path at all, which is the opposite of authoritative. `evaluateIntent` is now the only place a UI request becomes a core request, and `check_legacy_ec.py` fails the build on a fan-level write from outside the core. The legacy `SmartControl` table still computes a level and is still called; it now feeds a core request rather than a register write, but it is not deleted. Deleting it needs the sensor cycle moved first.
| T3-06 | Serialise the EC transaction against the application's worker thread | `[x]` | Done for the write path. `SetFan` holds `EcAccess` across the bridge's write and its readback, so the worker thread cannot interleave between them. The read path still needs the same treatment and is not yet done.
| T3-07 | Map `SafetyState` and `ControlMode` to UI text without bypassing transitions | `[x]` | Done. `toText(SafetyState)`, `toText(ControlMode)`, `toText(FanHealth)`, `toText(SensorValidity)` and `describe()` are in `core/app_bridge.h`, tested, and `describe` is never empty so a blank status cannot read as healthy. The dialog still shows its own strings; mapping them is UI work.
| T3-08 | Build the application's state each cycle and assert no fan command without core approval | `[x]` | Done. `AppBridge::apply` refuses any output the core did not authorise, before touching the bus, and there is no other method in the class that writes a register. 29 tests, including the assertion that the written register set is exactly `{0x2F}`.
| T3-09 | Keep monitor-only operation working when the backend is absent, end to end | `[x]` | Done. `LegacyBackend` is `Ready` while read-only, reads work, and every write returns `Unsupported`. `evaluateIntent` refuses every write source in monitor-only mode. Tested. Not yet exercised end to end in the running application — that needs Windows.
| T3-10 | Report backend failure, write failure and readback mismatch into the controller | `[x]` | Done. `AppBridge::makeInput` carries `backendFailure`, and the controller's own faults (`writeFailure`, `readbackMismatch`, `fanResponseFailed`) are reported from `apply` results. Tested in both directions: a read failure with restore verified fails safe to the firmware; without verified restore, nothing is issued.
| T3-11 | Do not reintroduce the legacy unconditional `0x31` write on the dual-fan path | `[x]` | Done. No write to `0x31` exists in the repository. `scripts/check_legacy_ec.py` fails the build if one reappears, and `check_legacy_ec_selftest.py` proves the guard still fires. Both verified by reintroducing the defect. ADR-021, `EC_REGISTER_MAP.md` §10.
| T3-12 | Record the integration in an ADR | `[x]` | Done. ADR-021 (refusing to address an individual fan), ADR-022 (superseded) and ADR-023 (the register-write barrier belongs to EcBus; the backend is a port backend).

**T3 evidence note — what has and has not been shown.** Everything marked done in
this table is verified by a runnable artifact in this repository: 120 tests across
five suites, passing plain, under `-O2 -DNDEBUG`, under ASan+UBSan and under TSan,
plus four static checkers. None of that is a Windows build. The application has
never been compiled here, because there is no Windows machine and no MSVC. The
core call sequence `SetFan` makes was typechecked by a separate translation unit
that mirrors it statement for statement, which catches an API mismatch but is not a
build of the application — MSVC-specific issues (sprintf_s overloads, the exact
`MUTEXSEM` semantics, Win32 header interactions) remain unverified until T1-11 runs
on a Windows runner. Treat every T3 row as "implemented and unit-tested", not as
"known to build".

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
| T5-05 | Typed EC protocol layer with IBF/OBF waits, bounded timeouts, retries | `[x]` | `core/ec_protocol.{h,cpp}`. Waits bounded by deadline **and** a derived poll ceiling, so a frozen clock still terminates (`testWaitTerminatesEvenWhenTheClockNeverAdvances`); the ceiling itself is pinned to an exact poll count (`testPollCountIsBoundedPerAttempt`) so it cannot quietly grow. Typed errors preserved across retries (`testPersistentFailureIsRetriedOnlyToTheLimit`). Insane config refused. **22 tests.** |
| T5-06 | Fake-backend fault injection: one failed read, repeated failed reads, one failed write, wrong readback, mutex contention, backend shutdown mid-command ([TESTING.md](TESTING.md) §7) | `[x]` | `tests/fake_ec.{h,cpp}`. Transient vs. persistent failures are modelled separately (`testTransientFailureIsRetried`, `testPersistentFailureIsRetriedOnlyToTheLimit`); a backend that passes the capability probe and then denies the write is a distinct mode (`testAccessDeniedIsNotRetried`), because otherwise `AccessDenied` is unreachable. Shutdown mid-transaction is `testBackendStoppingMidTransactionIsReported`. The contention tests run 4 reader threads plus a concurrent writer, and the fake rejects any interleaved data-port read; verified clean under ThreadSanitizer. |
| T5-07 | Stuck temperature and stale timestamp fault injection | `[x]` | `testStuckTemperatureSource` models a source whose value freezes while its timestamps keep advancing, so a freshness check cannot catch it. **Documented limitation, asserted rather than glossed over:** a uniformly stuck sensor set is not detectable from a single reading stream. What the tests do pin is the invariant that does hold — a stuck source can neither mask a hotter source nor hold the aggregate down, because the aggregate is a maximum over sources. `testStaleAndFutureTimestampsAreRejected` checks the age and skew limits on **both** sides of each boundary, since an off-by-one boundary is an untested boundary. |
| T5-08 | Impossible RPM fault injection | `[x]` | Fixed a real defect: `evaluateFanHealth` reported **Healthy** for 65535 RPM — the one answer that must never be reported for a value that cannot be a measurement. Added `validateFanRpm` (`FanRpmPolicy`, `ValidatedFanRpm`) with plausibility bounds, sentinel handling (`0xFFFF`, `0x8000`; **0 excluded** so a stopped fan stays distinguishable), and freshness; `ControllerInput::timestampMs` carries when the reading was taken. `testImplausibleRpmIsNotReportedHealthy` pins all five rejection classes and then asserts a plausible reading still returns `Healthy`, so the fix is not just "report `Failed` for everything". |
| T5-09 | Read-only `ecdiag` tool | `[x]` | `fancontrol/core/ecdiag.{h,cpp}` (portable engine), `tools/ecdiag/{main.cpp,simulated_ec.{h,cpp}}` (front end and an invented-table reader), `tests/ecdiag_tests.cpp` (**29 tests**), [ECDIAG.md](ECDIAG.md). Read-only is structural, not a promise: `ecdiag` is handed `core::IRegisterReader` (one read operation, no write member) and its include closure cannot reach `EcBus` — enforced by `scripts/check_ecdiag_readonly.py` and its 13-case self-test — and a complete run through a **real `EcBus`** over the fake EC is asserted against the bus's own write trace and the fake's committed-write log (`testFullRunThroughARealBusWritesNoRegister`, `testReadOnlyInvariantFiresWhenARealWriteReachesTheBus`). Modes `--plan` (no I/O at all) and `--simulate`; JSON schema `tpfancontrol.ecdiag/1` with an evidence grade (`plan-only`/`simulated`/`hardware`) and an explicit `not_evidence_reason`, because a report that could pass a simulated run off as a measurement would be worse than no report. Exit codes 0/2/3/4/6/8 with 5 and 7 unreachable by construction. Ports print as 16-bit hex (`0x1604`, never `0x04`). `tools/ecdiag/ecdiag.vcxproj` builds it on Windows. **No live backend exists**: every `--backend` name exits 4 with the reason (T5-01, T5-02), so the tool has never read a machine, and nothing here claims otherwise. |
| T5-10 | Prove the single-fan path never writes `0x31` | `[x]` | `EcBus::writeRegister` refuses `0x31` **before touching any port** (`testFanSelectorWriteIsRefusedBeforeAnyBackendCall` asserts `backend.operationCount() == 0`, not merely that the call returned an error). The refusal is checked against **all 256** values, because a rule that only catches the value the code happens to use is not a rule. `setFanSelectorWritesAllowed` is **never called anywhere in product code** — verified by grep over `*.cpp`/`*.h` excluding `tests/`, which finds only the declaration and the definition. Assertions are made against the bus's own write trace and the fake's committed-write log, never against intent. |
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
- **Out-of-range register addresses were silently aliased.** The command byte carries
  a 4-bit address, and the bus masked with `address & 0x0F`. A write to register `0x2A`
  was therefore accepted and issued as a write to register `0x0A` — a *different
  register*, with no error reported. On a bus where the wrong register can stop a fan,
  that is worse than a refusal. An address that does not fit the encoding is now
  rejected with `Unsupported` before any port is touched
  (`testAddressOutsideTheEncodingIsRefusedNotTruncated`).
- **`WriteFailure` was the one retryable error that was never retried.** A failure of
  the *command* write returned immediately, while an identical failure of a status read
  was retried. That inconsistency is exactly what hides a transient backend fault, so
  the command write now follows the same policy as every other step.
- **The write trace recorded address `0` for every command.** The T5-10 check walks the
  trace looking for `0x31`; a trace whose address field is always zero would have made
  that check pass vacuously. The encoded address is now recorded.
- **`assert` is compiled out under `NDEBUG`.** A Release test build would have run no
  assertions at all while still appearing to pass, and the values used only inside
  assertions became unused, breaking `/W4 /WX`. Tests now use `CHECK`, which has no
  conditional compilation. See `tests/test_check.h`. This was verified directly, not
  assumed: the same failing condition aborts under `-DNDEBUG` with `CHECK` and exits 0
  with `assert`.

Found while building the diagnostic and its Windows targets, not by running the
suites — this is the class of defect the suites structurally cannot see:

- **The extracted EC configuration was not in the Windows projects.** Splitting
  `ec_access.{h,cpp}` out of `ec_protocol.h` (ADR-024) gave four of the five test
  projects an undefined reference the moment `EcBus` validated a configuration.
  The Linux runner compiles every core source for every suite, so nothing here
  noticed; only `scripts/check_project_sources.py`, which compiles a project's own
  `<ClCompile>` list, did. `legacy_backend_tests.vcxproj` was additionally missing
  `fake_ec.cpp` — and so had never compiled the bus it exercises — while
  `app_bridge_tests.vcxproj` listed `core_types.cpp` twice.
- **Every test project wrote `core_tests.exe`.** All five were cloned from the
  same template and the linker output name came with it, so the four executables
  the workflow runs from `out\tests\<platform>\<configuration>\<suite>.exe` did
  not exist, and whichever project linked last would have overwritten the others.
  A job that runs `core_tests.exe` five times and passes is a job that ran one
  suite and reported success for six. The names are now per-project, and
  `check_projects.py` fails any project whose `OutputFile` is not recognisably its
  own — a rule validated by putting `core_tests.exe` back into one project and
  watching it fail, then restoring the file from the bytes read beforehand.
- **The tool reported ports as bytes.** `hexByte(port & 0xFF)` printed the
  configured status port `0x1604` as `0x04`. Every port in this project is 16
  bits, so the truncation would have made the report's own configuration block
  wrong — in the one artifact meant to be quoted as evidence. Ports now go
  through `hexWord`, and the tests assert the full value.
- **A plan-only report looked empty rather than explicitly unread.** The engine
  performs no I/O in plan-only mode, so the results table was rendered from an
  empty result set and the report was indistinguishable from one whose reads had
  found nothing. The table is now driven by the plan, with `not read` in the
  value cells, and `PlanOnly` is a distinct outcome from `Ok`.

The last two items of the earlier list are the reason the fake records *committed*
writes separately from *attempted* ones. A log that records the attempt as the
outcome would have made all four of those look like passing tests.

### Verification actually performed

Run locally on Linux with g++ (no MSBuild, Visual Studio or Windows in this
environment). Every cell below was executed, not inferred:

| Configuration | `core_tests` | `ec_protocol_tests` | `app_bridge_tests` | `ecdiag_tests` | `legacy_policy_tests` | `legacy_backend_tests` | Total |
|---|---|---|---|---|---|---|---|
| `-Wall -Wextra -Werror -pedantic` | 11 | 42 | 31 | 29 | 20 | 16 | **149** |
| `-O2 -DNDEBUG` | 11 | 42 | 31 | 29 | 20 | 16 | **149** |
| `-fsanitize=address,undefined` | 11 | 42 | 31 | 29 | 20 | 16 | **149** |
| `-fsanitize=thread` | 11 | 42 | 31 | 29 | 20 | 16 | **149** |

`core_tests` reports no count: it is the original suite and was not renumbered
when the others were added. The other five print their own count and fail if it is
wrong.

The `-DNDEBUG` row is not redundant. Release defines `NDEBUG`, which compiles
`assert` out entirely, so a suite written with `assert` runs zero checks in
Release and still exits 0. `tests/test_check.h` exists because that failure is
invisible, and running the row is how it is ruled out rather than assumed.

The static checks below run alongside the suites, and each has been shown to fail
on a tree that violates it:

| Checker | What it catches | Proven by |
|---|---|---|
| `check_projects.py` | A `Project(` with no `EndProject`, a missing project file, a `vcxproj` listing a source that is not on disk, a project unmapped into one of the four configurations, and a project whose `OutputFile` is not its own — which is how five projects came to write `core_tests.exe` | run against the previous broken solution; the output-name rule demonstrated by reintroducing the defect into `legacy_policy_tests.vcxproj` |
| `check_legacy_ec.py` | A write to the fan-selector register, or a fan-level write from outside the core, anywhere in the legacy sources | both defects reintroduced into `fanstuff.cpp`; each reported once, by the correct rule |
| `check_legacy_ec_selftest.py` | The guard above having silently stopped matching — 17 cases, half of which must not match | found a `0x2F`/`0x2f` gap, a prefix bug, and a rule that could not fire at all |
| `check_ecdiag_readonly.py` | The diagnostic reaching a write: an `#include` that opens the closure to `EcBus`, a write API or write-enabling call in the tool's own sources, or the runtime trace assertions having been deleted. 13-case self-test | validated by adding `#include "ec_protocol.h"` to `ecdiag.h` on purpose; the guard failed with the right rule and the file was restored from bytes read beforehand |
| `check_project_sources.py` | A Windows project whose own `<ClCompile>` list is incomplete, will not compile, or names a file that is not there. 5-case self-test | its first run against the tree as it stood found four projects unable to link, and the missing-source case is one of its own self-test cases |
| `check_links.py` | A relative link or anchor in the documentation that does not resolve | run against the previous tree |
| `check_ci_steps.py` | The portable steps of `ci/ci.yml` no longer working — it executes them locally | caught a suite that stopped linking because its fake source was wrong |

`tests/run_core_tests.sh` builds and runs all six suites, and builds and
smoke-tests the `ecdiag` tool, and is the single entry point used by CI. The
Windows build (eight projects, four configurations, `/W4 /WX`, MSVC `/analyze`)
is **not** verified here — no Windows toolchain exists in this environment — and
is recorded as awaiting CI evidence rather than as done.

### T5 limitations, stated rather than papered over

- The EC port mapping and command encoding are **candidate values**, not hardware
  facts. Phase 0 must establish them by read-only measurement before any write is
  attempted. See [EC_REGISTER_MAP.md](EC_REGISTER_MAP.md) §3 and §9.
- **The T5 placeholder encoding was contradicted by the legacy source.** Preparing
  T3-03 meant reading `fancontrol/portio.cpp`, which records a different protocol
  entirely: a command byte (`0x80` read, `0x81` write) followed by a **full-byte**
  address on the data port. Under the T5 placeholder, `0x2F`, `0x31`, `0x78`, `0x84`
  and `0xC0` were all unreachable — *every* register the application uses. `EcBus` now
  models the protocol the source records (ADR-020), and
  `testDefaultCommandBytesMatchTheLegacySource` pins the constants so they cannot
  drift from `portio.cpp` silently.
- **The constants are transcribed, not measured.** Being in the repository is a record
  of what the shipped application does; it is not a record of what the hardware
  requires, and generic documentation describes a different `0x62`/`0x66` interface
  entirely. Phase 0 must still confirm them read-only. This must not be skipped on the
  argument that the numbers came from our own source.
- A uniformly stuck sensor set is **not detectable** from a single source. The tests
  assert this limitation directly rather than working around it, and pin the property
  that does hold: one stuck source cannot mask or mask-out a differing source.
- The `EcBus` mutex is the portable equivalent of the EC access mutex in
  [EC_REGISTER_MAP.md](EC_REGISTER_MAP.md) §5 step 1. It does **not** serialise against
  the legacy application's own thread — that is T3-06.
- **`ecdiag` has never read a machine.** There is no live backend, so every
  `--backend` name exits 4 and the only runs that exist are `--plan` (no I/O) and
  `--simulate` (an invented table). Its read-only guarantee is structural and is
  exercised against a real `EcBus` over a fake EC; it has not been exercised
  against hardware, and a hardware report cannot be produced by this tool until
  T5-01 or T5-02 lands. See [ECDIAG.md](ECDIAG.md) §7.
- A value read from an offset does not establish that the offset means what its
  candidate label says. `ecdiag` records raw bytes and refuses to summarise them
  into a temperature, an RPM or a fan state; correlation stays a human step
  ([HARDWARE_VERIFICATION.md](HARDWARE_VERIFICATION.md) §7).

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
| T7-11 | Add a read-only CLI for status and profile validation | `[ ]` | Depends on T5-09 (complete): the portable read-only tool exists, but this is the in-application command surface from [CLI_DIAGNOSTICS.md](CLI_DIAGNOSTICS.md) §1, plus the profile validator. |
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
| 9 | §6.1 impossible RPM or tachometer behaviour → `Failed` | **Partly closed (T5-08)** — implausible, sentinel, stale and future-dated readings are all rejected, and `evaluateFanHealth` now reports `Failed` for every one of them instead of `Healthy`. **Open:** there is still no repeated-suspect escalation rule, and a uniformly stuck sensor set remains undetectable from a single reading stream |
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
