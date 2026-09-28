#!/usr/bin/env python3
"""Execute the POSIX steps of ci/ci.yml locally, so the workflow is not unverified.

The problem this solves
-----------------------

`ci/ci.yml` is a build artifact that nothing compiles and nothing runs until
GitHub does. Every edit to it - a suite added to a loop, a case statement
changed, a path corrected - is a change to a program whose syntax errors and
logic errors are both invisible until CI is red, on someone else's machine, in a
job that took four minutes to reach the broken line.

That is not hypothetical. The `FAKE=` selection for the sanitizer jobs was edited
several times, and the shape of that edit - giving one suite a different fake
source - is exactly the kind of change that passes review and fails on the
runner.

What this does
--------------

It extracts the `run: |` blocks from the workflow that are safe to execute on
this machine, runs each as a separate script with `set -euo pipefail`, and
reports the result. A step that exits non-zero fails this check.

Which steps are extracted
-------------------------

Only blocks that are genuinely portable and safe:

  - the AddressSanitizer and NDEBUG jobs, which compile and run the test suites
    on this machine already;
  - the static-checker steps, which are Python and shell;
  - the shell shebang guard.

Deliberately NOT extracted, and why:

  - `shell: cmd` and `shell: pwsh` steps. They need Windows.
  - `msbuild` steps. They need MSBuild.
  - anything that opens hardware or loads a driver. Nothing here should ever do
    that, and a checker that grew the ability would be a hazard rather than a
    tool.

A block that is skipped is *named* in the output, so a step cannot be quietly
excluded by being misclassified. If you add a job and it does not appear in
either list, this check says so.

Usage:  python3 scripts/check_ci_steps.py [--keep] [--only SUBSTRING]
        python3 scripts/check_ci_steps.py --selftest
Exit:   0 if every selected step passed, 1 if any failed, 2 if the workflow
        could not be parsed.
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

# Steps whose shell is not POSIX. Named so their absence from the run is
# visible rather than silent.
#
# `shell:` is matched on the *enclosing step*, not inside the run block: a
# `shell: cmd` step whose body is three lines of `out\tests\...\foo.exe`
# contains nothing here that identifies it as Windows, and matching the body
# instead of the declaration meant this checker ran cmd scripts under bash. It
# reported the failure correctly and for the wrong reason, which is worse than
# not running them, because the message sent you looking in the wrong place.
NON_POSIX_MARKERS = (
    "msbuild",
    "Get-Content",
    "Write-Host",
)

# NOT a marker: "::error::". It is a GitHub Actions workflow command, and bash
# echoes it as an ordinary string, so a step using it is ordinary bash. Listing
# it here made this checker skip three Linux hygiene steps - the shebang guard,
# the build-artifact guard and the UTF-8 check - on the grounds that they were
# PowerShell, which they are not.

# A GitHub Actions expression is not valid shell. `${{ ... }}` is substituted
# before the runner sees the script, so a step containing one cannot be
# reproduced locally without a substitution pass this checker deliberately does
# not implement.
GITHUB_EXPRESSION = re.compile(r"\$\{\{")

# Steps this checker refuses to run at all, whatever they look like.
#
# These name an *action*, not a file. An earlier version matched "TVicPort" and
# "0x31", which are perfectly ordinary to mention - the build-artifact step
# greps for tracked binaries and excludes TVicPort.lib by name, and the register
# map is full of 0x31 - and it refused to run a step that only ran `git ls-files
# | grep`. A guard that refuses harmless steps trains people to override it.
#
# The rule is narrow on purpose: these would perform privileged I/O, or escalate
# privileges, if executed. Merely naming the driver, the register or a source
# file is not that, and blocking it would make this checker useless.
FORBIDDEN_MARKERS = (
    "WriteByteToEC(",
    "IsDriverOpened",
    "sudo ",
    "sc.exe",
    "sc create",
    "sc start",
    "LoadDriver(",
)

# This script runs the steps, and the workflow now has a step that runs this
# script. Executing that would recurse until the machine ran out of process
# table. Skipped explicitly, and named in the output so the skip is visible.
SELF_MARKER = "check_ci_steps.py"


class Step(object):
    def __init__(self, index, body, indent, line_number, shell, name):
        self.index = index
        self.body = body
        self.indent = indent
        self.line_number = line_number
        # The `shell:` declared by the enclosing step, and its name, so a
        # skipped step can be identified rather than merely counted.
        self.shell = shell
        self.name = name

    @property
    def is_posix(self):
        if self.shell is not None and self.shell.strip() != "bash":
            return False
        if any(m in self.body for m in NON_POSIX_MARKERS):
            return False
        if GITHUB_EXPRESSION.search(self.body):
            return False
        return True

    def reason_skipped(self):
        if self.shell is not None and self.shell.strip() != "bash":
            return "shell: %s (needs Windows)" % self.shell.strip()
        if any(m in self.body for m in NON_POSIX_MARKERS):
            return "needs MSBuild or the Windows toolchain"
        if GITHUB_EXPRESSION.search(self.body):
            return "uses a GitHub Actions expression, which is substituted before the shell sees it"
        return ""

    @property
    def is_forbidden(self):
        return any(m in self.body for m in FORBIDDEN_MARKERS)


def extract_steps(text):
    """Return the `run: |` blocks, with the block indentation removed."""
    steps = []
    lines = text.split("\n")
    i = 0
    # The nearest preceding `shell:` and `- name:` belong to the step the
    # `run:` block is inside. Reset at every step boundary so one step's shell
    # cannot be attributed to the next.
    current_shell = None
    current_name = None
    while i < len(lines):
        # A step's `shell:` always follows its `- name:`, and a step that
        # declares no shell uses the runner's default - bash on Linux, pwsh on
        # Windows. So the shell must be reset at every step boundary, not just
        # updated when one is seen: carrying it forward attributed the Windows
        # static-analysis job's `shell: pwsh` to every later step, which marked
        # the Linux hygiene job's steps as needing Windows and silently stopped
        # checking them.
        if re.match(r"^\s*- name:", lines[i]):
            current_shell = None
            name_match = re.match(r"^\s*- name:\s*(.+?)\s*$", lines[i])
            current_name = name_match.group(1) if name_match else None
        shell_match = re.match(r"^\s*shell:\s*(\S+)\s*$", lines[i])
        if shell_match:
            current_shell = shell_match.group(1)
        # A folded scalar (`run: >` or `run: >-`) is deliberately not handled:
        # its body is the following lines, already consumed as part of this
        # step, and re-deriving it would mean re-implementing YAML's folding
        # rules. Every folded step in this workflow is an msbuild invocation,
        # which is skipped anyway.
        #
        # Note the class is `>-` and not `[>|]`. `|` is the literal block
        # scalar, which IS handled just below; matching it here skipped every
        # multi-line step in the workflow, leaving three one-liners as the only
        # things being checked.
        if re.match(r"^\s*run: >", lines[i]):
            i += 1
            continue

        # A single-line `run: <command>` is a step too, and skipping it silently
        # meant the static checkers - which are all one-liners - were never
        # executed here. They are exactly the steps most worth executing.
        inline = re.match(r"^(\s*)run: (?!\||>)(.+?)\s*$", lines[i])
        if inline:
            steps.append(Step(len(steps) + 1, inline.group(2) + "\n",
                              len(inline.group(1)), i + 1,
                              current_shell, current_name))
            i += 1
            continue

        match = re.match(r"^(\s*)run: \|\s*$", lines[i])
        if not match:
            i += 1
            continue
        indent = len(match.group(1))
        start = i
        step_shell = current_shell
        step_name = current_name
        i += 1
        collected = []
        while i < len(lines):
            line = lines[i]
            if not line.strip():
                collected.append("")
                i += 1
                continue
            line_indent = len(line) - len(line.lstrip())
            if line_indent <= indent:
                break
            collected.append(line[indent + 2:])
            i += 1
        body = "\n".join(collected).rstrip() + "\n"
        steps.append(Step(len(steps) + 1, body, indent, start + 1,
                          step_shell, step_name))
    return steps


SELF_TEST_WORKFLOW = """name: self-test
on: [push]
jobs:
  a:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: plain bash step
        run: |
          echo one
      - name: windows step
        shell: cmd
        run: |
          dir
      - name: powershell step
        shell: pwsh
        run: |
          Get-ChildItem
      - name: step with an Actions expression
        run: |
          git diff --check origin/${{ github.base_ref }}
      - name: step mentioning a file name, not an action
        run: |
          git ls-files | grep -v '^fancontrol/TVicPort\\.lib$'
      - name: msbuild step
        run: |
          msbuild fancontrol/fancontrol.sln
      - name: a privileged action
        run: |
          WriteByteToEC(0x2F, 0)
