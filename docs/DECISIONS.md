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

## ADR-015 — Optional enhancements are not MVP requirements

**Status:** Accepted

Named profiles, a graphical curve editor, foreground-application policies,
acoustic tuning, trend warnings, a local API surface, a split privileged and
unprivileged process model, extra machine profiles, extra backends, power-mode
integration, history graphs, and configuration rollback are all Phase 7
enhancements. They are not MVP requirements, and none of them establishes safe
EC access.

**Consequence:** They are Phase 7 enhancements, not MVP requirements. Starting
one before the T14 single-fan path is verified on hardware adds complexity
without reducing the project's actual risk. See ADR-012.

## ADR-016 — Generated build artifacts are not distributed from this repository

**Status:** Accepted

The repository does not publish a prebuilt `TPFanControl.exe`. A binary built
here is specifically the artifact that writes to an Embedded Controller, and
distributing one invites someone to run an unqualified build on real hardware.
Reviewers should not be asked to reverse-engineer a shipped binary to check the
safety changes.

**Consequence.** When a binary is needed for a hardware test session, it is
built from a recorded commit and attached to a
[test report](templates/TEST_REPORT.md) with its commit id and build
configuration, never committed or released. See T0-01 … T0-05 and
[BUILD.md](BUILD.md) section 10.

## ADR-017 — Do not rewrite Git history

**Status:** Accepted (T0-09)

**Context.** The repository as inherited contained roughly 66 tracked build
artifacts (`.ipch`, `.VC.db`, `.pdb`, `.suo`, `.obj`), a tracked
`fancontrol.exe` built with `Active=2`, which is smart mode and therefore
**writes to the EC**, and committed runtime logs containing a real Windows user
name. The project's own `SECURITY.md`, `BUILD.md` and `TESTING.md` each forbid
exactly this.

**Decision.** Do not rewrite history. Instead:

1. `.gitignore` and `git rm --cached` remove all of it going forward (T0-01 … T0-05).
2. The README no longer advertises the prebuilt binary, and states that it must not be
   used (ADR-016).
3. The committed logs are removed from the index. The files remain on the contributor's
   working copy, because they are local state and destroying it serves no purpose.

**Reasons.** The leaked data is a Windows user name in 2018-era logs, not a credential, a
serial number, or a token; it does not grant access to anything. The binaries are inert
without a kernel driver that the project deliberately does not ship. The ongoing cost of
a rewrite — broken clones, lost attribution, diverged forks — exceeds the ongoing cost of
the exposure for every plausible reader of a public fan-control fork.

**Consequence.** Anyone who cloned before 2026-09-27 still has the artifacts in their local
history, and a Windows user name from 2018 remains retrievable via `git log`. This is
accepted, not overlooked.

**Revisit conditions.** Rewrite history if any of the following becomes true:

- the repository is made private, or a credential, token, or serial number is found in
  history;
- the project is forked into a distribution where third-party binaries in `master` create a
  real supply-chain path;
- a security review under `SECURITY.md` §8 judges the exposure material.

If any condition holds, use `git filter-repo --path fancontrol/.vs --path fancontrol/Debug
--path fancontrol/Release --path fancontrol/ipch --invert-paths` and coordinate with
anyone holding a clone.
