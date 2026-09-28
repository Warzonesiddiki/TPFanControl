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

---

## ADR-018 — Source encoding: UTF-8 BOM, not `/utf-8`

**Status.** Accepted (T1-06)

**Context.** `fancontrol.vcxproj` is a `MultiByte` (ANSI) build. Several legacy
sources contain user-visible degree signs in narrow string literals — for example
`sprintf_s(title2, sizeof(title2), "%d°C", this->MaxTemp)` in
`fancontrol/fanstuff.cpp`, which feeds the minimised-window title, and
`"Fan: 0x%02x / Switch: %d°C (%s)"` in the same file, which is shown on hover.

MSVC has two independent charset knobs:

- the **source charset**, which decides how bytes in the file are decoded, and
- the **execution charset**, which decides how a decoded character is re-encoded
  into a narrow (`char`) string literal.

`/utf-8` sets *both* to UTF-8. `/source-charset:utf-8` sets only the first and
leaves the execution charset at the system ANSI code page (cp1252 on Western
Windows). A UTF-8 **BOM** overrides the source charset regardless of flags.

**Decision.**

1. Use `/source-charset:utf-8` on the legacy translation units, not `/utf-8`.
2. Put a UTF-8 BOM on every source file in the repository. A BOM makes source
   decoding deterministic and independent of the machine's code page.
3. Use `/utf-8` only on `fancontrol/core/*`, which is pure ASCII, so the
   execution charset cannot change the meaning of any literal there.
4. Keep the runtime library explicit and uniform: `/MT` for the application,
   `/MD` for the standalone test executables.

**Reasons.** With `/utf-8`, a narrow literal keeps its UTF-8 bytes `C2 B0`; the
ANSI window then decodes those through cp1252 and renders `Â°`. With
`/source-charset:utf-8`, the BOM-decoded `U+00B0` is re-encoded to the single
cp1252 byte `B0`, which the window renders as `°`. The second is correct, and it
is also what the project did before this change by accident.

This is not hypothetical. During T1 it was found that `fanstuff.cpp` was the only
source containing non-ASCII literals *without* a BOM, so it was decoded through
the ANSI code page and the same `Â°` mojibake was produced on every build. The
BOM policy in (2) is what fixes it. The two sibling projects `TPFCIcon/` and
`TPFCIcon_noballons/` were genuinely cp1252-encoded and have been converted to
UTF-8; their non-ASCII content is limited to comments and one title literal, and
the ASCII skeleton of every file was verified byte-identical across the
conversion.

**Consequence.** Rendering of non-ASCII text remains dependent on the machine's
ANSI code page, which is inherent to an ANSI application. On a system whose ACP
is not cp1252 the degree sign will not render correctly. Fixing that properly
means converting the application to `CharacterSet=Unicode`, which is a
substantive behavioural change to every string in the codebase and is out of
scope for T1.

**Revisit conditions.** Revisit if the project moves to `CharacterSet=Unicode`,
at which point `/utf-8` becomes correct for the legacy sources too and this ADR
should be superseded.

## ADR-019 — Refuse an unencodable EC register address instead of truncating it

**Status.** Accepted, T5. **Superseded by ADR-020**, which removes the truncation risk
at its source by widening the encoding to a full-byte address, so there is no longer
anything to truncate. The reasoning below still stands as the reason that risk was
caught and closed rather than shipped.

**Context.** `core/ec_protocol.h` needs a command encoding before any hardware has
been measured, so that the EC layer is testable at all. The placeholder used is
`command byte = base | address`, with a 4-bit address field.

That immediately raises the question of what to do with an address that does not
fit. The first implementation masked it:

```cpp
writeCommand(static_cast<std::uint8_t>(config_.writeCommandBase | (address & 0x0F)));
```

The mask is what such code usually looks like, and a test written to check "a write
to 0x2A succeeds" passes cleanly. What it actually does is issue a write to
register `0x0A`.

**Decision.** An address that does not fit the encoding is **rejected** with
`IoErrorCode::Unsupported`, before any port is touched, and the message names the
constraint. No masking anywhere in the EC path.

