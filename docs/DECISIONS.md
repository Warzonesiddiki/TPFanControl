# Architecture decision records

This file records decisions that constrain implementation. New decisions should use [templates/ADR.md](templates/ADR.md).

## ADR-001 — Verify the exact machine before selecting a profile

**Status:** Accepted

The name “T14 Gen 1” is not enough to establish CPU, machine type, graphics, BIOS, or EC behavior. The application must identify the installed machine and select a profile explicitly.

**Consequence:** Phase 0 is a hard gate. Unknown machines remain monitor-only.

## ADR-002 — Use an incremental migration, not a total rewrite

**Status:** Accepted

The current UI and working portions of the application provide useful compatibility context. Rewriting the entire program while changing drivers and EC behavior would make failures difficult to isolate.

**Consequence:** Add interfaces and adapters first. Defer UI replacement.

## ADR-003 — T14 uses an explicit single-fan profile

**Status:** Pending hardware confirmation

The repository contains a dual-fan write path. The T14 profile will not write a second-fan selector. Any dual-fan behavior remains isolated in a separate profile.

**Consequence:** A topology setting is mandatory for control. Automatic second-fan writes are forbidden.

## ADR-004 — No write-based automatic EC probing

**Status:** Accepted

Writing candidate EC values during startup could change fan behavior or affect unrelated firmware state.

**Consequence:** Phase 0 uses read-only evidence. Controlled writes require an operator-approved test.

## ADR-005 — x64 is primary, but x64 is not treated as the only reason Windows can run the application

**Status:** Accepted

A 32-bit user-mode application can run on 64-bit Windows, but x64 is preferred for new development and pointer correctness. Win32 compatibility is retained only where the backend supports it.

**Consequence:** Build matrix and backend capability matrix are separate concerns.

## ADR-006 — All privileged access goes through `IIoBackend`

**Status:** Accepted

The controller must not be coupled to TVicPort, PawnIO, or a future backend.

**Consequence:** Backends can be tested independently, and monitor-only mode is possible.

## ADR-007 — Monitor-only is the safe backend fallback

**Status:** Accepted

Trying an arbitrary second kernel driver is not an acceptable safety strategy.

**Consequence:** If no trusted backend is available, leave fan control to BIOS and explain the dependency.

## ADR-008 — CPU MSR temperature is optional

**Status:** Accepted

MSR support depends on CPU, backend, permissions, thermal-status validity, and correct TjMax handling. Fan control must not depend on an unverified MSR implementation.

**Consequence:** EC and other validated providers remain part of the design. Missing CPU MSR support is reported, not hidden.

## ADR-009 — A visible tray app should not run as SYSTEM

**Status:** Accepted

A SYSTEM task normally runs outside the interactive user session, making tray/UI behavior unreliable.

**Consequence:** Use an elevated per-user logon task for the tray MVP. A service requires a separate service/tray architecture.

## ADR-010 — Safety is a controller state machine

**Status:** Accepted

Scattered `if` statements cannot prove that every failure path restores BIOS mode.

**Consequence:** Startup validation, controlled operation, and failsafe transitions are explicit and testable.

## ADR-011 — Generated build artifacts are not source inputs

**Status:** Accepted

The repository contains old Debug/Release outputs and Visual Studio databases. They can conceal source build failures and create machine-specific noise.

**Consequence:** Build into an ignored output directory and progressively remove generated artifacts from version control.

## ADR-012 — Feature work follows hardware qualification

**Status:** Accepted

Curve editors, app profiles, and services increase complexity but do not establish safe EC access.

## ADR-013 — Start with a portable hardware-independent core

**Status:** Accepted

Curve evaluation, sensor validation, capability gating, timed manual override, and safety transitions are implemented in `fancontrol/core/` without Windows, driver, EC, or administrator dependencies. The legacy Win32 application remains an integration shell until the Windows backend adapter is verified.

**Consequence:** The highest-risk decision logic can be tested on a developer machine. A passing portable-core test does not prove any hardware register or driver is safe.

## ADR-014 — Control activation is explicit and safety faults latch

**Status:** Accepted

Profile eligibility alone does not start fan control. An explicit activation request is required. BIOS/automatic requests clear the control request. Emergency and failsafe decisions remain latched until an explicit recovery/lifecycle reset and a fresh validation sequence.

**Consequence:** A new build defaults to BIOS/automatic or monitor-only behavior, and a temporary temperature drop cannot silently restart control after a fault.

**Consequence:** They are Phase 7 enhancements, not MVP requirements.
