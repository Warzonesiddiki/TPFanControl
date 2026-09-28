#!/usr/bin/env python3
"""Fail the build if the read-only diagnostic can reach anything that writes.

Why this exists
---------------

T5-09's whole claim is that ``ecdiag`` is read-only *by construction*: it is
handed a ``core::IRegisterReader``, an interface with a single read operation
and no write member, so there is nothing to call. That claim is a statement
about which types the tool's translation units can name, and it holds only as
long as the tool's include closure stays closed.

A closed closure is not self-maintaining. One line - ``#include
"ec_protocol.h"`` added to ``ec_access.h`` because a helper seemed convenient
there, or an include of the bridge in the front end - puts ``EcBus``,
``writeRegister`` and the whole control path back within reach, and nothing in
the build would say so. The engine would still only *call* reads. That is
exactly the shape of the defect this project keeps finding: code that is
individually correct and wrong in combination.

So this check is static, and it is narrow:

  Rule 1 - the include closure.
      Starting from the tool's sources and following local ``#include "..."``
      transitively, every header reached must be one of a short list, and each
      entry on that list has to say why it belongs. ``ec_protocol.h`` is not on
      it and cannot be, because that header declares ``EcBus``.

  Rule 2 - no write API, no bus, no bridge, in the tool's own code.
      The closure rule covers headers; a tool source could also name a write if
      a header it already has declared one. Comments are stripped first, so the
      reasoning in prose - "this is deliberately not an IIoBackend" - is not a
      violation.

  Rule 3 - the runtime evidence still exists.
      The type system and the two rules above constrain the code. They cannot
      show that a run touches nothing, which is the claim a hardware report
      needs. That is ``tests/ecdiag_tests.cpp``: a full run through a real
      ``EcBus`` over a fake EC, asserted against the bus's write trace and the
      fake's committed-write log. This rule fails if those assertions are gone,
      because a structural guarantee whose runtime evidence has been deleted is
      a guarantee nobody is checking.

What this deliberately does not do
----------------------------------

  It does not prove the remaining runtime test still asserts anything. Rule 3
  checks that the trace assertion is present, not that it is meaningful; a test
  can be hollowed out while keeping the call. That limitation is stated rather
  than papered over, and the way the guard was validated is the answer to it:
  the property was broken on purpose - a real register write through a real bus
  - and the suite failed. ``--selftest`` proves the rules in this file still
  match what they claim to, which is the failure mode that has actually bitten
  this repository.

  It does not forbid the name ``IRegisterReader`` in a comment or a message, and
  it does not care about style.

Usage:  python3 scripts/check_ecdiag_readonly.py [--root DIR]
        python3 scripts/check_ecdiag_readonly.py --selftest
Exit:   0 clean, 1 violations found, 2 the check itself could not run.
"""

import argparse
import re
import sys
from pathlib import Path

# --------------------------------------------------------------------------
# The tool's sources: the files the rules below apply to.
# --------------------------------------------------------------------------

TOOL_SOURCES = (
    "fancontrol/core/ecdiag.h",
    "fancontrol/core/ecdiag.cpp",
    "tools/ecdiag/simulated_ec.h",
    "tools/ecdiag/simulated_ec.cpp",
    "tools/ecdiag/main.cpp",
)

# The runtime evidence rule 3 requires.
EVIDENCE_FILE = "tests/ecdiag_tests.cpp"

# Tokens rule 3 requires to still be present in that file. Each one is a place
# where the claim is checked against something other than the tool's own
# intent: the bus's record of what it wrote, and the fake EC's record of what
# changed.
EVIDENCE_REQUIRED = (
    ("bus.writeTrace()", "the bus's own record of every port write it issued"),
    ("writtenRegisters()", "the fake EC's record of registers that actually changed"),
)

# --------------------------------------------------------------------------
# Rule 1: the closed include closure.
# --------------------------------------------------------------------------

# Every local header the tool's closure may reach, and why. Adding an entry is
# meant to be a decision someone made on purpose, in a diff a reviewer sees.
ALLOWED_HEADERS = {
    "io_backend.h": "IoResult, IoErrorCode and BackendState; the result types of a read",
    "ec_access.h": "EcBusConfig and the read-only seam (IRegisterReader, ReaderAudit)",
    "ecdiag.h": "the diagnostic engine the front end calls",
    "simulated_ec.h": "--simulate's invented register table",
}

LOCAL_INCLUDE = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.MULTILINE)

