# `ecdiag` — the read-only EC diagnostic

Status: **implemented and unit-tested on Linux.** See [§9](#9-verification-performed-and-what-is-not-verified)
for exactly what that does and does not include. It has no live backend, so it has
never read a real EC — see [§7](#7-what-is-not-established).

`ecdiag` reads a fixed list of candidate embedded-controller offsets and writes a
report. It has no register-write operation anywhere in its code, and no option
that would allow one. Its purpose is to make the Phase 0 measurements in
[EC_REGISTER_MAP.md](EC_REGISTER_MAP.md) §9.1 collectable in a form a report can
quote, on a machine where nothing may be written yet.

It is **not** the `TPFanControl.exe --status` command described in
[CLI_DIAGNOSTICS.md](CLI_DIAGNOSTICS.md) §1. Those commands are an integration
target inside the application. `ecdiag` is a separate, portable tool that exists
so the read-only half can be built, tested and run before any of that is wired up.

## 1. What it does

- reads a set of offsets an explicit number of times each (default 3, maximum 32);
- records, per offset, whether every read succeeded, whether the value was stable
  across samples, and the minimum and maximum seen;
- reports the configuration the run used, including the port numbers, masks,
  command bytes and timeouts;
- reports what the reader says it did, and checks that account against the plan;
- refuses to do anything else.

The default target list is the documented candidate set — `0x2F`, `0x31`, `0x84`,
`0x85`, `0x78`–`0x7F`, `0xC0`–`0xC3` — with the candidate meaning of each offset
recorded beside it. Every one of those labels is a **candidate**, transcribed from
the legacy source or from generic documentation; the report says so at every
level, and `--list-candidates` prints the set without touching anything.

## 2. Modes, and what each one is evidence of

| Mode | What happens | Evidence grade in the report |
|---|---|---|
| `--plan` (default) | Nothing is read. The plan and the configuration are recorded. | `plan-only` |
| `--simulate` | Reads run against an invented register table inside the tool. | `simulated` |
| `--backend <name>` | Would use a hardware backend. None exists in this build; every name reports why and exits 4. | — |

A fourth grade, `hardware`, exists in the schema and is reachable only once a real
backend performs the reads. Until then every report carries an explicit
`not_evidence_reason` saying which of the two weaker things it is. There is no
mode that produces a `hardware` grade today, and no code path that could assign
one to a simulated or planned run.

Plan-only mode is deliberately not `--quiet` about being empty. Its table prints
`not read` in every value cell rather than omitting the rows, because a report
that looks empty and a report that says "nothing was read" invite different
mistakes: the first looks like a run that found nothing, which is a finding; the
second is a statement about the report itself. `PlanOnly` is also a distinct
outcome from `Ok` in the schema, for the same reason.

## 3. Running it

The portable runner builds and exercises it on every leg:

```
./tests/run_core_tests.sh          # builds all suites and the tool
```

On Windows there is `tools/ecdiag/ecdiag.vcxproj`, registered in
`fancontrol/fancontrol.sln` with the same four configurations as every other
project. It shares the portable sources with the Linux build, and
`scripts/check_project_sources.py` compiles the project's own source list on this
machine so the two cannot drift apart silently.

```
ecdiag --plan                      # what a run would do; no I/O
ecdiag --plan --json               # the same, machine-readable
ecdiag --simulate                  # exercise the tool against invented values
ecdiag --address 0x2F --samples 5  # a narrower, longer plan
ecdiag --list-candidates           # the offsets and their candidate meanings
ecdiag --output report.json --force
```

`--output` opens the path directly. It is never passed to a shell, and an
existing file is never replaced without `--force`. A failure to write the report
exits 8 and says so on stderr.

## 4. Read-only by construction

Three independent mechanisms, because the claim is the whole point of the tool
and a single one of them could be true while the others were not.

**The type.** `ecdiag` is given a `core::IRegisterReader` — one read operation,
one audit method, no write member — and its headers include only
`ec_access.h`, `io_backend.h` and itself. `EcBus`, `writeRegister` and the control
path are not merely unused: they are not nameable from the tool's translation
units. This is why the EC configuration lives in `ec_access.h` and not inside
`ec_protocol.h`; see [ADR-024](DECISIONS.md#adr-024--the-read-only-diagnostic-is-read-only-by-construction).

**The closure guard.** `scripts/check_ecdiag_readonly.py` walks the tool's
`#include` closure transitively and refuses any header that is not on a short,
commented allowlist. A closed closure is not self-maintaining: one convenient
`#include "ec_protocol.h"` restores the ability to name a write, and nothing in the
build would say so. The guard also refuses to pass if the runtime assertions in
`tests/ecdiag_tests.cpp` have been deleted, because a structural guarantee whose
evidence has been removed is a guarantee nobody is checking. Its `--selftest`
proves each rule still fires, including the three cases that must *not* fire.

**The runtime evidence.** `tests/ecdiag_tests.cpp` drives a complete run through a
**real `EcBus`** over the fake EC and then asks two independent witnesses what
happened: the bus's own write trace, and the fake's log of registers whose value
actually changed. The invariant is that no status-port write may carry the write
command byte, no data-port write may be anything other than a planned address, the
number of data-port writes may not exceed the number of command writes, and the
reader's own read count must equal the plan. A reader that lied about any of it
would have to lie somewhere the tests can see.

The guard also states what it cannot do, which is worth repeating: it cannot prove
the remaining test still asserts anything meaningful. That is why the property was
broken on purpose during development — a real register write through a real bus —
and the suite failed, as it should have.

## 5. The report

Text output is for a person; `--json` is the same run in the form a report can
quote. The schema is `tpfancontrol.ecdiag/1`.

| Field | Contents |
|---|---|
| `tool` | name, version, build flavour, UTC timestamp, purpose |
| `evidence` | `grade` (`hardware` / `simulated` / `plan-only`) and, when it is not hardware, `not_evidence_reason` |
| `outcome`, `exit_code` | the result, and the code the process exits with |
| `configuration_error` | non-null only when the plan or configuration was refused |
| `config` | every value the run used, in decimal and hex, with `source` and `encoding_status` |
| `backend` | name, version, state, capabilities — or `null` where there is no backend |
| `plan` | samples per target, target count, and each target's address and candidate meaning |
| `summary` | targets, samples, successful, failed |
| `reads[]` | per offset: success and failure counts, stability, distinct values, `value`/`value_hex`, min/max (or `null`), and the first error |
| `audit` | the reader's own account: reads received, bus transactions, timeouts, and every status- and data-port write value |
| `read_only_invariant` | `held` or not, the number of write command bytes observed, and any failures |
| `limitations[]` | what this particular run does not establish |

Unknown values are `null`, never `0`. A zero is a legitimate byte on an EC, so
using it for "no value" would make a missing measurement indistinguishable from a
measured zero — the distinction the whole report exists to preserve. Ports are
16-bit and printed from the full value, so `0x1604` cannot appear as `0x04`.

`encoding_status` is always `unverified-candidate`: the port mapping, the command
bytes and the masks are transcribed from `fancontrol/portio.cpp`, which records
what the shipped application does, not what the hardware requires.

## 6. Exit codes

| Code | Meaning |
|---:|---|
| 0 | Completed: `ok`, or `plan_only` |
| 1 | A general error, or the read-only invariant was violated |
| 2 | Invalid command line |
| 3 | The configuration or plan is unusable |
| 4 | Backend unavailable |
| 6 | No read succeeded, for a reason that is not a missing backend |
| 8 | The report could not be written |

Codes 5 and 7 are in [CLI_DIAGNOSTICS.md](CLI_DIAGNOSTICS.md) §3 but unreachable
here by design: there is no control path to gate and no failsafe state to report.
An invariant violation exits 1 rather than a code of its own, because a
diagnostic that is not read-only has no results worth reading and the run is a
defect report, not a measurement.

## 7. What is not established

- **No machine has been read.** There is no backend in this build. `--backend
  tvicport` is T5-01 and needs Windows and the vendor driver; `--backend pawnio`
  is T5-02, which is blocked on a disposable Windows install and an HVCI-capable
  machine. Both exit 4 without reading, and both say why.
- **A value read from an offset does not mean what the offset's label says.**
  Reading is not interpreting. Correlating a byte with a temperature, an RPM or a
  fan state is a human step ([HARDWARE_VERIFICATION.md](HARDWARE_VERIFICATION.md)
  §7), and it is why the report records raw values with their candidate labels
  rather than a summary.
- **`--simulate` is not evidence about anything.** It runs against an invented
  table with no ports behind it. Its only claim is that the tool, the checks and
  the report format work.
- **A clean report is not a verification.** It says the reads succeeded and
  nothing was written. It does not say the values are correct, or that the
  configuration is the one this machine uses.

## 8. Where it fits

Once a backend exists, the intended use is Phase 0:
[T4-07 … T4-12](IMPLEMENTATION_CHECKLIST.md) are read-only evidence collection,
and `ecdiag` is how that evidence is produced and recorded, with the report's
`config` block recording the configuration it was produced under. The tool
deliberately does not decide anything: classification stays with a person and ends
up in `docs/reports/`.

[T7-11](IMPLEMENTATION_CHECKLIST.md) — a read-only CLI for status and profile
validation inside the application — remains to be done. It shares this tool's
contract ([CLI_DIAGNOSTICS.md](CLI_DIAGNOSTICS.md)) but not its code.

## 9. Verification performed, and what is not verified

Verified here, on Linux, with g++:

- 29 tests in `tests/ecdiag_tests.cpp`, run by `tests/run_core_tests.sh`, covering
  the engine, plan-only mode, the invariant checks, the JSON writer, the strict
  JSON self-test, and the front end's option parsing and exit codes;
- the tool is built and run by the same script on every CI leg, with its exit
  codes (4 for a backend, 2 for a bad option, 3 for an unusable configuration)
  asserted rather than assumed;
- `scripts/check_ecdiag_readonly.py` and its 13-case self-test;
- `scripts/check_project_sources.py` builds `tools/ecdiag/ecdiag.vcxproj` from its
  own source list.

Not verified: **the tool has never run on Windows and has never read a real EC.**
`tools/ecdiag/ecdiag.vcxproj` and `tests/ecdiag_tests.vcxproj` are present and
mapped into all four solution configurations, and their source lists are compiled
here, but building them under MSVC is unverified until
`ci/ci.yml`'s `windows-build` job runs. Until a backend exists, a hardware report
cannot be produced by this tool at all.
