# Control algorithm and manual-control policy

This document defines deterministic behavior for curves, hysteresis, dwell time, ramping, manual overrides, and fan-health decisions. It does not authorize any unverified EC address or backend.

## 1. Control inputs

The controller consumes a snapshot, not individual UI fields:

```text
Timestamp
Valid temperature readings with source and age
Optional verified fan RPM
Current requested fan command
Current measured fan state
Power source
Selected profile and verification status
Backend capability and error state
Safety state
Manual-override expiry, if any
```

If a required input is missing or stale, the controller must choose monitor-only or BIOS/failsafe behavior instead of guessing. Control is opt-in: profile eligibility alone must not start fan control. An explicit activation request is required, and Return to BIOS/automatic clears that request.

## 2. Curve rules

A curve is an ordered list of temperature/command points. Validation must reject:

- fewer than the profile's required number of points;
- duplicate or descending temperatures;
- descending command levels unless the profile explicitly supports them;
- commands outside the profile's allowed range;
- a normal curve that can command an unverified fan channel;
- thresholds that make the emergency limit lower than a normal operating threshold;
- a profile that lacks a documented BIOS/automatic restore command.

The first implementation should use piecewise-constant levels or explicitly documented linear interpolation. It must not silently mix the two.

## 3. Hysteresis

Use different thresholds for increasing and decreasing a command. The controller should not change level merely because a reading moves by a fraction around one boundary.

For each curve boundary:

```text
increase when temperature >= up threshold
 decrease when temperature <= down threshold
otherwise retain the last safe command
```

Hysteresis values are profile data. The validator must ensure down thresholds are below their corresponding up thresholds.

## 4. Dwell time and ramping

- A normal command must remain active for the configured minimum dwell time before another normal command is considered.
- A command should not jump through multiple normal levels unless the temperature is in the emergency region.
- Ramp-down should be slower than ramp-up where noise reduction is a goal.
- Safety overrides, backend failure, and BIOS restoration are not blocked by normal dwell time.
- The first valid control decision requires the startup validation sample count defined by the profile.

The implementation must use an injected clock in tests. It must not rely on wall-clock sleeps in unit tests.

## 5. Manual override

A manual override is a temporary request, not a new permanent profile:

- it has an explicit expiry timestamp;
- it is visible in status and logs;
- it is cancelled on restart unless explicitly re-approved by policy;
- it is cancelled on invalid/stale required temperature data;
- it is cancelled on backend or readback failure;
- it cannot command a value outside the verified profile range;
- it can be ended immediately with Return to BIOS/automatic.

The manual override must never suppress an emergency safety state.

## 6. Safety precedence

Higher-priority decisions override lower-priority requests:

```text
Thermal emergency / backend failure / sensor failure
  > BIOS/automatic restoration
  > verified emergency command
  > normal automatic curve
  > timed manual override
  > UI request
```

The exact ordering between emergency command and BIOS restoration depends on the verified hardware behavior and must be recorded in the physical test report. The controller must not assume that a failed write succeeded. The portable controller latches emergency/failsafe decisions; cooling down by itself does not silently re-enable normal control. Recovery requires an explicit BIOS/automatic request or controller reset followed by the normal startup gates.

## 7. Fan-health detection

Only evaluate RPM response when the selected profile verifies a tachometer source. A health sample should include:

- requested command;
- RPM value and age;
- time since command;
- temperature trend;
- backend/readback status.

Possible outcomes:

- `Healthy`: RPM responds within the documented observation window;
- `Unknown`: RPM is unavailable or the fan is expected to be stopped;
- `Suspect`: command changed but RPM response is missing or implausible;
- `Failed`: repeated suspect samples plus a rising-temperature or safety condition.

A suspect result must be logged and surfaced. It must not cause repeated blind EC writes.

## 8. Power-source policies

AC/battery switching is a policy selection event, not permission to bypass hardware gates. A new policy must:

1. validate the profile;
2. preserve the verified topology;
3. retain the current safety state until the new policy is accepted;
4. use startup validation if sensor/backend continuity is lost;
5. log the reason for the switch.

## 9. Test vectors

The portable controller must cover:

- below-first-point temperature;
- exact curve thresholds;
- between-threshold hysteresis;
- cooling across a down threshold;
- dwell-time rejection and expiry;
- manual override expiry;
- stale temperature;
- invalid temperature;
- backend loss;
- write/readback mismatch;
- high-temperature override;
- unavailable RPM;
- suspect RPM;
- single-fan topology rejecting a second-channel request.
