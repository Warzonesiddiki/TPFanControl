# Contributing to TPFanControl

Thanks for looking. This project controls laptop cooling fans by writing to the
Embedded Controller, so the bar for a change is "obviously safe" rather than
"probably works". This document is the contract for contributions.

## The one rule that overrides everything

**No change may cause a fan command without the safety gates in
`fancontrol/core/` having approved it.** If you cannot explain which gate
approves your write, you have not finished the change.

## Before you write code

1. Read `docs/IMPLEMENTATION_CHECKLIST.md`. It is the single source of truth for
   what is done, what is in progress, and what is blocked. Every task has a
   stable ID.
2. Read `docs/SAFETY.md`, especially the numbered requirements and the open-gap
   table. If your change touches a numbered requirement, name it in the commit
   message.
3. Check `docs/ROADMAP.md` for the current phase. Work out of phase is rejected
   even if it is good work.

## Safety rules, non-negotiable

- **Never** probe a writable EC register automatically. Discover capability by
  reading, or do not discover it. See `docs/EC_REGISTER_MAP.md` section 2.
- **Never** treat a candidate value in a source file as hardware evidence. The
  values marked CANDIDATE must be replaced by a measurement recorded in
  `docs/reports/`, or the feature stays off.
- Enabling control requires verified hardware, profile, topology, backend, and
  restore capability — all five, before the first command.
- A control decision requires consecutive valid sensor readings, and fails safe
  to BIOS/automatic behaviour on anything else.
- Monitor-only operation must keep working when the backend is absent, and the
  application must say truthfully when a restore could not be issued.
- **Do not claim crash-safe restoration.** A user-mode process cannot guarantee
  restoration after a kernel crash or power loss. See `docs/SAFETY.md` section 9.
- **Never** ship or automatically load an unreviewed kernel driver. See ADR-014.

## Testing requirements

- New core behaviour needs a test that **fails without your change**. A test that
  passes either way is not a test; delete it or fix it.
- `sh tests/run_core_tests.sh` must pass. It runs on Linux, macOS and Windows and
  needs no hardware, no driver, and no administrator rights.
- Run the sanitizer build before opening a pull request:

  ```sh
  g++ -std=c++17 -Wall -Wextra -Werror -pedantic \
      -fsanitize=address,undefined -fno-omit-frame-pointer -g \
      -I fancontrol/core fancontrol/core/*.cpp \
      tests/fake_ec.cpp tests/ec_protocol_tests.cpp -o /tmp/ec_san
  /tmp/ec_san
  ```

- Core sources are held to `/W4` with warnings as errors under MSVC. Do not
  suppress a warning to get a green build; fix it or record why in an ADR.
- **Tests use `CHECK`, not `assert`.** `assert` is compiled out under `NDEBUG`,
  so a Release test build would silently run nothing. See `tests/test_check.h`.

## What must never be committed

- Build output, logs, CSV traces, or a runtime copy of the INI.
- A Windows user name, machine serial, or any other private path. Run
  `git diff --check` and search your diff before committing.
- A prebuilt executable. Build artifacts are not distributed from this
  repository; see ADR-016.
- Credentials. This project has no network component and must not acquire one
  without a decision record.

## Style

Match the surrounding file. The legacy application uses a house style that
predates this document: tabs, braces on the same line, and `_prec.h` as the
precompiled header. New portable code under `fancontrol/core/` is C++17 and uses
the conventions already established there, which are deliberately different.
Do not reformat unrelated lines.

## Commit messages

Say what changed and why, and name the safety requirement or task ID it
touches. If a change deliberately does not close a gap, say so explicitly — a
partially closed gap that reads as closed is worse than an open one.

## Decisions, not code, for policy questions

Some questions have no obviously correct engineering answer: how many sensors
must agree, what the operator sees after a restore could not be issued, how long
a failsafe latch lasts. Those are recorded as ADRs in `docs/DECISIONS.md` and as
`[!]` blocked tasks in the checklist. If you find yourself inventing a policy
inside a function, stop and write an ADR instead.

## Reporting a safety problem

If you believe a change can command a fan when it should not, or can fail to
hand the fan back to the BIOS, treat it as a blocking defect. Say so plainly,
with the test that reproduces it, and do not merge around it.
