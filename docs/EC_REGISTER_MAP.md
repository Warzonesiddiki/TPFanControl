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
