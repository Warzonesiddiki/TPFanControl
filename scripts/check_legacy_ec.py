#!/usr/bin/env python3
"""Fail the build if the legacy application writes a register it must not.

This exists because of T3-11, and because the thing it guards cannot be tested
by running anything.

The legacy ``FANCONTROL::SetFan`` wrote 0x31, the fan-selector register, four
times per attempt and up to five attempts, with no check of any kind. Removing
those four lines is easy. What is not easy is knowing they are gone and staying
that way: the function is Win32 dialog code, it does not compile on the machine
this repository is developed on, and no test in tests/ can execute it. So the
defect had been fixed once with nothing to stop it coming back.

This check is that something. It is a static analysis over the legacy sources
and it runs on every push, on every platform, with no hardware and no compiler.

What it forbids, and why each one:

  a write to 0x31 through any name
    The defect itself. 0x31 is the fan selector on some models and something
    else entirely on others, and this project has not measured which. Writing
    it on an unverified machine corrupts the embedded controller with no way to
    find out what was changed.

  a write whose value is the fan-level byte, outside the core
    The level belongs to the portable controller. A second place that can
    produce it is a second decision path, which is T3-05.

Deliberately absent: a rule about signed ``char``.

An earlier version of this file had one, and its own self-test showed it could
not fire on any real line: it required the token ``EcByte``, which appears
nowhere in the legacy sources. A guard that matches nothing is worse than no
guard, because it reads as protection in the job log while the tree is
unprotected. It was removed rather than loosened.

Signed-char misuse of an EC byte - storing 0x80 in a ``char`` - is real in the
legacy code, and it is caught by the compiler instead: /W4 /WX on the core, and
C4244/C4245 where a conversion may lose data. That is a better mechanism than a
grep, because it is the one that understands types. See docs/TESTING.md.

What it deliberately does not do:

  It does not try to understand the code. There is no dataflow analysis and no
  attempt to prove a write is reachable. It matches the small set of patterns
  that have actually caused harm, so that it stays useful instead of becoming a
  style checker people learn to route around.

  It does not forbid reading 0x31. Reading is how a register map is built.

  It does not forbid the *name*. ``TP_ECOFFSET_FAN_SWITCH`` is still defined,
  because the register map documents it and read-only code may refer to it.

Usage:  python3 scripts/check_legacy_ec.py [--root DIR]
Exit:   0 clean, 1 violations found, 2 the check itself could not run.
"""

import argparse
import re
import sys
from pathlib import Path

# Directories that hold the legacy application. Deliberately excludes
# fancontrol/core, which is the portable core: the core is allowed to name
# kRegisterFanSelector, because naming a register in order to refuse to write
# it is the point. Forbidding it there would make the guard unenforceable.
LEGACY_DIRS = ("fancontrol",)

SOURCE_SUFFIXES = (".cpp", ".c", ".cc", ".h", ".hpp")

# --------------------------------------------------------------------------
# Rules
# --------------------------------------------------------------------------
#
# Each rule is (identifier, compiled regex, human explanation). A match is a
# violation unless it is on an ignored line, so a rule can be documented or
# reasoned about in a comment without having to disable the whole check.

def _strip_line(line):
    """Return the line with // and /* */ comments removed.

    String literals are not handled, and do not need to be: none of these
    patterns can appear inside a message the application prints, and a false
    negative here costs one register write, while a false positive costs a
    suppression comment that teaches people the guard is noise.
    """
    line = re.sub(r"/\*.*?\*/", " ", line)
    line = re.sub(r"//.*$", "", line)
    return line


IGNORED = re.compile(
    r"check_legacy_ec\s*:\s*allow-write-fan-selector"   # explicit, greppable
)

