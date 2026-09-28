# Safety model

## 1. Scope

TPFanControl writes to a laptop EC. A software bug can leave a manual fan command active, misread temperature, or interfere with firmware control. Safety is therefore a core subsystem, not a final UI feature.

This document defines what the application may do and what it must refuse to do.

## 2. Safety principles

1. BIOS/automatic control is the safe default.
2. Unknown hardware is not controllable hardware.
3. Invalid or stale temperature data cannot justify turning a fan off.
4. Every write must be bounded, verified, and logged.
5. Every fatal path must attempt a BIOS/automatic restore.
6. Failure to restore must be visible to the user.
7. User-mode crash recovery must not be overstated.

## 3. Safety states

```text
MONITOR_ONLY
  No manual EC writes. Used at startup or when backend/sensors are unavailable.

VALIDATING
  Collecting consecutive valid readings before control is enabled.

CONTROLLED
  Smart or manual profile is active and every command passes safety gates.

FAILSAFE_BIOS
  Application has requested BIOS/automatic control and will not issue normal
  commands until the failure is cleared.

STOPPING
  Normal shutdown path; restore BIOS/automatic and close the backend.
```

## 4. Startup gate

Control may start only when all conditions are true:

- exact hardware profile selected;
- backend initialized;
- EC mutex can be acquired;
- EC reads are fresh and valid;
- temperature provider is valid;
- BIOS/automatic restore value is known;
- configuration passed validation;
- at least three consecutive samples agree sufficiently;
- no failsafe latch is active.

If any condition fails, remain in `MONITOR_ONLY`.

### 4.1 Control is also unreachable at the I/O layer

The startup gate is a decision, and a decision can be coded wrong. There is a
second, independent barrier below it.

`LegacyBackend`, the only `IIoBackend` the application uses, is constructed
**read-only**. There is no `makeWritable()`; the one call that makes it read-only
cannot be undone through the object's public surface (ADR-022). In that state
every write returns `IoErrorCode::Unsupported` with a message saying the register
map has not been verified on this machine — not `NotInitialized`, because the
driver is not the reason, and sending a user to hunt a driver problem that does
not exist is its own small failure.

The read-only backend reports `BackendState::Ready`, not `Faulted`. It does
everything monitor-only operation needs, and calling it `Faulted` would suggest
something is wrong when it is working exactly as configured.

This matters because the two barriers fail differently. A coding error in the
startup gate produces a command the application believes is authorised. A coding
error in the I/O layer produces a `Unsupported` that the controller reports and
fails safe on. Neither is a substitute for the other, and both are required
before any machine reaches a hardware report.

### 4.2 A refusal is a correct outcome and is shown as one

`evaluateIntent` and `Controller::update` both return refusals, and the
application reports them in the status line and the trace rather than swallowing
them or collapsing them into a single "FAILED!!". A refused write, a failed
write and a failed readback are three different faults with three different
causes, and the legacy code called all three the same thing — which is part of
why the fan-selector defect went unnoticed for so long.

The user-visible consequence is that "the core said no" and "the write failed"
are distinguishable, and that a refusal states plainly that no register was
written. A refusal that leaves the user guessing whether something happened is
only half an answer.

## 5. Normal command gate

Every requested command passes this sequence:

```text
Profile command
  -> allowed command check
  -> hardware topology check
  -> temperature validity check
  -> emergency threshold check
  -> dwell/hysteresis check
  -> EC write
  -> readback
  -> status update
```

A single-fan profile must never write a second-fan selector.

## 6. Failsafe triggers

Enter `FAILSAFE_BIOS` when any of the following occurs:

- emergency temperature reached;
- repeated EC read failures;
- repeated EC write failures;
- write/readback mismatch;
- backend disconnect;
- no valid temperature within the freshness window;
- impossible RPM or tachometer behavior during a commanded state;
- configuration is changed to an invalid value while running;
- application shutdown begins.

Failsafe behavior:

1. stop normal profile commands;
2. attempt the verified BIOS/automatic command;
3. read it back;
4. log the result;
5. notify the user if verification fails;
6. remain in monitor-only mode.

Do not automatically return from failsafe until readings are valid for a configured cooldown period. The portable controller additionally latches emergency/failsafe decisions so that a temporary temperature drop cannot silently re-enable control. A future recovery flow may require explicit user acknowledgement and a fresh startup-validation sequence.

### 6.1 What "impossible RPM or tachometer behavior" means in practice

