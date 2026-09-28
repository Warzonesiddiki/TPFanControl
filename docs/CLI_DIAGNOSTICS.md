# CLI and diagnostic capabilities

The command-line surface is intended for support, automation, and testing. It must be read-only unless a future command is explicitly documented as hardware-gated.

## 1. Commands

| Command | Purpose | EC write allowed |
|---|---|---:|
| `TPFanControl.exe --status` | Print the current capability and safety snapshot | No |
| `TPFanControl.exe --monitor` | Run or request monitor-only diagnostics | No |
| `TPFanControl.exe --validate-profile <path>` | Validate a profile without activating it | No |
| `TPFanControl.exe --export-diagnostics <path>` | Create a sanitized diagnostic bundle | No |
| `TPFanControl.exe --list-profiles` | List profiles and verification status | No |
| `TPFanControl.exe --version` | Print application and core versions | No |
| `TPFanControl.exe --restore-bios` | Request verified BIOS/automatic restoration | Only after hardware gate |

The current portable core implements validation primitives only. These commands are an integration target and must not be advertised as available until wired into the Windows application.

The read-only half of this contract also exists today as a separate portable tool,
`ecdiag`, which is not part of `TPFanControl.exe` and has no control path at all.
Its modes, report format and exit codes are documented in [ECDIAG.md](ECDIAG.md).

## 2. Output contract

Human-readable output is allowed, but every command should also support a stable machine-readable form in a later version:

```text
--format text
--format json
```

JSON must use explicit `null` or a documented status for unknown fields. It must not use zero as a missing temperature or RPM.

Example status fields:

```json
{
  "profile": {"id": "none", "verified": false},
  "backend": {"state": "unavailable", "control": false},
  "safety_state": "monitor_only",
  "temperature": {"value_c": null, "source": null},
  "fan": {"requested": null, "rpm": null, "health": "unknown"}
}
```

## 3. Exit codes

The eventual CLI should use stable exit codes:

| Code | Meaning |
|---:|---|
| 0 | Completed successfully |
| 1 | General/runtime error |
| 2 | Invalid command-line arguments |
| 3 | Profile/configuration invalid |
| 4 | Backend unavailable or unsupported |
| 5 | Hardware identity/profile not verified |
| 6 | Sensor data unavailable or invalid |
| 7 | Safety/failsafe state active |
| 8 | Diagnostic export failed |

A command that cannot perform control must fail closed and explain how monitor-only diagnostics can still be used.

## 4. Diagnostic export rules

- The output path must be validated and must not be passed to a shell.
- Existing files require an explicit overwrite option.
- Archive contents must be generated from an allowlist.
- Serial numbers, usernames, tokens, and private paths must be redacted.
- Export failures must not affect fan-control safety state.

## 5. Hardware-write restrictions

There must be no generic `--write-register`, `--probe`, or user-supplied EC command in the public CLI.

A future `--restore-bios` action may be exposed only when:

- the exact profile is verified;
- the backend capability is approved;
- the restore value is documented;
- the action uses the typed profile protocol;
- readback/error handling is implemented;
- the hardware report includes physical restore evidence.

## 6. Test usage

The test harness should invoke the portable core directly and use a fake backend. It must never require a real driver, administrator privileges, or a laptop EC.