# Where a quoted include is searched for, after the including file's own
# directory. This has to match the compile command - the portable runner uses
# `-I fancontrol/core -I tools/ecdiag` - and the tool project's
# AdditionalIncludeDirectories. If it ever stops matching, a header that cannot
# be resolved is reported as a violation rather than skipped, so the check fails
# loudly instead of quietly covering less.
INCLUDE_DIRS = ("fancontrol/core", "tools/ecdiag")

# --------------------------------------------------------------------------
# Rule 2: no write API, no bus, no bridge, in the tool's code.
# --------------------------------------------------------------------------

def strip_comments(text):
    """Remove C and C++ comments.

    String literals are not removed. None of these patterns can appear inside a
    message this tool prints - the messages name bytes and offsets, not types -
    and removing literals would let a real call hide behind a quoted string in a
    way this check is not trying to model.
    """
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
    text = re.sub(r"//[^\n]*", "", text)
    return text


FORBIDDEN = (
    (
        "write-api",
        re.compile(
            r"\bwriteRegister\b"
            r"|\bwritePort\b"
            r"|\bwriteStatusPort\b"
            r"|\bwriteDataPort\b"
            r"|\bWriteByteToEC\b"
        ),
        "names an EC write operation. A diagnostic that can name one can call "
        "one; the point of handing it IRegisterReader is that it cannot.",
    ),
    (
        "write-enabling-call",
        re.compile(r"\bsetRegisterWritesAllowed\b|\bsetFanSelectorWritesAllowed\b"),
        "names the call that enables register writes. Nothing in a read-only "
        "diagnostic may reach it, not even to disable it.",
    ),
    (
        "bus-or-bridge",
        re.compile(r"\bEcBus\b|\bLegacyBackend\b|\bAppBridge\b|\bIIoBackend\b"),
        "names the bus, a bridge or a backend. Those types live above the "
        "read-only seam, and ecdiag is below it: it is given one read operation "
        "and nothing else. Naming one of them is the first half of using it.",
    ),
)


class Problem(object):
    def __init__(self, path, line, rule, detail):
        self.path = path
        self.line = line
        self.rule = rule
        self.detail = detail

    def describe(self, root):
        location = self.path if self.line is None else "%s:%d" % (self.path, self.line)
        return "  %s: %s\n      %s" % (location, self.rule, self.detail)


# --------------------------------------------------------------------------
# Implementations, written against an injected reader so --selftest can run
# them on synthetic input rather than only on the tree.
# --------------------------------------------------------------------------

def check_identifier_rules(path, text, problems):
    """Rule 2 over one file's text."""
    code = strip_comments(text)
    for number, line in enumerate(code.splitlines(), start=1):
        for rule, pattern, why in FORBIDDEN:
            if pattern.search(line):
                problems.append(Problem(path, number, rule, why))


def collect_local_includes(text):
    """The local (quoted) includes in one file, as written."""
    return LOCAL_INCLUDE.findall(text)


RESOLVED_EXISTING = set()


def check_include_closure(root_paths, reader, problems, exists=None):
    """Rule 1: walk the closure and refuse any header that is not allowed.

    `root_paths` are repository-relative paths. `reader(path)` returns the file's
    text or None. A header is resolved relative to the including file's
    directory, which is what a quoted include does.
    """
    seen = set()
    pending = list(root_paths)
    while pending:
        path = pending.pop()
        if path in seen:
            continue
        seen.add(path)
        text = reader(path)
        if text is None:
            problems.append(Problem(path, None, "unreadable",
                                    "the file could not be read, so its includes are unknown"))
            continue
        for include in collect_local_includes(text):
            if include not in ALLOWED_HEADERS:
                problems.append(
                    Problem(path, None, "include-not-allowed",
                            'includes "%s", which is not on the read-only list. Every '
                            "header in ecdiag's include closure must be listed in "
                            "ALLOWED_HEADERS with a reason. ec_protocol.h declares EcBus "
                            "and cannot be listed." % include))
                continue
            resolved = _resolve(path, include) if exists is None else _resolve_with(
                path, include, exists)
            if resolved is None:
                problems.append(
                    Problem(path, None, "include-unresolved",
                            'includes "%s", which could not be resolved on the include '
                            "path. A header this check cannot read is a header it cannot "
                            "check, so it is reported rather than skipped." % include))
                continue
            if resolved not in seen:
                pending.append(resolved)


