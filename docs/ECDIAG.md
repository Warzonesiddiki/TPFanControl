# `ecdiag` — the read-only EC diagnostic

`ecdiag` reads a fixed list of candidate Embedded Controller offsets and writes a
report that a human can quote and a script can parse. It performs no register
writes, and it has no option that would let it perform one.

**Status: implemented and unit-tested on Linux. It has never run on Windows, and
it has never read a real machine** — no live backend exists ([DRIVER_BACKENDS.md](DRIVER_BACKENDS.md),
T5-01 and T5-02), so every `--backend` name exits 4 with the reason. Everything
below is true of the tool; none of it is a hardware claim.

## 1. What it is, and what it refuses to be

It is the read-only half of the command surface described in
[CLI_DIAGNOSTICS.md](CLI_DIAGNOSTICS.md) §1: measurement and reporting, without
control. What it is *not*:

- not a control tool. There is no profile, no curve, no fan level, and no
  "apply";
- not a register explorer. `--address` accepts an offset, but the offsets this
  tool reads by default are the documented candidates from
  [EC_REGISTER_MAP.md](EC_REGISTER_MAP.md) §2, and a value read from an offset is
  reported as a raw byte — never summarised into a temperature, an RPM or a fan
  state;
- not evidence on its own. A run states which evidence grade it reached and, when
  that grade is not `hardware`, why not. A simulated run cannot be mistaken for a
  measurement because the report says so in three places.

## 2. Read-only by construction

The claim is structural, not procedural: there is no function for the tool to
call that writes.

1. **The type it is handed.** `ecdiag` takes a `core::IRegisterReader` — one read
   operation, no write member (`fancontrol/core/ec_access.h`). `EcBus` adapts to
   that interface; the tool never names `EcBus`, `IIoBackend` or the bridge.
2. **The closure it cannot escape.** `scripts/check_ecdiag_readonly.py` walks the
   tool's includes and fails the build if the closure reaches anything that can
   write. It also fails if the runtime trace assertions below disappear. It has
   a 13-case self-test and is wired into the CI hygiene job.
3. **The trace it leaves.** `tests/ecdiag_tests.cpp` runs a complete diagnostic
   over a **real `EcBus`** and a fake EC, then asserts on two independent
   records: the bus's write trace (empty) and the fake's log of registers whose
   value actually changed (empty). "Attempted" and "took effect" are checked
   separately (ADR-023). A write command on the status port is an invariant
   violation that outranks every other outcome, so a future regression produces
   a report that says *the run is a defect* rather than a measurement.

## 3. Modes and evidence grades

| Mode | I/O performed | Evidence grade in the report |
|---|---|---|
| `--plan` (default) | none at all | `plan-only` |
| `--simulate` | reads an invented register table | `simulated` |
| `--backend <name>` | none: no backend exists in this build | exits 4 before reading |
| (future) a verified backend | reads against hardware | `hardware` |

A run that never touches hardware must not be quotable as if it did. The
`evidence` block carries the grade and a `not_evidence_reason` that names the
substitution, and the limitations list repeats it in text.

## 4. Running it

Built by `./tests/run_core_tests.sh`, which also smoke-tests the modes and exit
codes. To build it alone:

```sh
g++ -std=c++17 -Wall -Wextra -Werror -pedantic \
  -I fancontrol/core -I tools/ecdiag \
  fancontrol/core/*.cpp tools/ecdiag/*.cpp \
  -o /tmp/ecdiag -pthread
```

The sources are `fancontrol/core/ecdiag.{h,cpp}` (the portable engine),
`tools/ecdiag/main.cpp` (the front end) and `tools/ecdiag/simulated_ec.{h,cpp}`
(an invented-table reader that implements **only** `IRegisterReader`).

```
ecdiag [--plan | --simulate | --backend <name>]
       [--address <byte>]... [--range <first-last>]... [--candidates]
       [--samples <n>] [--list-candidates]
       [--status-port/--data-port/--read-command/--write-command/
        --ibf-mask/--obf-mask/--timeout-ms/--poll-ms/--attempts/--recovery-ms]
       [--json] [--output <file>] [--force] [--quiet] [--version] [--help]
```

Configuration defaults are **candidate values transcribed from the legacy source**
(`fancontrol/portio.cpp`), not measurements; the report records them, says where
they came from, and marks the encoding `unverified-candidate`. `--output` writes
the path directly (never through a shell), refuses to replace an existing file
without `--force`, and reports a failure as exit 8 rather than silently not
writing.

## 5. The report

### Text

A header with tool version, generation time, mode, backend, the configuration and
its source; one row per planned offset with its candidate meaning, the values
observed, and whether the value was stable; the audit; the read-only invariant;
the outcome; and the limitations. In `--plan` mode the value cells say `not read`
— the table is driven by the plan, so an unperformed read is visibly unperformed
rather than absent.

