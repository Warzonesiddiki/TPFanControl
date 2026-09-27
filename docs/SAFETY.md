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

Do not automatically return from failsafe until readings are valid for a configured cooldown period. A later version may require explicit user acknowledgement.

## 7. Manual mode restrictions

The normal UI should expose only verified normal levels. The following rules apply:

- fan-off is blocked when temperature is above the manual safety threshold;
- disengaged/full-speed mode is disabled unless an expert setting is deliberately enabled;
- arbitrary byte values are not accepted from the normal UI;
- a manual command is not accepted when the temperature provider is invalid;
- manual mode automatically yields to the failsafe state.

## 8. Crash and power-loss limitation

A user-mode process cannot guarantee restoration after a kernel crash, forced power loss, or hardware reset. The project must state this clearly.

Possible future mitigations:

- verified EC watchdog support;
- a small supervised service with a heartbeat;
- a separate controller process;
- BIOS/EC behavior that independently restores automatic control.

None of these may be claimed as protection until tested on the target hardware.

## 9. Sensor validity

A temperature reading is valid only if:

- backend read succeeded;
- timestamp is fresh;
- value is within a plausible model range;
- source-specific validity bits pass;
- the value is not a sentinel or stale repeated error;
- any configured offset is applied after raw validation.

Sensor offsets must not be used to hide a failed sensor.

## 10. Logging safety events

Log at least:

- backend initialization result;
- profile activation;
- each fan command and readback result;
- failsafe reason;
- maximum observed temperature;
- shutdown restore result;
- configuration validation failures.

Do not log serial numbers, user names, or other unrelated personal data by default.

## 11. Safety review checklist

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