**Alternatives considered.**

1. *Keep the mask, and widen the encoding later when the real one is known.* Rejected:
   the masking behaviour would still be live during Phase 0, which is exactly the
   window in which a wrong register is most likely to be written by mistake.
2. *Throw or assert.* Rejected: this runs in a monitoring process that must not
   terminate. A typed, reportable error is the correct failure mode.
3. *Widen the placeholder to 8 bits so it "fits".* Rejected: it would make the
   placeholder look adequate when it is a guess, and would still be wrong in whatever
   way the real encoding turns out to be wrong. The honest position is that the
   placeholder is too narrow, and that should be visible.

**Consequences.**

- The layer cannot address the real register map, which includes `0x31`. This is
  recorded as a T5 limitation and as a Phase 0 obligation in
  [EC_REGISTER_MAP.md](EC_REGISTER_MAP.md) §9.1, because it is a concrete
  demonstration that the placeholder is inadequate, not merely unverified.
- The `0x31` write refusal now has a second, independent reason to exist. The
  profile rule is still the primary one and is still checked first, but even a
  dual-fan profile that opts in cannot silently write the wrong register.
- `testAddressOutsideTheEncodingIsRefusedNotTruncated` pins the behaviour, and
  `testFanSelectorOptInRemovesTheProfileRuleButNotTheEncodingLimit` pins that the
  two gates stay distinguishable — a refusal that is diagnosed by its message
  cannot be checked by a test that only sees "it failed".

**Revisit conditions.** Superseded by ADR-020 when Phase 0 measures the real
command encoding, at which point `addressFits` should be replaced by the
encoding's real address width rather than retained as a fixed 4-bit constant.

## ADR-020 — Adopt the legacy application's two-phase EC command protocol

**Status.** Accepted, T3. Supersedes ADR-019.

**Context.** T5 introduced `EcBusConfig` with a placeholder command encoding of
`base | address`, carrying a 4-bit address in the low nibble of the command byte.
That encoding was never verified, and ADR-019 recorded the consequence: only
`0x00`–`0x0F` were addressable.

Preparing T3-03 — routing the application's EC writes through `EcBus` — required
reading `fancontrol/portio.cpp`, the legacy application's own EC layer. It records
a **different** protocol:

```c
#define EC_DATAPORT        0x1600
#define EC_CTRLPORT        0x1604
#define EC_STAT_OBF        0x01
#define EC_STAT_IBF        0x02
#define EC_CTRLPORT_READ   (char)0x80
#define EC_CTRLPORT_WRITE  (char)0x81

WritePort(EC_CTRLPORT, EC_CTRLPORT_READ);   // command byte
WritePort(EC_DATAPORT,  offset);            // address byte
*pdata = ReadPort(EC_DATAPORT);             // result
```

The command byte is an **operation code**, not a base. The register address is a
**full byte** written separately to the data port.

This is decisive. The register map the application uses is `0x2F` (fan level),
`0x31` (fan selector), `0x78` and `0xC0` (temperatures), `0x84` (fan speed). Under
the T5 placeholder **none** of them were addressable. The placeholder was not merely
unverified; it contradicted every call site in the codebase it was written to
integrate with.

**Decision.** `EcBusConfig` now models the two-phase protocol recorded in
`portio.cpp`, with `readCommand = 0x80`, `writeCommand = 0x81`, and a full-byte
address. The four port and bit constants are likewise taken from that source, and
`testDefaultCommandBytesMatchTheLegacySource` pins them so they cannot drift
silently.

`addressFits` and `kAddressBits` are **removed**. With a full-byte address there is
nothing to truncate, so the hazard ADR-019 guarded against no longer exists and
keeping a vestigial check would be misleading.

**On the status of these constants.** They are transcribed from the repository, not
measured. Being in the codebase makes them a record of *what the shipped application
does*; it does not make them a record of *what the hardware requires*. Generic EC
documentation describes a `0x62`/`0x66` interface instead, and
[EC_REGISTER_MAP.md](EC_REGISTER_MAP.md) §3 records both mappings as unverified.
Phase 0 must confirm them read-only before any write is issued. This is recorded as
a Phase 0 obligation in `EC_REGISTER_MAP.md` §9.1 and must not be read as having
been softened by the fact that the numbers now come from our own source.