The trigger above is only meaningful if an impossible tachometer reading can be told
apart from a real one. The portable core implements this as `validateFanRpm`
(`fancontrol/core/sensor_validation.cpp`), and `evaluateFanHealth` consults it before
any comparison against the commanded level. A reading is rejected when it is:

- **negative** — no unsigned register can produce this, so it is a sign or widening
  error;
- **a sentinel** (`0xFFFF`, `0x8000`) — these mean "no measurement" on some firmware.
  **`0` is deliberately not a sentinel**: a stopped fan genuinely reads 0 RPM, and
  conflating the two would hide the exact condition this section exists to catch;
- **above the plausible maximum** — the signature of a raw register byte pair read as
  unsigned without scaling, which is what a mis-mapped tachometer looks like;
- **stale or future-dated** — an old or unaged reading is not evidence about the fan
  right now.

Anything rejected makes `evaluateFanHealth` return `Failed`, not `Suspect` and
certainly not `Healthy`. This was a real defect: before T5-08 the function returned
`Healthy` for 65535 RPM, which is the one answer that must never be produced for a
value that cannot be a measurement.

The plausibility bounds are **candidate values, not hardware evidence**. They exist to
reject sentinels and sign errors, not to describe any particular machine. Phase 0 must
derive the real range from the target hardware before control is enabled.

**What this does not cover, stated plainly.** A *uniformly* stuck sensor set — every
source frozen at the same plausible value — is **not detectable** from a single reading
stream. Freshness checks cannot see it, because a stuck source can keep advancing its
timestamps. What the code does guarantee is the weaker property that a single stuck
source can neither mask a hotter source nor hold the aggregate reading down, because
the aggregate is a maximum over sources rather than a reading of any one source. This
is asserted in `testStuckTemperatureSource` rather than left to inference. Closing the
gap entirely requires an independent second source or a repeated-suspect escalation
rule, which is task T2-04 and remains open.

## 7. Manual mode restrictions

The normal UI should expose only verified normal levels. The following rules apply:

- fan-off is blocked when temperature is above the manual safety threshold;
- disengaged/full-speed mode is disabled unless an expert setting is deliberately enabled;
- arbitrary byte values are not accepted from the normal UI;
- a manual command is not accepted when the temperature provider is invalid;
- manual mode automatically yields to the failsafe state.

## 8. Manual override, fan health, and power policies

A manual override is temporary and must include an expiry. It is cancelled by invalid/stale required sensor data, backend failure, readback mismatch, application restart, or an emergency state. The UI must show the remaining time and provide an immediate Return to BIOS/automatic action.

Fan-health detection is meaningful only when the selected profile verifies a tachometer. It must distinguish unsupported RPM from a non-responsive fan and must not issue repeated blind writes after a suspect result.

AC/battery or workload policies select among already validated profiles; they do not grant new hardware permissions. A policy change is logged and must retain the current safety state until the replacement policy passes validation.

See [CONTROL_ALGORITHM.md](CONTROL_ALGORITHM.md) for deterministic curve, hysteresis, dwell, ramp, and health behavior.

## 9. Crash and power-loss limitation

A user-mode process cannot guarantee restoration after a kernel crash, forced power loss, or hardware reset. The project must state this clearly.

Possible future mitigations:

- verified EC watchdog support;
- a small supervised service with a heartbeat;
- a separate controller process;
- BIOS/EC behavior that independently restores automatic control.

None of these may be claimed as protection until tested on the target hardware.

## 10. Sensor validity

A temperature reading is valid only if:

- backend read succeeded;
- timestamp is fresh;
- value is within a plausible model range;
- source-specific validity bits pass;
- the value is not a sentinel or stale repeated error;
- any configured offset is applied after raw validation.

Sensor offsets must not be used to hide a failed sensor.

## 11. Logging safety events

Log at least:

- backend initialization result;
- profile activation;
- each fan command and readback result;
- failsafe reason;
- maximum observed temperature;
- shutdown restore result;
- configuration validation failures.

Do not log serial numbers, user names, or other unrelated personal data by default.

## 12. Safety review checklist

Before merging a hardware-control change:

- [ ] Does it add or change an EC write?
- [ ] Is the register verified on the target model?
- [ ] Is the write behind the profile/topology gate?
- [ ] Is the value range restricted?
- [ ] Is readback performed?
- [ ] Is timeout behavior tested?
- [ ] Is the mutex released on every path?
- [ ] Does failure enter BIOS failsafe?
- [ ] Is the operation covered by a fake-backend test?
- [ ] Is documentation updated?
