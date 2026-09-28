# Vibe-coding implementation guide

This project is being developed with agent-assisted/vibe coding. The rules below keep fast iteration from turning into unsafe hardware experimentation or undocumented architecture.

## 1. One task, one boundary

Every coding task must name:

- phase and milestone;
- files expected to change;
- hardware-independent or hardware-dependent status;
- acceptance criteria;
- test command or evidence required;
- documentation that must be updated.

Bad task:

```text
Modernize everything and make fan control work.
```

Good task:

```text
Phase 2 / M3: extract EC read/write calls behind IIoBackend.
No hardware writes change. Add a fake backend and preserve behavior.
Acceptance: source compiles, fake timeout tests pass, no UI changes.
```

## 2. Agent preflight

Before editing:

1. Read `docs/README.md`.
2. Read the current phase in `docs/ROADMAP.md`.
3. Read the relevant decision records.
4. Inspect `git status` and `git diff`.
5. Check whether the requested change adds or changes an EC write.
6. Check whether the current branch contains unfinished work.

Never overwrite user changes without inspecting them.

## 3. Hardware-write rule

Any change that can write an EC register requires all of:

- exact target profile;
- documented register evidence;
- bounded timeout;
- readback verification;
- safety gate;
- fake-backend test;
- documented physical test procedure.

An agent must not create an automatic “probe” that writes candidate values.

## 4. Scope control

Do not implement Phase 3–7 features while Phase 0–2 gates are open. If a later feature appears necessary, document it as a dependency or decision record instead of silently expanding scope.

Prefer:

- small interfaces;
- small commits;
- reversible changes;
- tests before refactors;
- typed errors;
- explicit feature flags;
- migration adapters.

Avoid:

- large generated rewrites;
- replacing the UI before the controller is stable;
- adding a new driver dependency without a feasibility spike;
- copying code from an unrelated fan-control project without checking its license and model assumptions;
- hiding failures behind retries with no upper bound.

## 5. Required response format for coding tasks

Every implementation task should finish with:

```text
Changed:
- ...

Not changed:
- ...

Validation:
- command/result

Hardware impact:
- none / read-only / writes register <x> under condition <y>

Known limitations:
- ...

Docs updated:
- ...

Next smallest task:
- ...
```

## 6. Commit and review rules

A commit should have one purpose, for example:

```text
refactor: add typed EC backend interface
fix: prevent second-fan writes in single-fan profile
 test: add EC timeout fault injection
 docs: record T14 phase-0 findings
```

Before review:

- build from a clean output directory;
- run the relevant unit/fake-backend tests;
- inspect the complete diff;
- run `git diff --check`;
- confirm no logs, binaries, credentials, or generated databases were added;
- update the implementation checklist.

## 7. Prompt template

Use this template for future agent tasks:

```text
Phase/milestone:
Goal:
Current behavior:
Expected behavior:
Hardware impact: none/read-only/EC write
Allowed files:
Required tests:
Required documentation:
Do not do:

Implement the smallest reversible change. Do not make unverified hardware
assumptions. Report changed files, validation, risks, and next task.
```

## 8. Stop and ask for evidence

The agent must stop and request hardware evidence when:

- the exact machine type is unknown;
- a register has only community-level evidence;
- the backend is blocked by HVCI/Secure Boot;
- a write does not read back correctly;
- temperature sources disagree materially;
- a safety rule cannot be tested;
- a proposed fallback requires disabling Windows security.