**Alternatives considered.**

1. *Keep the placeholder and translate in the backend.* Rejected: the translation
   would have to invent an address for every real register, none of which the
   placeholder could express. It would also mean the tested layer was not the
   layer that touched the hardware.
2. *Leave `addressFits` in place "just in case".* Rejected: a permanent check for
   a condition that cannot occur is a check that will never be exercised, and an
   unexercised safety check is indistinguishable from a broken one.
3. *Treat the legacy constants as verified hardware evidence.* Rejected outright.
   In-repo code is not a measurement, and treating it as one would let the project
   skip Phase 0 on the strength of a file that nobody has run on the target machine.

**Consequences.**

- The EC suite grows from 39 to 42 tests, and now pins the wire sequence itself
  (`testTheWireSequenceIsCommandThenAddressThenValue`,
  `testReadSequenceIsCommandThenAddressThenResult`) and the full address range
  (`testTheFullByteAddressRangeRoundTrips`), which the old encoding could not have
  passed.
- The fan-selector opt-in is now a real capability rather than a formality, because
  `0x31` is addressable. The single-fan refusal remains the thing that keeps it
  unused, and `setFanSelectorWritesAllowed` is still never called in product code.
- A stale result left in the output buffer by an interrupted transaction is now
  explicitly drained before each transaction
  (`testStaleResultIsDrainedBeforeATransaction`), matching what the legacy code did
  informally.
- ADR-019's refusal is superseded rather than deleted, so the record shows that the
  hazard was found and closed rather than never noticed.

**Revisit conditions.** Superseded by Phase 0 measurement. The defaults should be
replaced by whatever `ecdiag` (T5-09) actually observes, and this ADR updated with
the measurement rather than with an inference from the legacy source.

## ADR-021 — Refuse to address an individual fan rather than approximate it

**Status.** Accepted, T3. T3-11.

**Context.** The legacy UI let the user choose fan 1 or fan 2, and implemented that
choice by writing the fan-selector register `0x31` — up to four times per attempt,
five attempts, on every cycle. `0x31` is widely described as the fan selector and
is verified for nothing; the register map's evidence table has no row for it.
`EC_REGISTER_MAP.md` §10 records the defect and what removing it costs.

T3-11 requires the write to be gone. The question this ADR answers is what the
application does instead when a caller still asks for a specific fan.

**Options considered.**

1. *Ignore the request and apply the level to the selected fan.* Zero new code, and
   the status line would show `OK`. Rejected: on a two-fan machine the two fans may
   be different hardware with different thermal limits, and the user would have
   asked for a level on one of them specifically. Reporting success for an action
   that did not happen is the specific failure mode that made this defect hard to
   see in the first place.

2. *Keep `0x31` behind a configuration flag.* The write is preserved and simply
   defaulted off. Rejected: a class or flag whose safety depends on nobody enabling
   it is a weaker guarantee than one that cannot do the thing. The rest of this
   codebase is built on the opposite principle — `EcBus` refuses `0x31` before I/O
   and the T14 profile never opts in, and there is no setting that turns that on.

3. *Refuse, and say which fan was asked for.* More code, and the user sees an
   error where they used to see a success. Accepted.

**Decision.** `LegacyFanTarget` has exactly one writable value,
`FirmwareSelected`, which means "the fan the firmware has already selected" and
requires no selector write. `FirstFan` and `SecondFan` exist so the refusal can
name the fan that was asked for. `evaluateIntent` refuses them with
`SpecificFanNotSupported`, and the message says that no register was written —
because a refusal that leaves the user guessing whether something happened is
only half an answer.

`AppBridge` has no code path that can write `0x31`, and `EcBus` refuses the
register before touching the bus. Re-enabling selector writes requires a Phase 0
hardware report establishing what `0x31` is on this machine, and then the full
§9 change-control list: profile version, regression test, documented rollback,
second reviewer.

