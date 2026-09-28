#!/usr/bin/env python3
"""Keep docs/DEPENDENCIES.md true to the files it describes.

Why this exists
---------------

T5-11 asks for a dependency record: source, license, exact release, driver
architecture, signature chain, HVCI/Secure Boot behaviour, exposed device/API,
uninstall method, known vulnerabilities, redistribution. A record like that is
only useful while it is *about the files that are actually here*. It goes stale
in two ways, and both are silent:

  * the vendored import library is replaced, and the checksum in the record still
    describes the old one. Anyone quoting the record - a release note, a hardware
    report - is then quoting a file that is not in the tree;
  * a new binary artefact is committed (a DLL, a driver, an installer) and never
    enters the record at all. The project's policy is that no kernel component is
    redistributed without documented rights; a record that cannot notice a new
    artefact does not enforce that.

So the rules are mechanical:

  Rule 1 - every dependency section carries the ten fields.
      A dependency section is a ``##`` heading whose body contains the field
      table header ``| Field | Record |``. Each one must name all ten fields,
      because SECURITY.md §3 lists ten and a missing one is not an omission
      anyone notices by reading.

  Rule 2 - every recorded checksum matches the file on disk.
      Any table row whose second cell is a 64-hex string is treated as an
      artefact row: path, sha256, size. The file must exist, its digest and its
      size must both match.

  Rule 3 - no tracked binary artefact is unrecorded.
      Every ``git ls-files`` entry ending in .lib, .dll, .sys, .exe, .msi, .cab
      or .zip must appear in an artefact row, with a checksum.

``--selftest`` proves all three still fire, over fixtures that contain each
defect on purpose.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import re
import subprocess
import sys
import tempfile

RECORD_FILE = os.path.join("docs", "DEPENDENCIES.md")

FIELD_HEADER = "| Field | Record |"

# SECURITY.md §3, verbatim and in order.
FIELDS = (
    "Source or vendor",
    "License",
    "Exact release",
    "Driver architecture",
    "Signature chain",
    "HVCI / Secure Boot",
    "Exposed device or API",
    "Uninstall method",
    "Known vulnerabilities",
    "Redistribution",
)

ARTEFACT_SUFFIXES = (".lib", ".dll", ".sys", ".exe", ".msi", ".cab", ".zip")

SHA256_RE = re.compile(r"^[0-9a-f]{64}$")


class Problem:
    def __init__(self, where, message):
        self.where = where
        self.message = message

    def describe(self):
        return "%s: %s" % (self.where, self.message)


# --------------------------------------------------------------------------
# Parsing, written against injected text so --selftest can run without the tree
# --------------------------------------------------------------------------

def split_sections(text):
    """Return [(heading, body)] for every ``## `` heading in the document."""
    sections = []
    heading = "(before the first heading)"
    body = []
    for line in text.splitlines():
        if line.startswith("## "):
            sections.append((heading, "\n".join(body)))
            heading = line[3:].strip()
            body = []
        else:
            body.append(line)
    sections.append((heading, "\n".join(body)))
    return sections


def table_rows(body, header):
    """Yield the cell lists of the table introduced by ``header``."""
    rows = []
    inside = False
    for line in body.splitlines():
        stripped = line.strip()
        if stripped == header:
            inside = True
            continue
        if not inside:
            continue
        if not stripped.startswith("|"):
            inside = False
            continue
        cells = [cell.strip() for cell in stripped.strip("|").split("|")]
        if all(cell and set(cell) <= set("-: ") for cell in cells):
            continue  # separator row
        rows.append(cells)
    return rows


def artefact_rows(text):
    """Return (line number, path, sha256, size) for rows carrying a digest."""
    rows = []
    for number, line in enumerate(text.splitlines(), 1):
        stripped = line.strip()
        if not stripped.startswith("|"):
            continue
        cells = [cell.strip().strip("`") for cell in stripped.strip("|").split("|")]
        if len(cells) >= 3 and SHA256_RE.match(cells[1]):
            rows.append((number, cells[0], cells[1], cells[2]))
    return rows


