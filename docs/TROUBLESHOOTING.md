# Troubleshooting and recovery

## 1. Application says the backend is unavailable

Check:

1. The required backend is installed.
2. The backend architecture matches the executable.
3. The driver/device is present in Device Manager.
4. Code Integrity logs for blocks.
5. Secure Boot/HVCI status.
6. The application is elevated if required.
7. No other EC utility is holding the device or access mutex.

Do not disable Secure Boot or HVCI as the first workaround. Record the exact error and use monitor-only mode.

## 2. Application reports EC read errors

Possible causes:

- wrong EC port map;
- wrong profile;
- backend timeout;
- another EC utility is active;
- EC mutex contention;
- firmware/BIOS behavior changed;
- driver was blocked after a Windows update.

Actions:

1. Stop all other fan-control/EC utilities.
2. Return to BIOS/automatic control.
3. Check the hardware report and selected profile.
4. Enable bounded diagnostic logging.
5. Repeat read-only diagnostics.
6. Do not increase retries indefinitely.

## 3. Fan level changes but RPM does not appear

RPM readout and fan control are separate capabilities. Check:

- whether the tachometer offset was verified;
- whether the fan is physically stopped;
- whether the value is stale or an invalid sentinel;
- whether the selected fan topology is correct;
- whether the firmware delays tachometer updates.

Do not conclude that fan control failed solely because RPM is unavailable. Use an independent observation and label the UI value as unavailable when unverified.

## 4. Fan becomes unexpectedly loud

Immediately:

1. Select BIOS/automatic mode.
2. Stop TPFanControl.
3. Check CPU temperature and load.
4. Check for a stuck manual command in the log.
5. Check whether another utility is writing the EC.
6. Check thermal paste, dust, and fan hardware if temperatures are abnormal.

Do not set arbitrary EC bytes in an attempt to quiet the fan.

## 5. Temperature is zero, 128, negative, or implausibly high

Treat it as invalid. Check:

- raw value before offsets;
- source name;
- timestamp/freshness;
- signed/unsigned conversion;
- sensor sentinel handling;
- profile temperature offsets;
- whether the extended sensor block is supported.

The controller must remain in monitor-only or BIOS mode until a valid source exists.

## 6. T14 profile is rejected as unverified

This is expected until the hardware report is complete. Complete:

- exact machine identity;
- read-only EC evidence;
- controlled manual-level test;
- BIOS restore test.

Do not simply change `VerificationStatus` to bypass the guard.

## 7. Startup does not show the tray icon

Check:

- the task runs in the interactive user session;
- the task is not configured as `SYSTEM`;
- the working directory contains the executable and profile;
- the application is not blocked by elevation policy;
- Windows Explorer was restarted;
- the tray icon is hidden under the overflow menu.

A service should not be expected to display a user tray icon.

## 8. Application will not close

The application should attempt BIOS restore with a bounded timeout. If restore fails:

- do not kill/retry forever;
- record the failure;
- inform the user;
- allow a controlled reboot/power-off recovery procedure;
- document the final EC state if it can be read.

## 9. Recovery procedure

The normal recovery order is:

1. Use the UI's **BIOS/Automatic** action.
2. Confirm the status/readback.
3. Stop the application.
4. Disable startup if necessary.
5. Reboot to let firmware reinitialize the EC.
6. If temperature or fan behavior remains abnormal, power off and use Lenovo hardware diagnostics/service procedures.

The project must not publish undocumented raw EC commands as a user recovery recipe.

## 10. Reporting a bug

Include:

- exact machine type, with serial number removed;
- CPU model;
- BIOS version;
- Windows build;
- Secure Boot/HVCI state;
- selected profile and revision;
- backend/version;
- log excerpt;
- whether another hardware utility was running;
- reproduction steps;
- whether BIOS restore succeeded.

Never attach private serial numbers, license keys, or full personal directories.
