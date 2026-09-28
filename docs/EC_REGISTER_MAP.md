# EC register map and verification policy

**Status:** Candidate map only; not a release hardware map
**Target:** T14 Gen 1 Intel

## 1. Important distinction

The following addresses are common ThinkPad candidates. They are not proof of the T14 Gen 1 Intel map. Only values verified in the Phase 0 hardware report may be used by the T14 release profile.

Generic ThinkPad references commonly describe a fan-control register at `0x2F`, a tachometer at `0x84/0x85`, and temperature blocks near `0x78`. See the external reference in the project README and verify all values on the target laptop before writing.

## 2. Candidate EC data

| EC offset | Candidate meaning | Access in MVP | Verification requirement | Status |
|---:|---|---|---|---|
| `0x2F` | HFSP/fan-control command | Read/write only after verification | Read current value; controlled write; readback | Unknown |
| `0x31` | Fan selector in some dual-fan implementations | **Never write for T14 single-fan profile** | Must not be used as automatic topology probe | Rejected for T14 MVP |
| `0x84` | Fan tachometer low byte | Read-only | Correlate with external RPM while fan runs | Unknown |
| `0x85` | Fan tachometer high byte | Read-only | Correlate with external RPM while fan runs | Unknown |
| `0x78–0x7F` | Primary temperature block in legacy maps | Read-only | Correlate each non-sentinel value with independent sensors | Unknown |
| `0xC0–0xC3` | Extended temperature block in legacy maps | Read-only and optional | Read individually; one bad sensor must not invalidate all data | Unknown |

## 3. Candidate I/O ports

The legacy source uses EC ports mapped as:

```text
Data port:    0x1600
Command port: 0x1604
```

Generic documentation also discusses the conventional EC interface at `0x62` and `0x66`. The project must not switch between these mappings based on guesswork. The exact mapping must be established from the target hardware, backend behavior, and read-only evidence.

Profile fields should therefore remain explicit:

```ini
EcDataPort=0x0000
EcCommandPort=0x0000
EcPortStatus=unverified
```

`0x0000` means “not approved”; it must prevent control from starting.

### 3.1 The command encoding in `core/ec_protocol.h` is transcribed, not verified

`fancontrol/core/ec_protocol.h` models the two-phase protocol that
`fancontrol/portio.cpp` records:

```c
WritePort(EC_CTRLPORT, 0x80 or 0x81);  // read or write operation
WritePort(EC_DATAPORT, address);      // full-byte register address
WritePort(EC_DATAPORT, value);        // write only
value = ReadPort(EC_DATAPORT);        // read only
```

with `EC_CTRLPORT` `0x1604`, `EC_DATAPORT` `0x1600`, `EC_STAT_OBF` `0x01` and
`EC_STAT_IBF` `0x02`. See ADR-020.

**These are transcribed from this repository, not measured on hardware.** Being in
the codebase makes them a record of what the shipped application *does*; it does
not make them a record of what the hardware *requires*. Generic EC documentation
describes a `0x62`/`0x66` interface instead, and §3 records both mappings as
unverified. Phase 0 must confirm them read-only before any write is issued.

This distinction matters enough to be stated twice. It would be easy to argue that
constants lifted from a working program must be correct, and to skip Phase 0 on
that basis. They come from a program that has not been run against the target
machine by anyone on this project, and the generic documentation disagrees.

An earlier design encoded the address into the command byte instead, leaving only
four addressable registers. That could not reach `0x2F`, `0x31`, `0x78`, `0x84` or
`0xC0` — **every register the application uses** — so it was not a plausible
placeholder but a contradiction of the code it was meant to integrate with. ADR-019
and ADR-020 record that history and the reasoning.

## 4. Fan command semantics

The legacy ThinkPad convention is commonly described as:

| Value | Intended meaning | MVP policy |
|---:|---|---|
| `0x00` | Fan off/manual level 0 | Guard at high temperature |
| `0x01–0x07` | Normal manual levels | Allowed after verification |
| `0x40` | Disengaged/full-speed on some models | Disabled by default |
| `0x80` | BIOS/automatic control on many models | Required restore path, verify first |

These meanings are model- and firmware-dependent. The T14 profile must not enable a value merely because it is conventional.

## 5. Read protocol requirements

The EC transaction layer must:

1. acquire the EC access mutex;
2. wait for input/output buffer state with a bounded timeout;
3. clear stale output only according to the verified protocol;
4. issue the read/write command;
5. verify completion;
6. release the mutex on every path;
7. return a typed error and timestamp.

The implementation must not use a signed `char` as an EC byte. Use `uint8_t` and convert explicitly when calling a legacy API.

## 6. Read validation

A read is valid only when:

- the backend returned success;
- the EC transaction completed within the timeout;
- the value is in the expected range;
- the value is not a known sentinel;
- repeated samples are stable enough for the measurement;
- the timestamp is fresh.

Fan RPM validation should reject impossible values and distinguish “fan stopped” from “tachometer read failed.”

Temperature validation should preserve raw and adjusted values. A configured offset must never hide an invalid raw read.

## 7. Write validation

A manual write is valid only when:

- the profile explicitly allows writes;
- the value is in the allowed command set;
- the temperature provider is valid;
- the write completed;
- a readback matches the intended state;
- the safety monitor has not latched failsafe.

The single-fan path must not write the fan-selector register. The dual-fan path must be a separate, explicitly selected profile.

## 8. Evidence table

Complete this table from the hardware report. Do not replace `Unknown` with an assumption.