# --------------------------------------------------------------------------
# Rules
# --------------------------------------------------------------------------

def check_fields(text, problems):
    sections = 0
    for heading, body in split_sections(text):
        if FIELD_HEADER not in body:
            continue
        sections += 1
        labels = {row[0].strip("`") for row in table_rows(body, FIELD_HEADER) if row}
        missing = [field for field in FIELDS if field not in labels]
        if missing:
            problems.append(Problem(
                heading,
                "does not record %d of the ten required field(s): %s"
                % (len(missing), ", ".join(missing))))
    if sections == 0:
        problems.append(Problem(
            RECORD_FILE,
            "has no dependency field table (a '## ' section containing %r); "
            "SECURITY.md §3 requires ten fields per dependency" % FIELD_HEADER))
    return sections


def check_artefacts(text, root, problems):
    rows = artefact_rows(text)
    if not rows:
        problems.append(Problem(
            RECORD_FILE,
            "records no artefact checksum. The vendored library has to be pinned "
            "by hash, or the record describes a file that may not be the one here."))
    for number, path, recorded, size in rows:
        where = "%s:%d" % (RECORD_FILE, number)
        full = os.path.join(root, path)
        if not os.path.isfile(full):
            problems.append(Problem(where, "records %s, which does not exist" % path))
            continue
        with open(full, "rb") as handle:
            digest = hashlib.sha256(handle.read()).hexdigest()
        if digest != recorded:
            problems.append(Problem(
                where,
                "records sha256 %s for %s, but the file on disk is %s"
                % (recorded, path, digest)))
        actual_size = os.path.getsize(full)
        if not size.isdigit() or int(size) != actual_size:
            problems.append(Problem(
                where,
                "records size %s for %s, but the file on disk is %d bytes"
                % (size, path, actual_size)))
    return rows


def check_coverage(text, tracked, problems):
    recorded = {row[1] for row in artefact_rows(text)}
    missing = [path for path in tracked if path not in recorded]
    for path in missing:
        problems.append(Problem(
            RECORD_FILE,
            "%s is tracked in the repository but has no checksum row. A binary "
            "artefact that is not in the record has no documented licence, "
            "architecture or redistribution basis." % path))
    return missing


