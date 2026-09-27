# Implementation checklist

Use this file as the project execution board. Do not mark an item complete without evidence.

Legend:

- `[ ]` not started
- `[-]` in progress
- `[x]` complete
- `[!]` blocked or requires a decision

## Phase 0 — hardware verification

- [ ] Run exact machine identity commands.
- [ ] Confirm standard T14 Gen 1 Intel vs another model/edition.
- [ ] Record CPU and graphics configuration.
- [ ] Record BIOS version and Windows build.
- [ ] Record Secure Boot and HVCI state.
- [ ] Record AC/battery baseline temperatures and RPM.
- [ ] Determine whether TVicPort loads.
- [ ] Collect read-only EC evidence.
- [ ] Classify candidate EC ports.
- [ ] Classify fan-control register.
- [ ] Classify tachometer registers.
- [ ] Classify valid temperature sensors.
- [ ] Classify fan topology.
- [ ] Document BIOS/automatic restore.
- [ ] Complete `templates/HARDWARE_REPORT.md`.

## Phase 1 — build and backend feasibility

- [ ] Clean build from source using Visual Studio 2022.
- [ ] Remove dependency on checked-in generated objects.
- [ ] Fix baseline compiler errors and warnings.
- [ ] Add/configure x64 build.
- [ ] Audit pointer-sized Win32 APIs.
- [ ] Define `IIoBackend`.
- [ ] Add fake backend.
- [ ] Add TVicPort adapter for baseline comparison.
- [ ] Perform PawnIO feasibility spike.
- [ ] Record backend signature/HVCI result.
- [ ] Add monitor-only behavior.

## Phase 2 — EC layer

- [ ] Move all I/O behind the backend interface.
- [ ] Use typed byte/word values.
- [ ] Add bounded EC timeouts.
- [ ] Add mutex acquisition/release guards.
- [ ] Add structured EC errors.
- [ ] Add readback verification.
- [ ] Add `ecdiag` read-only tool or equivalent.
- [ ] Add fake timeout/failure tests.
- [ ] Prove single-fan path does not write `0x31`.

## Phase 3 — T14 controller and sensors

- [ ] Create T14 Gen 1 Intel profile.
- [ ] Set profile verification status correctly.
- [ ] Configure explicit single-fan topology.
- [ ] Add EC temperature provider.
- [ ] Add sensor freshness/validity checks.
- [ ] Add optional CPU temperature provider.
- [ ] Implement correct MSR validity/TjMax handling if used.
- [ ] Test manual level 1.
- [ ] Test manual level 3.
- [ ] Test manual level 7.
- [ ] Test BIOS/automatic restore.
- [ ] Complete physical manual-control report.

## Phase 4 — safety and profiles

- [ ] Add explicit hysteresis.
- [ ] Add dwell time.
- [ ] Add startup validation state.
- [ ] Add failsafe BIOS state.
- [ ] Add high-temperature override.
- [ ] Add invalid/stale sensor handling.
- [ ] Add configuration validation.
- [ ] Add legacy dual-fan profile isolation.
- [ ] Complete fake-backend safety tests.
- [ ] Complete high-temperature/failure hardware tests.

## Phase 5 — UI, startup, and logs

- [ ] Display backend status.
- [ ] Display sensor source and freshness.
- [ ] Display safety/failsafe state.
- [ ] Add explicit return-to-BIOS action.
- [ ] Fix x64 window pointer APIs.
- [ ] Add rotating logs.
- [ ] Add bounded CSV telemetry.
- [ ] Add per-user delayed Task Scheduler startup.
- [ ] Test Explorer restart.
- [ ] Test sleep/resume.

## Phase 6 — release

- [ ] Complete build matrix.
- [ ] Complete test matrix.
- [ ] Complete hardware report.
- [ ] Complete release candidate checklist.
- [ ] Verify dependency licensing and signatures.
- [ ] Create clean package.
- [ ] Create checksums.
- [ ] Test install/uninstall.
- [ ] Test rollback.
- [ ] Publish known limitations.

## Phase 7 — optional enhancements

- [ ] Named profiles.
- [ ] AC/battery switching.
- [ ] Curve editor.
- [ ] Foreground-app triggers.
- [ ] Separate service/tray processes.
- [ ] Additional ThinkPad profiles.
- [ ] Additional approved backends.

## Current blockers

| Blocker | Owner | Resolution | Status |
|---|---|---|---|
| Exact machine type unknown | User/project maintainer | Complete Phase 0 identity report | Open |
| EC map unverified | Project maintainer | Complete read-only hardware report | Open |
| Backend/HVCI compatibility unknown | Project maintainer | Run backend feasibility test | Open |
| Final T14 curve unknown | Project maintainer | Collect telemetry after control validation | Open |