def _resolve(including_path, include):
    """Resolve a quoted include the way the compiler does: the including file's
    directory first, then the include path."""
    candidates = [Path(including_path).parent / include]
    for directory in INCLUDE_DIRS:
        candidates.append(Path(directory) / include)
    for candidate in candidates:
        normalised = Path(str(candidate))
        if str(normalised) in RESOLVED_EXISTING:
            return str(normalised)
        if normalised.is_file():
            RESOLVED_EXISTING.add(str(normalised))
            return str(normalised)
    return None


def _resolve_with(including_path, include, exists):
    candidates = [Path(including_path).parent / include]
    for directory in INCLUDE_DIRS:
        candidates.append(Path(directory) / include)
    for candidate in candidates:
        if exists(str(candidate)):
            return str(candidate)
    return None


def check_evidence(text, path, problems):
    """Rule 3: the runtime trace assertions are still there."""
    for token, what in EVIDENCE_REQUIRED:
        if token not in text:
            problems.append(
                Problem(path, None, "evidence-missing",
                        "no longer contains %s (%s). The structural rules above constrain "
                        "the code; this test is what shows a real run touches nothing, and "
                        "without it the read-only claim is unverified." % (token, what)))


# --------------------------------------------------------------------------
# --selftest
# --------------------------------------------------------------------------

SELFTEST_FILES = {
    # The tool's own sources, with the includes the real files have.
    "fancontrol/core/ecdiag.h": '#include "ec_access.h"\n#include "io_backend.h"\n',
    "fancontrol/core/ecdiag.cpp": '#include "ecdiag.h"\n',
    "tools/ecdiag/simulated_ec.h": '#include "ec_access.h"\n',
    "tools/ecdiag/simulated_ec.cpp": '#include "simulated_ec.h"\n',
    "tools/ecdiag/main.cpp": '#include "simulated_ec.h"\n#include "ecdiag.h"\n',
    # And the rest of the closure, because the rule is about the closure.
    "fancontrol/core/ec_access.h": '#include "io_backend.h"\n',
    "fancontrol/core/io_backend.h": "#pragma once\n#include <cstdint>\n",
}


def selftest():
    """Prove each rule still fires, and still stays quiet when it should.

    Every checker in this repository has a self-test for the same reason: a
    guard whose patterns have stopped matching reports a clean tree and a green
    job, which is worse than having no guard at all, because the job log reads
    as protection. That happened here already - check_legacy_ec.py carried a
    rule that could not fire on any real line - so the rules are exercised
    rather than assumed.
    """
    failures = []
    cases = 0

    def run(label, expect_violations, files=None, evidence=None):
        nonlocal cases
        cases += 1
        files = SELFTEST_FILES if files is None else files
        problems = []
        for path in TOOL_SOURCES:
            text = files.get(path, "")
            check_identifier_rules(path, text, problems)
        check_include_closure([p for p in TOOL_SOURCES if p in files],
                              lambda p: files.get(p), problems,
                              exists=lambda p: p in files)
        if evidence is not None:
            check_evidence(evidence, EVIDENCE_FILE, problems)
        found = len(problems) > 0
        if found != expect_violations:
            failures.append("%s: expected %s, got %d problem(s): %s"
                            % (label, "violations" if expect_violations else "clean",
                               len(problems),
                               "; ".join(p.rule for p in problems)))
            return
        print("  ok    %-52s %s" % (label, "violations found" if found else "clean"))

    clean_evidence = "bus.writeTrace();\nbackend.writtenRegisters();\n"

    run("the current shape is clean", False, evidence=clean_evidence)

    # Rule 1
    files = dict(SELFTEST_FILES)
    files["fancontrol/core/ecdiag.h"] = '#include "ec_protocol.h"\n'
    run("the tool includes ec_protocol.h", True, files)

    files = dict(SELFTEST_FILES)
    files["fancontrol/core/ec_access.h"] = '#include "ec_protocol.h"\n'
    run("a header in the closure reaches the bus", True, files)

    files = dict(SELFTEST_FILES)
    files["fancontrol/core/ecdiag.h"] = '#include "controller.h"\n'
    run("the closure reaches the control path", True, files)

    # Rule 2
    files = dict(SELFTEST_FILES)
    files["fancontrol/core/ecdiag.cpp"] = '#include "ecdiag.h"\nvoid f(EcBus& b) { b.writeRegister(0x2F, 0x40); }\n'
    run("a write call in the engine", True, files)

    files = dict(SELFTEST_FILES)
    files["tools/ecdiag/main.cpp"] = (
        '#include "ecdiag.h"\n// The bus is what a backend hands to a reader.\n'
        "#include \"ec_access.h\"\n")
    run("a mention inside a comment is not a violation", False, files)

    files = dict(SELFTEST_FILES)
    files["fancontrol/core/ecdiag.cpp"] = '#include "nothing-lives-here.h"\n'
    run("an include that cannot be resolved", True, files)

    files = dict(SELFTEST_FILES)
    files["tools/ecdiag/main.cpp"] = '#include "ecdiag.h"\nvoid f() { b.setRegisterWritesAllowed(true); }\n'
    run("the write-enabling call", True, files)

    files = dict(SELFTEST_FILES)
    files["tools/ecdiag/simulated_ec.cpp"] = '#include "simulated_ec.h"\nclass X : public IIoBackend {};\n'
    run("the simulator grows a backend interface", True, files)

    # Rule 3
    run("the trace assertion was deleted",
        True, evidence="backend.writtenRegisters();\n")
    run("the committed-write assertion was deleted",
        True, evidence="bus.writeTrace();\n")
    run("the evidence file is missing entirely", True, evidence="")

    # A guard that reports the wrong thing is a guard people learn to argue with.
    files = dict(SELFTEST_FILES)
    files["fancontrol/core/ecdiag.cpp"] = '#include "ecdiag.h"\n// writePort appears only here\n'
    run("a commented-out writePort is not a violation", False, files)

    print("")
    if failures:
        for failure in failures:
            print("  FAIL  %s" % failure)
        print("check_ecdiag_readonly --selftest: %d of %d case(s) wrong"
              % (len(failures), cases))
        return 1
    print("check_ecdiag_readonly --selftest: all %d cases behave as specified" % cases)
    return 0


