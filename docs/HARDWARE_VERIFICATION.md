# Phase 0 — hardware verification

**Status:** Not started
**Target:** ThinkPad T14 Gen 1 Intel on Windows 11 x64
**Safety level:** Read-only until the controlled-write checklist is explicitly approved

## 1. Why this phase exists

The existing application and community utilities contain assumptions from several ThinkPad generations. A register address that works on one model is not proof that it is safe or meaningful on another model.

The exact machine type must be identified before selecting a profile. The standard T14 Gen 1 Intel family is expected to use machine types 20S0/20S1, but the installed machine type is authoritative.

## 2. Collect system identity

Open an elevated PowerShell window and save the output to a private local file. Do not place serial numbers in public issue reports.

```powershell
$root = Join-Path $env:USERPROFILE 'Desktop\tpfancontrol-phase0'
New-Item -ItemType Directory -Force $root | Out-Null

Get-CimInstance Win32_ComputerSystemProduct |
    Select-Object Name, Version, IdentifyingNumber |
    Out-File "$root\computer-product.txt"

Get-CimInstance Win32_ComputerSystem |
    Select-Object Manufacturer, Model, SystemFamily |
    Out-File "$root\computer-system.txt"

Get-CimInstance Win32_Processor |
    Select-Object Name, Manufacturer, NumberOfCores, NumberOfLogicalProcessors |
    Out-File "$root\processor.txt"

Get-CimInstance Win32_BIOS |
    Select-Object SMBIOSBIOSVersion, Version, ReleaseDate |
    Out-File "$root\bios.txt"

Get-CimInstance Win32_OperatingSystem |
    Select-Object Caption, Version, BuildNumber, OSArchitecture |
    Out-File "$root\windows.txt"
```

Optional security state:

```powershell
Confirm-SecureBootUEFI

Get-CimInstance -Namespace root\Microsoft\Windows\DeviceGuard `
  -ClassName Win32_DeviceGuard |
  Select-Object VirtualizationBasedSecurityStatus, SecurityServicesRunning, CodeIntegrityPolicyEnforcementStatus
```

If `Confirm-SecureBootUEFI` fails, record the failure rather than assuming Secure Boot is disabled.

## 3. Record the baseline

Measure the following with BIOS/automatic fan control and no TPFanControl running:

| Condition | AC/battery | CPU load | CPU/package temp | EC temp(s) | Fan RPM | Fan audible state |
|---|---|---:|---:|---|---:|---|
| Idle, 10 min | | | | | | |
| Browser/video | | | | | | |
| Sustained load, 5 min | | | | | | |
| Sustained load, 15 min | | | | | | |
| Cooldown | | | | | | |

Record the monitoring tool, version, BIOS version, ambient temperature, and power mode.

Do not compare one sensor application's label with an EC label without recording both raw values and names.

## 4. Driver/backend verification

Before changing source code, determine whether the existing TVicPort backend loads:

1. Run the existing application in monitor-only/passive mode.
2. Record the exact initialization message.
3. Inspect Windows Event Viewer under Code Integrity and System logs.
4. Inspect the installed driver package and architecture.
5. Record whether Secure Boot/HVCI is enabled.

Do not disable Secure Boot, HVCI, or the vulnerable-driver blocklist as a project requirement. If the backend is blocked, document the reason and evaluate an approved backend.

## 5. Read-only EC investigation

Use one controlled diagnostic method. Save:

- raw EC dump, if available;
- port mapping;
- timestamp;
- BIOS revision;
- AC/battery state;
- fan state during the dump;
- temperature readings from an independent monitor.

Candidate addresses should be read repeatedly under idle and load. A plausible value is not proof by itself; it must correlate with an independent measurement.

Candidate values from generic ThinkPad documentation are listed in [EC_REGISTER_MAP.md](EC_REGISTER_MAP.md). They are not automatically approved for writing.

## 6. Controlled write test

Only perform this after the read-only report is complete and backed up.

Preconditions:

- exact machine type documented;
- correct backend verified;
- BIOS/automatic restore value documented;
- external temperature monitor running;
- AC adapter connected;
- no sustained workload;
- emergency power-off/reboot procedure available;
- an operator has explicitly approved the test.

Test order:

1. Read the current fan-control value.
2. Read current fan speed and temperatures.
3. Write only the documented BIOS/automatic value if needed.
4. Read back and verify it.
5. If a manual test is approved, use the lowest normal non-zero level first.
6. Observe temperature and RPM for a short bounded interval.
7. Restore BIOS/automatic mode immediately.
8. Read back and record the result.

Never test a second-fan selector by writing `0x31` on a single-fan target. Never test disengaged/full-speed mode during Phase 0.

## 7. Evidence and classification

Every candidate address receives one classification:

- **Verified read:** correlated with an independent value.
- **Verified write:** controlled write/readback succeeded and behavior was observed.
- **Candidate:** plausible but not yet correlated.
- **Unsupported:** read failed or value was invalid.
- **Unknown:** no reliable evidence.

The T14 profile may use only verified values for automatic control. Candidate values may appear in a diagnostic report but not in a release profile.

## 8. Phase 0 completion checklist

- [ ] Exact machine type recorded.
- [ ] CPU model recorded.
- [ ] BIOS version recorded.
- [ ] Windows build and architecture recorded.
- [ ] Secure Boot/HVCI state recorded.
- [ ] Baseline measurements recorded on AC and battery.
- [ ] Existing backend load result recorded.
- [ ] EC port map classified.
- [ ] Fan-control offset classified.
- [ ] Fan-speed offsets classified.
- [ ] Temperature offsets classified.
- [ ] Single/dual topology classified without unsafe probing.
- [ ] BIOS restoration procedure recorded.
- [ ] Hardware report copied to the project documentation set.