**Consequences.**

- The per-fan feature is gone from the UI until a machine is verified. This is a
  real functional loss and is recorded as one rather than presented as a fix.
- The single-fan path is the one with test coverage, which is the path the T14
  profile uses. The uncovered path is the one that would need hardware evidence
  first.
- The refusal is a *decision*, not an error: it has its own enum value, its own
  text, and its own tests, and the UI can style it differently from a failed write.

**Revisit conditions.** A Phase 0 hardware report that establishes the meaning of
`0x31` and the fan topology on the target machine, plus the §9 change control.

## ADR-022 — The EC backend is read-only by default, with no way back

**Status.** SUPERSEDED by ADR-023, T3. Retained because the reasoning error is
worth reading: the conclusion was right, and it was reached by a route that
does not work. See "What ADR-022 got wrong" below.

**Context.** T3-02 routes the application's EC access through `IIoBackend`.
`LegacyBackend` adapts the application's existing `ReadByteFromEC` and
`WriteByteToEC` rather than reimplementing the two-phase protocol (ADR-020), so
the portable core drives the same code path the legacy application always used.

That leaves a question the portable core does not otherwise have to answer: when
is a write permitted at all? Today the answer is "whenever the code path gets
there". On an unverified machine, `0x2F` and `0x31` are candidates, not
measurements, and a write is how a candidate becomes a change to someone's
embedded controller.

The obvious design is a flag. The alternative is a state the object cannot leave.

**Decision.** `LegacyBackend` has `makeReadOnly()`. It does **not** have
`makeWritable()`. Once read-only, a write fails with `IoErrorCode::Unsupported`
and a message saying the register map has not been verified on this machine —
not `NotInitialized`, because the driver state is not the reason and sending a
user to look for a driver problem that does not exist is its own small failure.

`CoreInit` calls `makeReadOnly()`, and that is the single line to change when a
machine is verified. It is a line in source, in the same commit as the hardware
report, where a reviewer sees it — not a setting in a config file, which is where
an enabling change is hardest to notice and easiest to ship.

The read-only backend is `BackendState::Ready`, not `Faulted`. It does everything
monitor-only operation needs, and calling it `Faulted` would suggest something is
wrong when it is in fact working exactly as configured (ADR-007, T3-09).

**Consequences.**

- Control cannot be enabled on any machine until Phase 0 completes, by
  construction. That is the intended state, not a limitation to work around.
- The independent capability report is unchanged: even with a writable backend, the
  Phase 0 verdicts in `CapabilityReport` are what make control ineligible, so
  there are two separate barriers and neither is a UI setting.
- Enabling control on a verified machine is a code change and gets reviewed like
  one.

**Revisit conditions.** Never for the absence of `makeWritable()`. If a future
need requires a writable backend on a machine that is not verified, that need
should be treated as a safety defect, not a feature.

**What ADR-022 got wrong.** Two things, both found by running the code rather
than reading it.

*The barrier was in the wrong layer.* It was placed on the backend, on the
reasoning that a backend which cannot write cannot cause harm. But the EC
protocol writes a command byte to the status port even to READ, so `EcBus`
rejects a backend that cannot write ports. A "read-only" backend therefore made
**every read fail**, with "backend cannot write ports, which a read requires".
The application would have displayed nothing. The claim that this was "Ready,
and does everything monitor-only operation needs" was false, and it was written
into ADR-022 and SAFETY.md 4.1 in the same commit that introduced the code.

The barrier cannot be "no port writes". It has to be "no REGISTER writes", and
only the layer that knows which writes are register writes can enforce that.

*`LegacyBackend` was the wrong shape entirely.* It implemented `IIoBackend`,
whose methods take ports, by handing those numbers to
`FANCONTROL::ReadByteFromEC` / `WriteByteToEC` - which take register offsets
and speak the whole two-phase protocol themselves. Every `EcBus` operation
therefore started a second transaction nested inside the first, and a single
register read wrote registers 0x04 and 0x00. The `makeReadOnly` flag was beside
the point: the class should never have held a register-level API.