def tracked_artefacts(root):
    try:
        result = subprocess.run(["git", "ls-files", "-z"], cwd=root,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    except OSError as exc:
        raise RuntimeError("could not run git: %s" % exc)
    if result.returncode != 0:
        raise RuntimeError("git ls-files failed: %s"
                           % result.stderr.decode("utf-8", "replace").strip())
    paths = [p for p in result.stdout.decode("utf-8", "replace").split("\0") if p]
    return sorted(p for p in paths if p.lower().endswith(ARTEFACT_SUFFIXES))


def check_record(text, root, tracked):
    problems = []
    check_fields(text, problems)
    check_artefacts(text, root, problems)
    check_coverage(text, tracked, problems)
    return problems


# --------------------------------------------------------------------------
# --selftest
# --------------------------------------------------------------------------

def _fixture_record(path, sha, size, drop_field=None, drop_table=False,
                    tracked_note="fixture"):
    fields = [f for f in FIELDS if f != drop_field]
    field_rows = "\n".join("| %s | fixture value |" % field for field in fields)
    table = ""
    if not drop_table:
        table = ("\n### Artefacts\n\n"
                 "| Path | SHA-256 | Size | What it is |\n"
                 "|---|---|---|---|\n"
                 "| %s | `%s` | %s | fixture |\n" % (path, sha, size))
    return ("# Dependency record\n\n"
            "Fixture for %s.\n\n"
            "## 1. Fixture dependency\n\n"
            "### The ten fields\n\n"
            "| Field | Record |\n|---|---|\n%s\n%s" % (tracked_note, field_rows, table))


def selftest():
    failures = []
    cases = 0

    def run(label, expect_problems, record_builder, tracked=("vendor/thing.lib",),
            create_file=True):
        nonlocal cases
        cases += 1
        with tempfile.TemporaryDirectory() as tmp:
            path = "vendor/thing.lib"
            payload = b"fixture payload\n"
            digest = hashlib.sha256(payload).hexdigest()
            size = len(payload)
            if create_file:
                full = os.path.join(tmp, path)
                os.makedirs(os.path.dirname(full), exist_ok=True)
                with open(full, "wb") as handle:
                    handle.write(payload)
            text = record_builder(path, digest, size)
            problems = check_record(text, tmp, list(tracked))
            if bool(problems) != expect_problems:
                failures.append("%s: expected %s, got %d problem(s): %s"
                                % (label, "problems" if expect_problems else "clean",
                                   len(problems),
                                   "; ".join(p.describe() for p in problems)))
                return
            print("  ok    %-52s %s" % (label, "problem found" if problems else "clean"))

    run("an accurate record is clean", False,
        lambda p, d, s: _fixture_record(p, d, s))

    run("a replaced artefact is caught", True,
        lambda p, d, s: _fixture_record(p, "0" * 64, s))

    run("a wrong size is caught", True,
        lambda p, d, s: _fixture_record(p, d, s + 1))

    run("a missing field is caught", True,
        lambda p, d, s: _fixture_record(p, d, s, drop_field="Redistribution"))

    run("an unrecorded tracked binary is caught", True,
        lambda p, d, s: _fixture_record(p, d, s, drop_table=True))

    run("a recorded file that is not on disk is caught", True,
        lambda p, d, s: _fixture_record(p, d, s), create_file=False)

    run("a new artefact with no record row is caught", True,
        lambda p, d, s: _fixture_record(p, d, s),
        tracked=("vendor/thing.lib", "vendor/extra.dll"))

    run("a document with no field table at all is caught", True,
        lambda p, d, s: "# Dependency record\n\nProse only.\n")

    if failures:
        sys.stderr.write("check_dependencies --selftest: %d of %d case(s) wrong\n\n"
                         % (len(failures), cases))
        for failure in failures:
            sys.stderr.write("  %s\n" % failure)
        return 1
    print("check_dependencies --selftest: all %d cases behave as specified" % cases)
    return 0


# --------------------------------------------------------------------------

def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Keep the dependency record true to the files it describes.")
    parser.add_argument("--selftest", action="store_true",
                        help="prove the rules still fire, over fixtures")
    parser.add_argument("--record", default=RECORD_FILE,
                        help="the record to check (default: %s)" % RECORD_FILE)
    parser.add_argument("--root", default=".",
                        help="repository root the record's paths are relative to")
    args = parser.parse_args(argv)

    if args.selftest:
        return selftest()

    root = os.path.abspath(args.root)
    record = os.path.join(root, args.record)
    if not os.path.isfile(record):
        sys.stderr.write("check_dependencies: %s does not exist\n" % args.record)
        return 2

    with open(record, encoding="utf-8-sig") as handle:
        text = handle.read()

    try:
        tracked = tracked_artefacts(root)
    except RuntimeError as exc:
        sys.stderr.write("check_dependencies: %s\n" % exc)
        return 2

    problems = check_record(text, root, tracked)

    if problems:
        sys.stderr.write("check_dependencies: %d problem(s) found\n\n" % len(problems))
        for problem in problems:
            sys.stderr.write("  " + problem.describe() + "\n\n")
        sys.stderr.write(
            "The dependency record must describe the files that are here: hashes\n"
            "that match, ten fields per dependency, and a row for every tracked\n"
            "binary artefact. See docs/DEPENDENCIES.md and SECURITY.md section 3.\n")
        return 1

    print("check_dependencies: %d dependency section(s) with all ten fields, "
          "%d artefact(s) hashed and matched, %d tracked binary artefact(s) all recorded"
          % (len([1 for _, body in split_sections(text) if FIELD_HEADER in body]),
             len(artefact_rows(text)), len(tracked)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
