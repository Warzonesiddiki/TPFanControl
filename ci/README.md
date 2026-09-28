# Continuous integration

The workflow definition is version-controlled here as [`ci.yml`](ci.yml).

## Why it is not at `.github/workflows/ci.yml`

GitHub only runs a workflow from `.github/workflows/`. The push credential
available in this environment is a GitHub App installation token without the
`workflows` permission, so it is refused with:

> refusing to allow a GitHub App to create or update workflow
> `.github/workflows/ci.yml` without `workflows` permission

Rather than leave the CI definition unversioned — which is how the earlier
build of this work was lost — the file is tracked here. `.github/` is ignored
with that reason recorded in the rule, so a `git add -A` cannot quietly
reintroduce an unpushable path.

## Enabling it

A maintainer with workflow permission runs, from the repository root:

```sh
mkdir -p .github/workflows
cp ci/ci.yml .github/workflows/ci.yml
git add .github/workflows/ci.yml
git commit -m "ci: enable the GitHub Actions workflow"
git push
```

After that, every push runs the five jobs. Until it is enabled, the T1 CI tasks
remain `[-]` on the taskboard: the workflow is written and YAML-validated, but
no green run exists to close them.

## What the jobs do

| Job | Runs on | Covers |
|---|---|---|
| `portable-tests` | ubuntu, macos, windows | `tests/run_core_tests.sh`, plus ASan + UBSan on the non-Windows legs |
| `windows-build` | windows | `Debug`/`Release` x `Win32`/`x64`; builds the solution and runs all seven test executables; archives the build log |
| `release-warnings-as-errors` | windows | Release x64 core build with warnings-as-errors, the suite, and a step that asserts the flags reached every core unit |
| `hygiene` | ubuntu | `git diff --check`, solution/project structure (including that no project writes another project's binary), every project compiles from its own source list, the legacy no-write guard, the `ecdiag` read-only guard, the core-bootstrap guard (the application actually starts the core), the dependency record's hashes and required fields, shell shebangs, Markdown links, the workflow's own portable steps, no tracked build artifacts, all text files valid UTF-8 |
| `static-analysis` | windows | MSVC `/analyze` on the core (enforced) and the application (reported until T1-05 closes) |

No job loads a kernel driver or writes to an EC register.