Both errors had the same cause: a correct-sounding claim about a layer written
without asking what number actually arrives at the layer below. The correction
is ADR-023.

## ADR-023 — The register-write barrier belongs to EcBus, and the backend is a port backend

**Status.** Accepted, T3. Supersedes ADR-022. Amends ADR-020.

**Context.** T3-02 had to connect the portable core to the legacy application's
privileged I/O. The first attempt put the control barrier in the backend, as
`LegacyBackend::makeReadOnly()`, and gave that class register-offset primitives
that implemented the EC protocol themselves. Both choices are described, with
their consequences, in the two sections above.

The generalisation: **`IIoBackend` is a port interface, and the EC protocol sits
above it.** Anything that implements `IIoBackend` must forward port numbers
unchanged and interpret nothing. A backend that knows what a register is, or
what the command sequence is, has duplicated a layer, and the two copies will
diverge - which is precisely what happened here.

**Decision 1 — `EcBus::setRegisterWritesAllowed`, false by default.**

`writeRegister` refuses before the transaction is attempted and before any port
is touched, with `IoErrorCode::Unsupported` and a message that says why and
that reads are unaffected. Reads are entirely unaffected, which is the point:
monitor-only operation must work on a machine where control has never been
enabled.

Deny by default rather than allow by default, so the code that can change
someone's fan is the code that has to be edited to do it. `BridgeConfig` carries
the flag and `AppBridge` applies it in its constructor, so the bus's policy is
always exactly what the configuration said and cannot be flipped afterwards by
somebody who forgot.

`CoreInit` contains no line enabling it. Not commented out, not read from a
configuration file - absent. Adding it is a source change made in the same
commit as the hardware report that justifies it.

**Decision 2 — `LegacyBackend` is a port backend.**

It wraps TVicPort's `ReadPort` and `WritePort`, forwards port numbers
unchanged, and records a trace of every call. The trace is not instrumentation
for its own sake: it is how a claim like "this cycle changed no register" is
checked, and it is what would have caught the nested-transaction defect. The
first version of the tests asserted on return values and passed while the
system was wrong; the ones that matter now assert on the sequence of numbers
that reached the primitive.

**Decision 3 — the fan-selector switch stays separate.**

Enabling register writes does not enable the fan selector. They are different
claims about different machines: one says "I know what these registers mean",
the other says "I know how to select a fan on this topology". Phase 0
establishes the first. The second still does not exist (ADR-021).

**Consequences.**

- Every test that writes now says so, through a named `makeWritable()` call.
  Which tests exercise a write is visible rather than implicit, and a new test
  that forgets fails loudly instead of silently asserting the refusal.
- `EcBus` gains a piece of policy. That is the right place for it: the bus is
  the only layer that can tell a register write from a command-port write.
- The application has one implementation of the EC protocol. Two was always
  going to be a bug waiting for a hardware report to find.

**Revisit conditions.** Never for the deny-by-default direction. If a future
need requires register writes on a machine that has not been verified, that need
is a safety defect, not a feature.

## ADR-024 — The read-only diagnostic is read-only by construction

**Status.** Accepted, T5-09.

**Context.** `ecdiag` is the tool that will collect Phase 0's read-only EC
evidence (T4-07 … T4-12). Its whole value is that it cannot write. `EC_REGISTER_MAP.md`
§10 records how the previous application already failed exactly this test: the
fan-selector write sat in `SetFan()` for years, four writes per attempt, without
anyone having decided to put it there. A rule that a diagnostic must not write is
therefore in the same category as a comment saying "do not write to register X":
it is true until somebody edits the file for an unrelated reason.

Two weaker designs were rejected before this one.

*Rejected: a runtime flag.* A `--no-write` option, or a bus configured with
writes disabled, produces a tool that does not write **today**. The write path is
still present, compiled, and reachable by any future edit that flips a flag, and
no test would fail if it were flipped — the test suite would simply be exercising
a different configuration.

*Rejected: a static rule against calling `writeRegister`.* This is closer, but a
rule about calls is only as good as its patterns. The same reasoning as ADR-023:
a guard that cannot fire reads as protection while the tree is unprotected. A
grep for `writeRegister` does not fail when somebody adds a new write-capable
function and calls that instead.