RULES = [
    (
        "fan-selector-write",
        # \\b after each alternative is load-bearing. Without it, the pattern for
        # the selector would also match 0x310, and the pattern for the level
        # would match TP_ECOFFSET_FAN1 and TP_ECOFFSET_FAN_SWITCH as prefixes of
        # themselves. Both were real: a guard that reports the wrong rule for a
        # real violation is a guard people learn to argue with.
        re.compile(
            r"\bWrite(?:Byte|Word)ToEC\s*\(\s*"
            r"(?:TP_ECOFFSET_FAN_SWITCH\b"
            r"|0[xX]0*31\b"
            # The legacy code casts its EC offsets: (char)0x31. The 0x has to
            # be optional or the cast form slips past the rule that is meant to
            # catch exactly the way the legacy code spelled it.
            r"|\(char\)\s*0*[xX]?31\b)"
        ),
        "writes the fan-selector register 0x31. Its meaning is not established "
        "by any measurement this project has taken, so writing it can corrupt "
        "the embedded controller on an unverified machine. See T3-11 and "
        "docs/EC_REGISTER_MAP.md 10.",
    ),
    (
        "fan-level-write-outside-core",
        re.compile(
            r"\bWrite(?:Byte|Word)ToEC\s*\(\s*"
            r"(?:TP_ECOFFSET_FAN\b|0[xX]0*2[fF]\b)"
            r"(?!\s*\))"
        ),
        "writes the fan level from outside the portable core. The level is the "
        "controller's to decide; a second writer is a second decision path "
        "(T3-05). Writes must go through AppBridge::apply, which verifies "
        "readback and refuses anything the core did not authorise.",
    ),
]


def check_file(path, problems):
    """Check one file, appending (path, line number, rule id, text) tuples."""
    try:
        raw = path.read_text(encoding="utf-8-sig")
    except (OSError, UnicodeDecodeError) as exc:
        problems.append((path, 0, "unreadable", str(exc)))
        return

    for number, line in enumerate(raw.splitlines(), start=1):
        code = _strip_line(line)
        if not code.strip():
            continue
        if IGNORED.search(line):
            continue
        for rule_id, pattern, _why in RULES:
            if pattern.search(code):
                problems.append((path, number, rule_id, line.strip()))


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Fail if the legacy application writes a forbidden register.")
    parser.add_argument("--root", default=None,
                        help="repository root (default: the parent of scripts/)")
    args = parser.parse_args(argv)

    root = Path(args.root) if args.root else Path(__file__).resolve().parent.parent
    if not root.is_dir():
        sys.stderr.write("check_legacy_ec: %s is not a directory\n" % root)
        return 2

    sources = []
    for directory in LEGACY_DIRS:
        base = root / directory
        if not base.is_dir():
            sys.stderr.write("check_legacy_ec: expected %s to exist\n" % base)
            return 2
        for path in sorted(base.rglob("*")):
            if path.suffix.lower() in SOURCE_SUFFIXES and path.is_file():
                # The core is portable C++ with its own rules; this check is
                # about the legacy application.
                if path.parent.name == "core":
                    continue
                sources.append(path)

    if not sources:
        sys.stderr.write("check_legacy_ec: found no legacy sources under %s\n" % root)
        return 2

    problems = []
    for path in sources:
        check_file(path, problems)

    why = {rule_id: why for rule_id, _pattern, why in RULES}

    if problems:
        sys.stderr.write("check_legacy_ec: %d violation(s) found\n\n" % len(problems))
        for path, number, rule_id, text in problems:
            rel = path.relative_to(root)
            sys.stderr.write("  %s:%d: %s\n" % (rel, number, rule_id))
            sys.stderr.write("      %s\n" % text)
            sys.stderr.write("      %s\n\n" % why.get(rule_id, ""))
        sys.stderr.write(
            "If a line is correct and must stay, mark it with a trailing\n"
            "'check_legacy_ec: allow-write-fan-selector' comment, but only\n"
            "after recording why in docs/DECISIONS.md.\n")
        return 1

    print("check_legacy_ec: %d legacy source(s) clean; no write to the "
          "fan-selector register, no fan-level write outside the core"
          % len(sources))
    return 0


if __name__ == "__main__":
    sys.exit(main())
