# Modernization roadmap

**Status:** Draft ready for Phase 0
**Target release:** TPFanControl T14 Gen 1 Intel MVP
**Owner:** Project maintainer
**Rule:** A phase is not complete because the code compiles; it is complete only when its evidence and exit criteria are recorded.

## Strategy

Use an incremental migration rather than a large rewrite. The current repository is a legacy Win32 application with direct TVicPort access and dual-fan logic. The first objective is a safe, validated single-fan T14 path. UI expansion and generalized multi-model support come later.

```text
Phase 0  Identify laptop and EC behavior
    |
Phase 1  Clean build + backend feasibility
    |
Phase 2  Hardware abstraction + EC protocol
    |
Phase 3  T14 single-fan controller + temperature providers
    |
Phase 4  Safety state machine + profile validation
    |
Phase 5  Minimal UI, startup, and logging
    |
Phase 6  Hardware qualification + release
    |
Phase 7  Optional profiles, curve editor, and service split
```

## Phase 0 — hardware and environment verification

### Objective

Replace assumptions with evidence from the exact T14 unit.

### Deliverables

- Completed [HARDWARE_VERIFICATION.md](HARDWARE_VERIFICATION.md)
- A copied and completed `docs/templates/HARDWARE_REPORT.md`
- Exact machine type, CPU, BIOS, Windows build, Secure Boot, and HVCI state
- Read-only EC dump or diagnostic evidence
- Baseline fan and temperature measurements on AC and battery
- A decision on whether the current TVicPort backend can load

### Exit criteria

- The exact target is known.
- Candidate EC ports/registers are classified as `verified`, `candidate`, or `unknown`.
- A safe BIOS/automatic restore procedure is documented.
- No automatic EC write probing is required.

### Stop conditions

Stop implementation if the exact machine is not a standard T14 Gen 1 Intel, or if no trusted backend can access the EC. Update the target profile instead of guessing.

## Phase 1 — reproducible build and backend feasibility

### Objective

Build the source cleanly and determine whether low-level access is possible with the security settings on the laptop.

### Deliverables

- Clean VS2022 build from source
- v143/Windows SDK project migration
- `Debug|Win32`, `Release|Win32`, `Debug|x64`, and `Release|x64` configurations as applicable
- `IIoBackend` interface
- Existing TVicPort adapter for compatibility testing
- Backend feasibility report for PawnIO
- No third-party driver binaries committed to the repository

### Exit criteria

- Clean build succeeds without relying on checked-in objects.
- Pointer-sized Win32 APIs are correct for x64.
- Driver load success/failure is reported clearly.
- A monitor-only path works when the backend is unavailable.
- The backend interface can be tested with a fake implementation.

### Stop conditions

Do not proceed to fan writes if the backend is not trusted, signed/approved for the target policy, or capable of bounded error reporting.

## Phase 2 — hardware abstraction and EC protocol

### Objective

Move all port and EC transactions behind a testable, typed layer.

### Deliverables

- `IoBackend` interface and implementations
- Typed EC protocol functions with timeouts and retry limits
- Named mutex/EC access serialization
- Structured hardware error codes
- Fake backend tests for timeouts, corrupt reads, and write mismatch
- Read-only `ecdiag` tool or equivalent diagnostic mode

### Exit criteria

- No application code calls TVicPort/PawnIO directly.
- Every EC transaction has a timeout.
- Every manual fan write has readback verification.
- A failed transaction cannot leave the mutex held.
- The fake backend proves the error paths.

## Phase 3 — T14 profile and temperature providers

### Objective

Implement the T14 single-fan path only after the EC evidence exists.

### Deliverables

- `T14Gen1-Intel` hardware profile
- Explicit `FanTopology=single`
- No `0x31` fan-selector writes in the single-fan path
- Validated fan-control and tachometer access
- Temperature-provider interface
- EC temperature provider
- Optional CPU MSR provider with correct validity and TjMax handling
- Conservative initial fan curve

### Exit criteria

- Manual levels can be tested and returned to BIOS mode.
- RPM, when supported, is plausible and labelled with its source.
- At least one trusted temperature source is available.
- Invalid or stale temperatures prevent fan-off decisions.
- The real T14 passes the manual-control test matrix.

## Phase 4 — safety and profile engine

### Objective

Make the controller fail safe and make curves deterministic.

### Deliverables

- Explicit up/down hysteresis
- Minimum dwell time between commands
- Safety state machine
- Emergency BIOS restore
- Manual-mode temperature guard
- Versioned configuration validation
- Separate legacy dual-fan profile

### Exit criteria

- Safety tests pass with a fake backend.
- High-temperature and read-error transitions are logged.
- Invalid configuration is rejected before control starts.
- The T14 profile cannot select a second fan.

## Phase 5 — UI, startup, and logging

### Objective

Expose the stable controller through the existing application without changing the safety model.

### Deliverables

- Existing tray/dialog UI updated with backend and safety status
- Explicit return-to-BIOS action
- Rotating logs and CSV telemetry
- Per-user delayed Task Scheduler startup with elevation
- Explorer restart/tray recovery
- DPI-safe pointer and window handling

### Exit criteria

- The tray application never runs as `SYSTEM` merely to display UI.
- Startup is optional and removable.
- Logs do not grow without a bound.
- The UI remains usable at 100%, 125%, and 150% scaling.

## Phase 6 — qualification and release

### Objective

Produce a reproducible, supportable release.

### Deliverables

- Completed hardware test report
- Completed release checklist
- Clean Release build artifacts produced outside the source tree
- Installation and rollback instructions
- Known limitations and compatibility matrix
- Signed application only if signing is available; otherwise honest unsigned-build documentation

### Exit criteria

- All release gates in [RELEASE.md](RELEASE.md) pass.
- The release has a known-good recovery procedure.
- No claim is made for hardware or Windows configurations that were not tested.

## Phase 7 — optional enhancements

Only begin after Phase 6:

- AC/battery profile switching
- Multiple named profiles
- Graphical curve editor
- Foreground-application profiles
- Separate service and tray processes
- Additional ThinkPad models
- Additional backends

## Milestone tracking

| Milestone | Status | Evidence |
|---|---|---|
| M0 Exact hardware identified | Not started | Hardware report |
| M1 Clean source build | Not started | Build log |
| M2 Backend feasibility | Not started | Backend report |
| M3 Typed EC layer | Not started | Unit/fake-backend tests |
| M4 T14 manual control | Not started | Hardware test report |
| M5 Safe smart mode | Not started | Safety and curve tests |
| M6 Windows MVP release | Not started | Release checklist |
| M7 Optional UX features | Not started | Feature-specific evidence |