**Decision 1 — the read-only constraint is the type system's job.**

The EC *configuration* and the *read-only seam* live in `core/ec_access.h`, a
header that declares no write operation anywhere:

- `EcBusConfig` — ports, masks, command bytes, timeouts, and the validation of
  those values;
- `IRegisterReader` — one read operation and one audit method, with no write
  member to add by accident;
- `ReaderAudit` — the reader's own account of what it did.

`core/ec_protocol.h` includes `ec_access.h` and adds the transactions, `EcBus`,
and `EcBusReader` (the seam that adapts a bus to `IRegisterReader`).
`core/ecdiag.{h,cpp}` and `tools/ecdiag/` include `ec_access.h` and never
`ec_protocol.h`, so `EcBus` and `writeRegister` are not merely unused by the
diagnostic: they are not nameable from its translation units.

**Decision 2 — the closure is guarded, because a closed closure is not
self-maintaining.**

One convenient `#include "ec_protocol.h"` restores the ability to name a write,
and nothing else in the build would say so. `scripts/check_ecdiag_readonly.py`
therefore walks the tool's include closure transitively, refuses any header that
is not on a short commented allowlist, refuses any write API, write-enabling call
or bus/bridge name in the tool's own sources, and refuses to pass if the runtime
trace assertions in `tests/ecdiag_tests.cpp` have been deleted. A header that
cannot be resolved is a violation rather than a skip, so the check cannot quietly
cover less than it claims.

**Decision 3 — the claim is also checked at runtime, against two witnesses.**

`tests/ecdiag_tests.cpp` drives a complete run through a **real `EcBus`** over the
fake EC and then asserts on two independent records: the bus's own write trace,
and the fake's log of registers whose value actually changed. "Attempted" and
"took effect" are different claims (ADR-023), and the tests assert on both. The
engine additionally treats a *write command byte on the status port* as an
invariant violation that outranks every other outcome, so a future regression
produces a report that says the run is a defect rather than a measurement.

**Consequences.**

- `ec_protocol.h`'s include of `ec_access.h` is the only direction of dependency.
  Splitting the configuration out was a real cost: every translation unit that
  uses `EcBusConfig` now also needs `ec_access.cpp`, which is how four of the five
  Windows test projects came to be unable to link. The fix added the file and
  `scripts/check_project_sources.py` to keep it added.
- The tool cannot perform a hardware read until a backend exists (T5-01, T5-02),
  and it says so: every `--backend` name exits 4 with the reason.
- A future maintainer who needs a write in the diagnostic cannot add one quietly.
  They have to change this ADR and the guard in the same diff, which is the point.

**Revisit conditions.** When a backend exists, the seam is extended — a
read-only method — never widened to a write. If a future diagnostic genuinely
needs a write, that is a different tool with a hardware gate, and this ADR is the
record of what would have to be re-decided.


## ADR-025 — TVicPort is a documented baseline, never the shipping backend

**Status.** Accepted, T5-11. Applies [DRIVER_BACKENDS.md](DRIVER_BACKENDS.md) §5 and §8 and [SECURITY.md](SECURITY.md) §3 to the one external dependency this repository actually contains.

**Context.** T5-11 asks for a dependency record. Writing it turned up four
findings that are decisions rather than fields:

1. The vendored import library is 32-bit — all 51 COFF members are `pe-i386` —
   while the solution declares `Debug|x64` and `Release|x64` for the
   application, and the project lists the library in all four configurations.
2. The vendor states the free package is for personal, non-commercial use, and
   that the licence to "redistribute the driver with your application" is the
   commercial one.
3. The product family's 64-bit driver is catalogued as a vulnerable driver:
   CVE-2026-30769 (CVSS 3.1 7.8, local privilege escalation via IOCTL
   `0x80002008`, no vendor fix at the time of writing) and a LOLDrivers entry
   describing device `\\.\TVicPortDevice0` with no DACL.
