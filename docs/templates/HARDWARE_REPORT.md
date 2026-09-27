# T14 hardware verification report

Copy this file to a dated report such as:

```text
docs/reports/T14Gen1-Intel-<machine-type>-<yyyy-mm-dd>.md
```

## Report metadata

- Operator:
- Date/time:
- Location/ambient temperature:
- Report status: draft / complete / blocked
- Related issue/commit:

## System identity

- Manufacturer:
- Product name:
- Machine type:
- Product/serial identifier: **redact before publishing**
- CPU:
- Graphics configuration:
- BIOS version:
- EC firmware version, if available:
- Windows edition:
- Windows build:
- OS architecture:
- Secure Boot:
- HVCI/Memory Integrity:
- Lenovo/other power mode:

## Backend environment

- Existing TVicPort installed: yes/no/unknown
- TVicPort version:
- Candidate modern backend:
- Backend version:
- Driver architecture:
- Driver signature result:
- HVCI result:
- Other EC utilities running:

## Baseline measurements

| Test | AC/battery | Load | CPU/package temp | EC temp(s) | RPM | Fan state | Tool/version |
|---|---|---:|---:|---|---:|---|---|
| Idle 10 min | | | | | | | |
| Browser/video | | | | | | | |
| Load 5 min | | | | | | | |
| Load 15 min | | | | | | | |
| Cooldown | | | | | | | |

## Read-only EC evidence

- EC dump filename/hash:
- Data port candidate:
- Command port candidate:
- Mutex/access notes:

| Offset/port | Raw values observed | Correlated measurement | Classification |
|---|---|---|---|
| `0x2F` | | | |
| `0x31` | | | |
| `0x84` | | | |
| `0x85` | | | |
| `0x78–0x7F` | | | |
| `0xC0–0xC3` | | | |

## Controlled write evidence

Complete only after read-only evidence and operator approval.

- Approved by:
- Safety preconditions met: yes/no
- BIOS/automatic value:
- Manual level tested:
- Write/readback result:
- Audible/RPM response:
- Maximum observed temperature:
- Restore result:
- Unexpected behavior:

## Profile decision

- Profile ID:
- Fan topology:
- Approved writable registers:
- Approved read-only registers:
- Unsupported registers:
- Initial safety threshold:
- Evidence limitations:

## Conclusion

- [ ] T14 single-fan profile may be used for read-only monitoring.
- [ ] T14 single-fan profile may be used for manual control.
- [ ] T14 single-fan profile may be used for smart control.
- [ ] Backend is approved for release testing.
- [ ] Additional evidence required.

## Reviewer notes

-