### JSON (`tpfancontrol.ecdiag/1`)

| Key | Contents |
|---|---|
| `schema` | `"tpfancontrol.ecdiag/1"` — a consumer must check this string |
| `tool` | name, version, build flavour, generation time (UTC), purpose |
| `evidence` | `grade` (`hardware` / `simulated` / `plan-only`) and `not_evidence_reason` |
| `config` | every setting used, decimal and hex, plus `source`, `encoding_status`, `sane`, `validation_message` |
| `backend` | name, version, state, capabilities (or explicit nulls) |
| `plan` | samples per target, target count, and the targets with their candidate meanings |
| `summary` | targets, samples, successful, failed |
| `reads[]` | per offset: success/failure counts, `stable`, `distinct_values`, min/max, first error, and every sample with its own result, value, error and elapsed time |
| `audit` | reads received by the reader, bus transactions, timeouts, status-port writes (count and distinct values), data-port writes |
| `read_only_invariant` | `held`/`violated`, write commands observed, and each failure |
| `limitations[]` | what this particular run does and does not establish |

Unknown values are explicit `null`, never `0`. A failure carries an error code
and a message; a failed read reports **no value** rather than a fabricated zero,
and a legitimate `0x00` is reported as a value.

## 6. Exit codes

| Code | Meaning |
|---|---|
| 0 | `ok`, or `plan_only` — the run did what it said and nothing was wrong |
| 1 | general error, or a **read-only invariant violation** (never a measurement) |
| 2 | bad arguments |
| 3 | invalid configuration (e.g. the two ports are the same) |
| 4 | backend unavailable — no backend exists, or the named one is not implemented |
| 6 | sensor data unavailable — no read succeeded, or only some did |
| 8 | report not written (`--output` in use) |

Codes 5 (identity/profile not verified) and 7 (failsafe active) are unreachable
**by design**: this tool has no control path to gate and no failsafe state to
report. [CLI_DIAGNOSTICS.md](CLI_DIAGNOSTICS.md) §3 defines them for the
application command surface; a diagnostic must not invent them.

## 7. The analysis invariants

Four checks run over the audit, because a read-only tool that quietly did
something else is the failure this project cares most about:

1. no write command was observed on the status port;
2. no value was written to the data port that was not part of the plan;
3. the data-port write count does not exceed the status-port write count;
4. the reader's own read count equals targets × samples.

Any failure is reported as `InvariantViolated` (exit 1) and outranks a complete
set of readings — a partial read set that is honest is a better result than a
full one produced by a machine that did something unexplained. Failures are
capped at 8 messages plus a count of further findings, both in the JSON and in
the text report.

## 8. What it does not do

- **It does not read a machine yet.** Every `--backend` name exits 4 with a
  reason naming the task that must land first (T5-01, T5-02). The tool has been
  exercised against a real `EcBus` and a fake EC; it has never touched a port.
- **It does not establish that an offset means what its label says.** The labels
  are candidates. Correlating a byte with an independent measurement is a human
  step ([HARDWARE_VERIFICATION.md](HARDWARE_VERIFICATION.md) §7).
- **It does not decide anything.** No report sets a register, chooses a profile,
  or claims a machine is safe to control.
- **It does not prove the port mapping.** The two competing mappings
  ([EC_REGISTER_MAP.md](EC_REGISTER_MAP.md) §3.1) are configuration, and the
  report says which configuration produced it.

## 9. Where it fits

- **T4-07 … T4-12** — this is the tool that performs the read-only measurement on
  the target machine. Its report is what goes into the Phase 0 hardware report,
  with a real backend behind it.
- **T5-01, T5-02** — the backends. When one exists, `--backend <name>` becomes a
  hardware run and the evidence grade becomes `hardware`.
- **T7-11** — the in-application status and profile-validation command surface is
  still to come; it will share this report format rather than invent a second one.

## 10. Verification performed, and not performed

Performed here: `tests/ecdiag_tests.cpp` (29 tests) covering the plan, the
four invariants (each fired deliberately), partial and total read failure,
`0x00` as a value, unstable values, timeouts, configuration refusal, the report
schema, JSON escaping with a self-testing parser, and exit-code agreement with
the table above; `scripts/check_ecdiag_readonly.py` with its 13-case self-test;
the build and smoke test inside `./tests/run_core_tests.sh`.

Not performed: any Windows build, any MSVC compile, any run against a real
backend, and any measurement of any register. The `ecdiag` tool is not evidence
that this project can read an EC — it is the instrument that will produce that
evidence when T5-01 or T5-02 lands.