4. `LICENSE` attributed the library to a person whose name appears in neither
   the artefact nor the vendor's site, and pointed at a task (`T0-07`) that is
   `CONTRIBUTING.md`.

**Decision 1 — the record is an artefact, and it is checked.**

`docs/DEPENDENCIES.md` carries the ten SECURITY.md §3 fields for every
dependency, each field labelled with how it is known (verified here, vendor
statement, third-party report, not established). `scripts/check_dependencies.py`
fails when a recorded checksum or size no longer matches the file on disk, when
a dependency section is missing one of the ten fields, or when a tracked
`.lib`/`.dll`/`.sys`/`.exe`/`.msi`/`.cab`/`.zip` has no checksum row. It has its
own eight-case self-test.

**Decision 2 — no kernel component is redistributed, and nothing asks a user to
weaken a machine.**

The driver is not in this repository and is not going into it. The import
library's redistribution basis is recorded as **not established**, and `LICENSE`
now claims only what the vendor's page supports. Whether the driver loads under
Secure Boot, HVCI or the vulnerable-driver blocklist is *measured* on the target
machine (T4-06, T5-03) — never worked around, and never a reason to tell a user
to disable a protection.

**Decision 3 — TVicPort is not a candidate for the modern backend.**

It stays what it already was: the legacy baseline, measured at T5-01 and T4-06
so the replacement has something to be compared against. Adoption is closed by
its security history, not by preference. The modern backend decision belongs to
T5-02 and is recorded in `DRIVER_BACKENDS.md` §6.

**Alternatives considered.**

- *Keep the record as prose and trust it* — rejected. The field that rots is the
  checksum, and a record describing a file that is no longer in the tree is
  worse than no record, because it is quoted.
- *Delete the import library now* — rejected. It would break the legacy build
  and the baseline measurement, and the question is a release gate, not a reason
  to break the tree. The record marks it open and `RELEASE.md` gates it.
- *Treat the CVE as out of scope because no driver ships here* — rejected. The
  project's own baseline instruction is to install the vendor package; "not our
  binary" is not a mitigation, and `DRIVER_BACKENDS.md` §5 requires the security
  history to be part of the evaluation.

**Safety impact.** No new privileged access, no EC writes, no startup change.
It removes a plausible future path — bundling or auto-loading a driver — and
adds a release gate.

**Testing and evidence.** `scripts/check_dependencies.py --selftest` (8 fixtures,
including a tampered hash and an unrecorded artefact); the live run over the tree
(2 dependency sections with all ten fields, 2 artefacts hashed and matched, 1
tracked binary artefact recorded); the TVicPort hashes were taken from the
committed files. No hardware claim is made anywhere in the record.

**Consequences.** The x64 link gap is written down rather than latent — see
`BUILD.md` §5 and `DEPENDENCIES.md` §4.4. `LICENSE`'s TVicPort exception is now
accurate. A future commit that adds a binary without recording it fails the
hygiene job instead of passing review.

**Revisit conditions.** A commercial licence is obtained and a 64-bit package is
recorded (the CVE still applies to file version 5.2.1.0, so a fixed version
would be required); or T5-02 selects PawnIO and its ten fields are completed; or
the baseline is dropped entirely and the import library leaves the tree.

## ADR-026 — The TVicPort adapter does not turn on hard access by itself

**Status.** Accepted, T5-01. Amends the behaviour of the legacy application; does not change it.

**Context.** T5-01 asks for a TVicPort adapter implementing `IIoBackend`, so
the baseline can be compared with a modern backend on the target machine
(T4-06). Writing it meant reading `fancontrol/approot.cpp` again, and the
sequence there is:

```cpp
HardAccess = TestHardAccess();
SetHardAccess(NewHardAccess);   // NewHardAccess = true, unconditionally
HardAccess = TestHardAccess();  // and the result is never looked at
```

TVicPort's hard access is the mode in which the driver performs port I/O even
when its own probe says the I/O is not safe to perform on that machine. The
legacy code turns it on every time, before it has any idea what the machine is,
and then ignores what the driver says about the result. `SetHardAccess` is also
the only entry point in the vendored library that changes a driver setting
rather than reading or writing a port.

