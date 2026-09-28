# TPFanControl modernization documentation

**Project:** TPFanControl
**Target:** ThinkPad T14 Gen 1 Intel; Windows version and build not yet supplied
**Documentation status:** Feature plan expanded; portable core foundation started; hardware verification is still required
**Last reviewed:** 2026-09-28

## Purpose

This directory is the source of truth for modernizing the legacy TPFanControl fork safely. The project controls a laptop Embedded Controller (EC), so the documentation deliberately separates:

1. facts verified on the target laptop;
2. behavior inferred from generic ThinkPad documentation;
3. implementation decisions;
4. code that is safe to run without hardware access; and
5. hardware tests that require an explicit operator.

No EC write should be implemented merely because a register address appears in an old ThinkPad utility. Every writable register and every driver backend must be verified on the target machine.

## Current target boundary

The initial supported target is the **standard T14 Gen 1 Intel family**, expected to be machine types 20S0/20S1. Lenovo's product information identifies these systems as 10th-generation Intel platforms; 20W0/20W1 are T14 Gen 2 Intel machine types, and should not be silently treated as the same hardware.

The exact machine type, CPU, BIOS revision, graphics configuration, EC port mapping, fan topology, and driver compatibility remain open until Phase 0 is completed. See [Hardware Verification](HARDWARE_VERIFICATION.md).

## Non-negotiable safety rules

- Never probe a candidate EC register by writing to it automatically.
- Never take fan control until the application has several consecutive valid sensor reads.
- Never use an unverified temperature as the basis for a fan-off decision.
- On read, write, verification, or backend failure, attempt to return control to BIOS/automatic mode and stop issuing manual commands.
- Keep a monitor-only mode that works without a low-level driver.
- Do not claim crash-safe fan restoration unless a real EC or supervisor watchdog has been tested.
- Do not ship an unsigned or unreviewed kernel driver as an automatic fallback.

## Documentation map

| Document | Use it for |
|---|---|
| [ROADMAP.md](ROADMAP.md) | Ordered milestones, gates, deliverables, and stop conditions |
| [ARCHITECTURE.md](ARCHITECTURE.md) | Target design and migration strategy |
| [FEATURE_BACKLOG.md](FEATURE_BACKLOG.md) | Prioritized useful capabilities and deferred features |
| [CONTROL_ALGORITHM.md](CONTROL_ALGORITHM.md) | Curves, hysteresis, dwell, ramping, and manual control |
| [OBSERVABILITY.md](OBSERVABILITY.md) | Status, telemetry, logs, graphs, fan health, and diagnostics |
| [CLI_DIAGNOSTICS.md](CLI_DIAGNOSTICS.md) | Read-only command-line and diagnostic contracts |
| [ECDIAG.md](ECDIAG.md) | The read-only EC diagnostic tool: modes, report format, and its read-only guarantee |
| [USER_GUIDE.md](USER_GUIDE.md) | Intended user experience and recovery |
| [COMPATIBILITY.md](COMPATIBILITY.md) | Capability states and support matrix |
| [HARDWARE_VERIFICATION.md](HARDWARE_VERIFICATION.md) | Laptop identification and Phase 0 evidence |
| [EC_REGISTER_MAP.md](EC_REGISTER_MAP.md) | Candidate EC addresses and verification status |
| [DRIVER_BACKENDS.md](DRIVER_BACKENDS.md) | TVicPort, PawnIO, capability checks, and dependency policy |
| [DEPENDENCIES.md](DEPENDENCIES.md) | The dependency record: sources, licences, hashes, and what is not established |
| [SAFETY.md](SAFETY.md) | Safety state machine, failsafes, and limits |
| [PROFILES.md](PROFILES.md) | INI/profile schema and T14 curve policy |
| [BUILD.md](BUILD.md) | Reproducible Windows builds and output layout |
| [TESTING.md](TESTING.md) | Unit, fault-injection, CI, and physical-laptop tests |
| [RELEASE.md](RELEASE.md) | Packaging, release evidence, rollback, and support |
| [VIBE_CODING_GUIDE.md](VIBE_CODING_GUIDE.md) | Rules for incremental agent-assisted implementation |
| [DECISIONS.md](DECISIONS.md) | Architecture decision records and rejected alternatives |
| [TROUBLESHOOTING.md](TROUBLESHOOTING.md) | Recovery and diagnostic procedures |
| [IMPLEMENTATION_CHECKLIST.md](IMPLEMENTATION_CHECKLIST.md) | Execution checklist and phase status |
| [templates/HARDWARE_REPORT.md](templates/HARDWARE_REPORT.md) | Copy for a real T14 hardware report |
| [templates/TEST_REPORT.md](templates/TEST_REPORT.md) | Copy for each physical test session |
| [templates/ADR.md](templates/ADR.md) | Copy for future architectural decisions |

## Required workflow for every coding task

Before changing code:

1. Read this file and the current phase in [ROADMAP.md](ROADMAP.md).
2. Read the relevant design document and decision records.
3. State whether the task is hardware-independent or hardware-dependent.
4. Define the smallest testable increment.
5. Do not implement a later-phase feature to compensate for an unverified earlier phase.

After changing code:

1. Update the relevant documentation and checklist.
2. Add or update a test, fake-backend case, or hardware evidence record.
3. Run the checks in [BUILD.md](BUILD.md) and [TESTING.md](TESTING.md).
4. Review for accidental raw EC writes, unsafe defaults, logging of sensitive data, and unchecked errors.
5. Record unresolved risks rather than hiding them.

## Reference links

These links are starting points, not substitutes for evidence from the actual laptop:

- [Lenovo ThinkPad T14 Gen 1 Intel PSREF](https://psref.lenovo.com/Product/ThinkPad/ThinkPad_T14_Gen_1_Intel)
- [Lenovo ThinkPad T14 Gen 2 Intel PSREF](https://psref.lenovo.com/Product/ThinkPad/ThinkPad_T14_Gen_2_Intel)
- [ThinkWiki EC/fan-control reference](https://www.thinkwiki.org/wiki/How_to_control_fan_speed)
- [PawnIO official site](https://pawnio.eu/)
- [Microsoft `SetWindowLongPtr` documentation](https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-setwindowlongptrw)
- [Microsoft Task Scheduler documentation](https://learn.microsoft.com/windows/win32/taskschd/task-scheduler-2-0-examples)

## Source-of-truth order

When documents disagree, use this order:

1. Evidence recorded from the actual target laptop;
2. Lenovo documentation for the exact machine type;
3. official backend/driver documentation;
4. tests and source code;
5. generic ThinkPad documentation;
6. community reports and assumptions.

Community reports are useful leads, not proof of hardware compatibility.

## Governance

- [`../LICENSE`](../LICENSE) — public domain dedication, with named third-party exceptions
- [`../CONTRIBUTING.md`](../CONTRIBUTING.md) — the contribution contract and safety rules
- [`IMPLEMENTATION_CHECKLIST.md`](IMPLEMENTATION_CHECKLIST.md) — the taskboard and the only
  document that tracks what is actually done
- [`DECISIONS.md`](DECISIONS.md) — architecture decision records
- [`SAFETY.md`](SAFETY.md) — safety requirements and the open-gap table