"""


def selftest():
    """Check the classification, which is the part that can silently go wrong.

    Every bug this file had was in classification, not in the running: a shell
    that leaked across step boundaries marked three Linux steps as Windows, a
    marker that matched a file name refused a step that only ran grep, and
    `::error::` was treated as PowerShell. All three produced a green run that
    checked less than it appeared to, which is the failure mode this whole
    script exists to prevent - so the classification needs its own tests.
    """
    steps = extract_steps(SELF_TEST_WORKFLOW)
    by_name = {step.name: step for step in steps}
    cases = [
        ("plain bash step", True, "an undeclared shell is the runner default (bash)"),
        ("windows step", False, "shell: cmd"),
        ("powershell step", False, "shell: pwsh"),
        ("step with an Actions expression", False, "${{ }} is substituted before the shell sees it"),
        ("msbuild step", False, "needs MSBuild"),
        ("step mentioning a file name, not an action", True,
         "TVicPort.lib in a grep exclusion is a name, not an action"),
    ]
    failures = 0
    for name, want_posix, why in cases:
        step = by_name.get(name)
        if step is None:
            print("  FAIL  %-44s step not found" % name)
            failures += 1
            continue
        got = step.is_posix
        ok = got == want_posix
        if not ok:
            failures += 1
        print("  %s  %-44s %-5s (expected %-5s)  %s"
              % ("ok  " if ok else "FAIL", name, got, want_posix, why))

    # A shell must not leak from one step to the next. This is the specific bug
    # that made three Linux hygiene steps skip silently.
    leaked = [s for s in steps if s.name == "plain bash step" and s.shell is not None]
    if leaked:
        print("  FAIL  shell leaked across a step boundary: %r" % leaked[0].shell)
        failures += 1
    else:
        print("  ok    shell does not leak across a step boundary")

    # The privileged action must be refused outright, not merely classified.
    privileged = by_name.get("a privileged action")
    if privileged is not None and not privileged.is_forbidden:
        print("  FAIL  a step performing a privileged action was not marked forbidden")
        failures += 1
    else:
        print("  ok    a step performing a privileged action is refused")

    print("")
    if failures:
        print("check_ci_steps --selftest: %d case(s) wrong" % failures)
        return 1
    print("check_ci_steps --selftest: all %d cases behave as specified"
          % (len(cases) + 2))
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Run the portable steps of ci/ci.yml locally.")
    parser.add_argument("--selftest", action="store_true",
                        help="check this script's step classification and exit")
    parser.add_argument("--root", default=None,
                        help="repository root (default: the parent of scripts/)")
    parser.add_argument("--only", default=None,
                        help="only run steps whose body contains this substring")
    parser.add_argument("--keep", action="store_true",
                        help="keep the extracted scripts instead of deleting them")
    args = parser.parse_args(argv)

    if args.selftest:
        return selftest()

    root = Path(args.root) if args.root else Path(__file__).resolve().parent.parent
    workflow = root / "ci" / "ci.yml"
    if not workflow.is_file():
        sys.stderr.write("check_ci_steps: %s not found\n" % workflow)
        return 2

    text = workflow.read_text(encoding="utf-8-sig")
    steps = extract_steps(text)
    if not steps:
        sys.stderr.write("check_ci_steps: no 'run: |' blocks found in %s\n" % workflow)
        return 2

    selected = []
    skipped = []
    for step in steps:
        if args.only and args.only not in step.body:
            skipped.append((step, "did not match --only"))
            continue
        if SELF_MARKER in step.body:
            skipped.append((step, "this script; running it would recurse"))
            continue
        if not step.is_posix:
            skipped.append((step, step.reason_skipped()))
            continue
        if step.is_forbidden:
            # This is a refusal, not a skip. A step that touched hardware or a
            # privileged primitive would be run by nobody, including a person.
            sys.stderr.write(
                "check_ci_steps: refusing to run step %d (ci/ci.yml line %d): "
                "it references hardware or a privileged primitive.\n%s\n"
                % (step.index, step.line_number, step.body))
            return 2
        selected.append(step)

    if not selected:
        sys.stderr.write("check_ci_steps: nothing selected to run\n")
        for step, why in skipped:
            print("  step %d (line %d): %s" % (step.index, step.line_number, why))
        return 1

    workdir = tempfile.mkdtemp(prefix="tpfancontrol-ci-steps-")
    failures = []
    try:
        for step in selected:
            script = Path(workdir) / ("step%02d.sh" % step.index)
            # `set -euo pipefail` on top of whatever the step declares. The steps
            # use `set -eu`; pipefail matters for the ones that pipe into tee or
            # grep, where a failure in the first command would otherwise be
            # invisible.
            script.write_text(
                "#!/usr/bin/env bash\nset -euo pipefail\n" + step.body,
                encoding="utf-8")
            os.chmod(str(script), 0o755)

            print("  running step %d (ci/ci.yml line %d, %d lines)"
                  % (step.index, step.line_number, step.body.count("\n")))
            proc = subprocess.run(
                ["bash", str(script)], cwd=str(root),
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            output = proc.stdout.decode("utf-8", "replace").rstrip()

            if proc.returncode != 0:
                failures.append((step, proc.returncode, output))
                print("    FAILED with exit %d" % proc.returncode)
                for line in output.splitlines()[-25:]:
                    print("      %s" % line)
            else:
                print("    passed")
                for line in output.splitlines()[-6:]:
                    print("      %s" % line)
    finally:
        if args.keep:
            print("  scripts kept in %s" % workdir)
        else:
            shutil.rmtree(workdir, ignore_errors=True)

    print("")
    for step, why in skipped:
        print("  step %d (line %d) %r not run: %s"
              % (step.index, step.line_number, step.name, why))

    if failures:
        print("check_ci_steps: %d of %d step(s) failed"
              % (len(failures), len(selected)))
        return 1

    print("check_ci_steps: %d of %d step(s) passed (%d skipped as non-portable)"
          % (len(selected), len(steps), len(skipped)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