There is no third option in which this is neutral. Either the adapter copies the
call - and then a new binary, on an unverified machine, silently changes a
driver setting, in a code path nobody decided anything in - or it does not, and
the difference has to be written down somewhere a reviewer will find it.

**Decision 1 — the default is not to ask.**

`TvicPortOptions::requestHardAccess` is false. `attach()` records
`TestHardAccess()` in a `HardAccessReport`, and calls `SetHardAccess(true)` only
when the caller set that option. When the driver reports hard access unavailable
and nothing was asked for, `attach()` still succeeds: reads are attempted, the
driver's answer is recorded, and the run is a monitor-only run that says so.
Refusing to attach would be worse, because it would hide the machine's real
state behind a failure.

**Decision 2 — the decision is recorded, not just taken.**

`HardAccessReport` carries whether the driver reported hard access available,
whether this adapter asked for it, whether `SetHardAccess` was called, whether
this adapter opened the driver or found it open, and a message saying which.
A hardware report that cannot state this cannot claim the baseline ran in the
same mode as the shipped application, and that comparison is the reason T5-01
exists.

**Decision 3 — the legacy application keeps its own behaviour, unchanged.**

`approot.cpp` still calls `SetHardAccess(true)`. It is being *measured*, and a
measurement taken through code that had already been changed is not a
measurement of the thing the users have. The adapter is the modern path; the
legacy path is evidence. When a backend is chosen, the legacy path's fate is
T5-02's successor task and T1/T3's, not this one.

**Alternatives considered.**

- *Copy the legacy call so the two paths behave identically* — rejected. It
  optimises for comparison and pays with a silent driver-setting change on every
  machine, made by code that has no way to know whether the setting is
  appropriate.
- *Refuse to attach when hard access is unavailable* — rejected. It converts a
  diagnosable state ("the driver will not do port I/O here") into a failure
  message, and removes the monitor-only mode that works in exactly that state.
- *Read the setting back and report it, without offering to change it at all* —
  rejected as too rigid: a person with the machine in front of them, and a
  reason, has to be able to ask for it. The option is how they ask, and it is
  recorded.

**Safety impact.** The change reduces what the software does to a machine: one
driver setting is no longer changed without a caller asking. No new privileged
access; no EC writes; no startup behaviour change; nothing loads a driver. The
adapter cannot make a register-level decision in either direction — it forwards
port values, including 0x31 and 0x2F, and a test asserts that it does, because
refusing those is `EcBus`'s job (ADR-021, ADR-023).

**Testing and evidence.** `tests/tvicport_backend_tests.cpp` (19 tests) covers
the lifecycle against a fake DLL: construction touches nothing, attach is
idempotent, a failed open is typed and leaves nothing open, an already-open
driver is neither reopened nor closed, `SetHardAccess` is called zero times
unless asked for and exactly once with `true` when it is, an available hard
access is not touched even when the option is set, a driver closed behind the
adapter's back stops every port call, ports and all 256 byte values arrive
unchanged, and the trace records every call. Not covered, and not claimed: any
interaction with a real driver, on real hardware, under any security
configuration. There is no Windows machine here; T4-06 is where this is measured.

**Consequences.** `scripts/check_ci_steps.py` immediately earned its keep: adding
the suite to the workflow's sanitizer loop exposed that `run_core_tests.sh`'s
core-source list was missing `tvicport_backend.cpp`, so the suite could not link
in the step that runs the real script. The primitive mapping now exists once —
`makeTvicPortPrimitives`, used by both the adapter and `fanstuff.cpp`, which
previously wrote the same two lambdas out by hand. The DLL's entry points are
named in exactly one file, `fancontrol/tvicport_dll.cpp`.

**Revisit conditions.** When T4-06 measures the baseline and the answer is that
hard access is required on the target machine, that finding is recorded in the
Phase 0 report and the option is set by the code that constructs the baseline
run — with the report as its justification. If a future backend makes this
switch irrelevant, this ADR is superseded rather than deleted, because the
question will come back with the next vendor library.
