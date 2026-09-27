# Useful feature backlog

**Status:** Prioritized for incremental implementation; portable safety/backend foundation is now implemented
**Hardware gate:** Features that can issue EC writes remain disabled until Phase 0 and the relevant physical test exit criteria are complete.

This backlog turns the modernization goals into user-visible capabilities. It is intentionally ordered by usefulness and risk, not by visual appeal.

## 1. Prioritization rules

A feature is high priority when it does at least one of the following:

- prevents an unsafe fan command;
- makes an unsupported machine fail safely;
- explains what the controller is doing;
- reduces noise or battery use without hiding thermal risk;
- makes a hardware problem diagnosable;
- can be tested with a fake backend before hardware is involved.

A feature is not allowed to bypass the exact-machine, verified-profile, backend, sensor, or BIOS-restore gates.

## 2. Tier 0 — safety and trust foundation

These capabilities should be implemented before expanding the UI or adding automation.

| Capability | User benefit | Hardware dependency | Exit evidence |
|---|---|---:|---|
| Hardware capability report | Shows the exact machine, backend, topology, sensors, and control eligibility | Read-only identity/backend only | Unit tests plus Phase 0 report |
| Monitor-only mode | Provides useful temperature, RPM, status, and logs without EC writes | Optional sensor/backend access | Fake-backend and unavailable-backend tests |
| Sensor validity and freshness | Prevents fan-off decisions from bad data | Provider-specific | Boundary and stale-sample tests |
| Explicit safety state machine | Makes startup, faults, fallback, and shutdown deterministic | No hardware for core logic | State-transition tests |
| Profile validation | Rejects impossible or unsafe configuration before activation | No hardware for parser/validator | Invalid-profile test matrix |
| Diagnostic export | Makes support reports reproducible and privacy-aware | No hardware for redaction/export | Redaction and bundle tests |
| Bounded logging | Keeps evidence without filling the disk | No hardware | Rotation and size tests |
| BIOS/automatic fallback contract | Defines recovery instead of silently continuing | Requires verified restore on hardware | Physical restore report |

## 3. Tier 1 — daily-use features

| Capability | Intended behavior | Safety rules |
|---|---|---|
| Named profiles | BIOS, Quiet, Balanced, Performance, Cool workstation, Battery saver, and Custom | Only verified profiles may control hardware |
| AC/battery policies | Select a policy based on power source | Profile change must pass the same startup and sensor gates |
| Time-limited manual override | Manual command expires and returns to BIOS/automatic mode | No indefinite override; invalid sensor or backend cancels it |
| Hysteresis and dwell | Prevents rapid fan oscillation | Limits are profile data and are validated |
| Ramp limiting | Makes changes less abrupt | Emergency response may bypass normal ramp-down, never safety gates |
| Tray status | Shows temperature, requested level, RPM, power, profile, backend, and safety state | Must distinguish requested from measured values |
| Temperature/RPM history | Shows recent behavior and command response | Data is bounded and local by default |
| Notifications | Reports faults, fallback, invalid profile, or thermal warning | Rate-limited and severity-aware |
| Fan health detection | Detects missing or implausible RPM when a tachometer is verified | No false failure when RPM is unavailable |
| Configuration backup/rollback | Restores the last known-good profile | Rollback never writes undocumented EC values |
| Read-only CLI | Supports status, monitoring, validation, and diagnostic export | No raw EC access through the CLI |

Detailed contracts are in [CONTROL_ALGORITHM.md](CONTROL_ALGORITHM.md), [OBSERVABILITY.md](OBSERVABILITY.md), and [CLI_DIAGNOSTICS.md](CLI_DIAGNOSTICS.md).

## 4. Tier 2 — useful automation

Implement only after Tier 0 and Tier 1 have test and review evidence:

- foreground-application or workload profiles using an explicit allowlist;
- full-screen/game/compile/render workload hints;
- Windows power-mode integration;
- acoustic optimization through slower changes and minimum dwell;
- thermal trend warnings;
- read-only system health dashboard;
- import/export of versioned profiles;
- localized and accessible status text;
- Explorer/tray recovery after restart or sleep/resume;
- optional per-user startup task.

Workload detection must not become hidden surveillance. The allowlist, collected fields, and retention policy must be visible to the user.

## 5. Tier 3 — advanced and optional

These are valuable only after the controller is stable:

- sensor fusion using multiple independently validated providers;
- a graphical curve editor with live validation but no direct register editing;
- a separate privileged backend and unprivileged tray process;
- a read-only local named-pipe API;
- additional verified ThinkPad profiles;
- additional approved I/O backends;
- signed packages and an update channel with rollback.

A local API must be disabled by default, user-scoped, and unable to issue arbitrary EC commands.

## 6. Deferred or rejected features

Do not implement these as convenience features:

- automatic write-based EC discovery;
- arbitrary EC scripts in profile files;
- remote fan-control or an internet-facing dashboard;
- an unsigned replacement driver as automatic fallback;
- automatic AI-generated curves applied without review;
- undervolting, overclocking, or firmware modification;
- disabling Secure Boot, HVCI, or the vulnerable-driver blocklist;
- claiming crash-safe restoration without a separately verified watchdog;
- a universal dual-fan path for a single-fan T14.

## 7. Recommended implementation sequence

1. Portable curve, sensor, and safety core with fake-backend tests.
2. Profile validator and monitor-only status model.
3. Bounded event/telemetry and diagnostic export.
4. Read-only CLI and capability report.
5. Windows adapter integration behind `IIoBackend`.
6. Hardware-gated T14 profile and physical manual tests.
7. Tray status, notifications, and timed manual override.
8. AC/battery policies and fan-health detection after tachometer verification.
9. Workload automation and advanced UI only after release qualification.

## 8. Acceptance rule

A feature is complete only when:

- its safety and hardware dependency are documented;
- its configuration is validated;
- its failure behavior is specified;
- it has unit/fake-backend tests where possible;
- it has a physical test report when it touches real hardware;
- the user can tell whether it is active, unavailable, or blocked.