| Item | Value | Evidence file/measurement | Verified by | Date |
|---|---|---|---|---|
| Machine type | Unknown | | | |
| EC data port | Unknown | | | |
| EC command port | Unknown | | | |
| Fan control offset | Unknown | | | |
| BIOS/automatic value | Unknown | | | |
| Fan RPM low/high | Unknown | | | |
| Valid temperature offsets | Unknown | | | |
| Fan topology | Unknown | | | |

## 9. Change control

Any change to a verified register map requires:

- a new hardware report or firmware-specific evidence;
- an updated profile version;
- a regression test;
- a documented rollback;
- review by a second person before release.

### 9.1 Phase 0 obligations created by T5

`core/ec_protocol.h` ships with placeholder defaults. Before any control path may be
enabled, Phase 0 must establish by **read-only** measurement:

| Must be measured | Currently | Why it cannot be guessed |
|---|---|---|
| status and data ports | `0x62` / `0x66` | §3 documents two competing mappings, `0x62`/`0x66` and `0x1600`/`0x1604` |
| IBF/OBF status bit positions | `0x02` / `0x01` | not recorded as verified anywhere in the legacy source |
| read/write command bytes | `0x80` / `0x81` | transcribed from `portio.cpp`, never measured |
| whether the address really is a full byte on the data port | assumed | a shorter address field would silently alias registers again |
| timeout and poll budget | 50 ms / 1 ms | a bound that is too long delays failsafe; too short causes spurious fallbacks |

None of these may be probed by writing to an unknown register. The legacy application
contained the defect recorded in §10 — an unconditional write to the fan-selector
register — and repeating that pattern during discovery is exactly what this project
exists to avoid. T3-11 must not reintroduce it. See also §10.1.

## 10. The unconditional fan-selector write (resolved in T3)

`0x31` is widely described as the fan-selector register: writing `0x00` addresses
fan 1, writing `0x01` addresses fan 2, and the fan-level register `0x2F` then
applies to whichever was selected. That description is plausible, widely repeated,
and **not verified for this machine** — it is in none of the rows of the §8
evidence table, and this project has taken no measurement of it.

The legacy `FANCONTROL::SetFan` relied on it completely. One call did this, up to
five times:

| Step | Write | Purpose |
|---:|---|---|
| 1 | `0x31` ← `0x00` | select fan 1 |
| 2 | `0x2F` ← level | set the level |
| 3 | `0x31` ← `0x01` | select fan 2 |
| 4 | `0x2F` ← level | set the same level on fan 2 |
| 5 | read `0x2F` | verify |
| 6 | `0x31` ← `0x00` | select fan 1 again |
| 7 | read `0x2F` | verify |

That is four writes to `0x31` per attempt, so **up to twenty writes to an
unverified register from a single UI click**, none of them conditional on
anything. The consequences if `0x31` is not the fan selector on a given model:

- the writes land on some other register of the embedded controller, whose
  meaning is unknown, and there is no way to find out which;
- the damage is not visible in the application's own state, so nothing reports
  it;
- it happened on every cycle, not only on a settings change, because
  `SmartControl` calls `SetFan` whenever its computed level changes;
- the retries multiplied the exposure, so a machine where the writes *did*
  something visible was the case where the user did the most damage.

`docs/EC_REGISTER_MAP.md` §4 lists `0x31` nowhere. That is not an oversight; see
§10.1.

### 10.1 What replaced it, and why refusing is the right answer

`0x31` is no longer written by any code in this repository. The fan level is
written only to `0x2F`, which applies to the fan the **firmware** has already
selected. Three things follow, and the third is the important one.

1. **Addressing a specific fan is refused, not approximated.** The legacy UI let
   the user pick fan 1 or fan 2. Honouring that needs the `0x31` write. With it
   gone, the only alternatives were to silently apply the level to the other fan,
   or to say no. Silently applying it would report success for an action that
   did not happen, on a machine where the two fans may be different hardware
   with different thermal limits. So `LegacyFanTarget::FirstFan` and
   `SecondFan` are refused with a message naming the fan that was asked for.

2. **The refusal is permanent until Phase 0 says otherwise, and Phase 0 has not
   said.** `EcBus` has a `setFanSelectorWritesAllowed` opt-in, and the T14
   single-fan profile never calls it. `AppBridge` has no code path that can.
   Enabling selector writes requires a hardware report establishing what `0x31`
   is on this machine, a profile version bump, a regression test, and a
   documented rollback — the §9 change-control list, in full.

3. **A removal with no guard is a removal that comes back.** `SetFan` is Win32
   dialog code. No test in `tests/` can execute it and this repository is
   developed on Linux, so the regression guard is static:
   `scripts/check_legacy_ec.py` fails the build if a write to the fan-selector
   register, or a fan-level write from outside the core, reappears anywhere in
   the legacy sources. `scripts/check_legacy_ec_selftest.py` proves that guard
   still matches what it claims to, because a guard that has silently stopped
   matching reports a clean tree and a green job.

### 10.2 What is still unverified

| Item | Status | Why it is not assumed |
|---|---|---|
| Meaning of `0x31` | Unknown | Widely described, never measured. §8 has no row for it. |
| Whether `0x2F` is the fan level | Candidate | §2, §4. Needs a read-only Phase 0 report. |
| Fan topology (one fan or two) | Unknown | §8. Determines whether a selector is needed at all. |
| Whether `0x2F` applies to the selected fan | Assumption | The whole model in §10.1 rests on it. |

None of these may be resolved by writing to `0x31` to see what happens. That is
the defect.
