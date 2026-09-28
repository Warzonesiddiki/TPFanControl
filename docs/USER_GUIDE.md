# User guide and recovery behavior

This guide describes the intended user experience. Hardware control remains unavailable until the exact machine profile and backend are verified.

## 1. First launch

On first launch, the application should:

1. identify the machine without exposing serial numbers;
2. load and validate configuration;
3. report backend capabilities;
4. collect the required valid sensor samples;
5. start in monitor-only or BIOS/automatic mode unless every control gate passes.

The first launch must not silently activate a guessed T14 or legacy dual-fan profile.

## 2. Status modes

The tray/status view should show one of these understandable states:

- **Monitor only:** temperatures/status may be visible, but no manual fan commands are allowed;
- **Validating:** collecting fresh, valid samples before control;
- **BIOS automatic:** firmware owns fan control;
- **Automatic profile:** a verified profile is controlling the fan;
- **Manual override:** a temporary user command is active and has an expiry;
- **Failsafe:** normal control stopped because a safety condition occurred;
- **Thermal emergency:** the user should reduce load or shut down according to the warning.

The status must distinguish requested fan level from measured RPM.

## 3. Profiles

Profiles should be named and versioned. The initial safe choices are:

- BIOS/Automatic;
- Quiet;
- Balanced;
- Performance;
- Battery saver;
- Custom, only after validation.

A profile can be visible but unavailable. The UI must explain whether it is blocked because of machine identity, backend, sensor, topology, or missing physical evidence.

## 4. Manual override

Manual control should be:

- explicitly confirmed;
- limited to the verified command range;
- time-limited;
- cancelled on invalid sensors, backend errors, or expiry;
- ended by a visible Return to BIOS/automatic action.

A manual override must never disable an emergency safety transition.

## 5. AC and battery

Power-source policies may select different curves, but a power-source change must not bypass validation. The current policy and reason for the change should appear in status and logs.

## 6. Notifications

Notify the user for:

- unsupported or unverified hardware;
- backend unavailable;
- stale/invalid sensors;
- fan response failure when RPM is verified;
- temperature warning/emergency;
- automatic fallback to BIOS/monitor-only;
- profile validation failure.

Notifications must be rate-limited.

## 7. Recovery

If behavior is unexpected:

1. choose Return to BIOS/automatic if available;
2. stop the application;
3. reduce system load and observe temperature;
4. reboot if the control state is uncertain;
5. use Lenovo hardware diagnostics or service support if the fan remains abnormal;
6. export sanitized diagnostics after the system is stable.

Do not disable Secure Boot or HVCI as a routine recovery step.

## 8. Startup

Startup should be optional, delayed until the desktop is ready, and run in the user session for the tray UI. A SYSTEM scheduled task must not be used merely to display the tray icon.

Startup must default to monitor-only/BIOS behavior when a profile is unverified.

## 9. Privacy

Diagnostics are local by default. The application should not upload telemetry or inspect arbitrary process activity unless the user enables a documented feature. Workload profiles must use an explicit allowlist.