# --------------------------------------------------------------------------
# The real run
# --------------------------------------------------------------------------

def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Fail if the read-only diagnostic can reach anything that writes.")
    parser.add_argument("--selftest", action="store_true",
                        help="check this script's rules on synthetic input and exit")
    parser.add_argument("--root", default=None,
                        help="repository root (default: the parent of scripts/)")
    args = parser.parse_args(argv)

    if args.selftest:
        return selftest()

    root = Path(args.root) if args.root else Path(__file__).resolve().parent.parent
    if not root.is_dir():
        sys.stderr.write("check_ecdiag_readonly: %s is not a directory\n" % root)
        return 2

    problems = []
    present = []
    for relative in TOOL_SOURCES:
        path = root / relative
        if not path.is_file():
            sys.stderr.write("check_ecdiag_readonly: expected %s to exist\n" % path)
            return 2
        present.append(relative)
        try:
            text = path.read_text(encoding="utf-8-sig")
        except (OSError, UnicodeDecodeError) as exc:
            problems.append(Problem(relative, None, "unreadable", str(exc)))
            continue
        check_identifier_rules(relative, text, problems)

    def reader(relative):
        try:
            return (root / relative).read_text(encoding="utf-8-sig")
        except (OSError, UnicodeDecodeError):
            return None

    check_include_closure(present, reader, problems)

    evidence = root / EVIDENCE_FILE
    if not evidence.is_file():
        problems.append(Problem(EVIDENCE_FILE, None, "evidence-missing",
                                "the file does not exist, so the read-only claim has no "
                                "runtime evidence at all"))
    else:
        try:
            check_evidence(evidence.read_text(encoding="utf-8-sig"), EVIDENCE_FILE, problems)
        except (OSError, UnicodeDecodeError) as exc:
            problems.append(Problem(EVIDENCE_FILE, None, "unreadable", str(exc)))

    if problems:
        sys.stderr.write("check_ecdiag_readonly: %d problem(s) found\n\n" % len(problems))
        for problem in problems:
            sys.stderr.write(problem.describe(root) + "\n\n")
        sys.stderr.write(
            "ecdiag is read-only by construction: it is handed an interface with one\n"
            "read operation and no write member. That holds only while its include\n"
            "closure stays closed. See docs/ECDIAG.md and ADR-024.\n")
        return 1

    print("check_ecdiag_readonly: %d tool source(s) clean; the include closure holds only "
          "%s, no write API or bus is named, and the runtime trace assertions are present"
          % (len(present), ", ".join(sorted(ALLOWED_HEADERS))))
    return 0


if __name__ == "__main__":
    sys.exit(main())
