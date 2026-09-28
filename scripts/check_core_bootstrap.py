#!/usr/bin/env python3
"""Fail the build if the portable core is not actually started by the application.

Why this exists
---------------

T5-04 found a defect that no test, and no compiler, could have reported:
``FANCONTROL::CoreInit()`` existed, was documented in three places, and **had no
caller**. ``CoreBridge`` was therefore always null in the shipped application, so
every request to move the fan ended at ``"FAILED!! (core not initialised)"``. The
T3 work that connected ``SetFan`` to ``AppBridge::apply`` was real code that never
ran, and the checklist's own "biggest single risk" line - *the portable core is
not connected to the application* - had quietly become true in a different way
than it described.

The reason it could not have been caught here: the application is Win32 dialog
code, it does not compile in this environment, and the call was *impossible* to
write where it was needed. ``CoreInit`` sat in the protected section of the class
while the startup path, ``approot.cpp``, is a free function. A reviewer reading
either file would find nothing wrong; the two files together are what was wrong.

So this check is about a property that spans files:

  Rule 1 - the bootstrap is reachable and used.
      ``StartCore()`` and ``CoreShutdown()`` are each called from a file other
      than the one that defines them. A method with no caller outside its own
      translation unit is how this defect happened.

  Rule 2 - the seam is public.
      ``StartCore`` and ``CoreStatus`` are declared after the ``public:`` label
      in the class. This is the specific thing that made the original call
      impossible: the startup path cannot call a protected member, so the only
      ways forward were to move the seam or to leave the core dead.

  Rule 3 - exactly one place builds the bridge.
      ``AppBridge`` is constructed once in the application sources. Two
      construction sites would mean two configurations, and the read-only one
      and the writable one would eventually be built in different places.

  Rule 4 - changing the core's startup is not possible without touching here.
      ``CoreInit`` is called from ``StartCore``. The two cannot drift into
      "someone moved the call and forgot the seam" without this file noticing.

What this check cannot do: it cannot run the application. It is a static
property check with a self-test, in the same spirit as ``check_legacy_ec.py``,
and it is honest about being a floor rather than a proof.

Usage:  python3 scripts/check_core_bootstrap.py [--root DIR]
        python3 scripts/check_core_bootstrap.py --selftest
Exit:   0 clean, 1 violations found, 2 the check itself could not run.
"""

import argparse
import os
import re
import sys

# The application's sources. `core/` is excluded: the portable core is allowed
# to have internal callers, and this check is about the Win32 side reaching it.
APP_DIR = "fancontrol"
CORE_SUBDIR = "core"

SOURCE_SUFFIXES = (".cpp", ".c", ".cc")
HEADER_SUFFIXES = (".h", ".hpp")

# The seam this check is about.
SEAM_CALLS = ("StartCore", "CoreShutdown")
SEAM_DECLARATIONS = ("StartCore", "CoreStatus")
BOOTSTRAP = "CoreInit"
BRIDGE_TYPE = "AppBridge"


def strip_comments(text):
    """Remove comments so prose about a call is not read as a call."""
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    return text


def calls_to(text, name):
    """Return the number of object-qualified calls to `name`.

    A definition (``FANCONTROL::StartCore()``) is not a call, and neither is a
    mention in prose - the comments are stripped first. What is counted is a
    call through an object, which is what the startup path performs.
    """
    pattern = r"(?:[A-Za-z_]\w*\s*(?:\.|->)\s*)" + re.escape(name) + r"\s*\("
    return len(re.findall(pattern, text))


def read_sources(root, suffixes):
    files = {}
    base = os.path.join(root, APP_DIR)
    if not os.path.isdir(base):
        return files
    for dirpath, dirnames, filenames in os.walk(base):
        if os.path.basename(dirpath) == CORE_SUBDIR:
            dirnames[:] = []
            continue
        dirnames[:] = [d for d in dirnames if d not in ("out", ".git")]
        for filename in sorted(filenames):
            if filename.lower().endswith(suffixes):
                path = os.path.join(dirpath, filename)
                with open(path, encoding="utf-8-sig", errors="replace") as handle:
                    files[path] = handle.read()
    return files


def check(files):
    """Return a list of problem strings for the given {path: text} map."""
    problems = []

    defines_startcore = [p for p, t in files.items()
                         if re.search(r"FANCONTROL::StartCore\s*\(", strip_comments(t))]
    if len(defines_startcore) != 1:
        problems.append(
            "expected exactly one definition of FANCONTROL::StartCore, found %d (%s)"
            % (len(defines_startcore),
               ", ".join(os.path.basename(p) for p in defines_startcore) or "none"))

    for name in SEAM_CALLS:
        # Rule 1: a call from somewhere other than the defining file.
        callers = []
        for path, text in files.items():
            clean = strip_comments(text)
            if calls_to(clean, name) and path not in (
                    defines_startcore if name == "StartCore" else []):
                if name == "CoreShutdown" and re.search(
                        r"FANCONTROL::CoreShutdown\s*\(", clean):
                    continue  # the definition itself
                callers.append(path)
        if not callers:
            problems.append(
                "%s() is never called from the application. A core method with no "
                "caller is how T5-04 happened: the bridge is never built, so every "
                "fan request answers \"core not initialised\" and the integration "
                "described in the checklist is dead code." % name)

    # Rule 2: the seam is reachable from the startup path.
    headers = read_sources_from_text(files, HEADER_SUFFIXES)
    for path, raw in headers.items():
        text = strip_comments(raw)
        if "class FANCONTROL" not in text:
            continue
        public_at = text.find("public:")
        if public_at < 0:
            problems.append("%s declares class FANCONTROL with no public section"
                            % os.path.basename(path))
            continue
        for name in SEAM_DECLARATIONS:
            declaration = re.search(r"\b%s\s*\(" % re.escape(name), text)
            if not declaration:
                problems.append("%s does not declare %s(), so the startup path has "
                                "no way to reach the core"
                                % (os.path.basename(path), name))
                continue
            if declaration.start() < public_at:
                problems.append(
                    "%s declares %s() before the public: label, so the startup path "
                    "(a free function in approot.cpp) cannot call it. This is the "
                    "exact shape of the T5-04 defect."
                    % (os.path.basename(path), name))

    # Rule 3: one construction site for the bridge.
    constructions = []
    for path, text in files.items():
        if not path.lower().endswith(SOURCE_SUFFIXES):
            continue
        clean = strip_comments(text)
        if re.search(r"\b" + BRIDGE_TYPE + r"\s*\(", clean) or \
           re.search(r"new\s+[\w:]*" + BRIDGE_TYPE, clean):
            constructions.append(path)
    if len(constructions) != 1:
        problems.append(
            "expected exactly one place in the application to construct %s, found "
            "%d (%s). Two construction sites mean two configurations, and the "
            "read-only one and the writable one will eventually be built in "
            "different files." % (BRIDGE_TYPE, len(constructions),
                                  ", ".join(os.path.basename(p) for p in constructions)
                                  or "none"))

    # Rule 4: StartCore actually starts the core.
    for path in defines_startcore:
        clean = strip_comments(files[path])
        body = clean[clean.index("FANCONTROL::StartCore"):]
        if BOOTSTRAP not in body:
            problems.append(
                "%s defines FANCONTROL::StartCore without calling %s(), so the seam "
                "does not reach the bootstrap the application needs."
                % (os.path.basename(path), BOOTSTRAP))

    return problems


def read_sources_from_text(files, suffixes):
    """The subset of a {path: text} map with one of `suffixes` (case-insensitive)."""
    return {p: t for p, t in files.items() if p.lower().endswith(suffixes)}


# --------------------------------------------------------------------------
# --selftest
# --------------------------------------------------------------------------

FIXTURE_HEADER_PUBLIC = """\
#pragma once
class FANCONTROL
{
\tprotected:
\t\tbool CoreInit();
\tpublic:
\t\tbool StartCore();
\t\tconst CoreStatus& CoreStatus() const noexcept;
};
"""

FIXTURE_HEADER_PROTECTED = FIXTURE_HEADER_PUBLIC.replace(
    "\tpublic:\n\t\tbool StartCore();\n\t\tconst CoreStatus& CoreStatus() const noexcept;\n",
    "\t\tbool StartCore();\n\t\tconst CoreStatus& CoreStatus() const noexcept;\n")

FIXTURE_IMPL = """\
#include "fancontrol.h"
bool FANCONTROL::CoreInit() {
    CoreShutdown();
    CoreBridge.reset(new tpfancontrol::core::AppBridge(*CoreBackend, CoreClock, config));
    return true;
}
bool FANCONTROL::StartCore() { return this->CoreInit(); }
void FANCONTROL::CoreShutdown() { }
"""

FIXTURE_APPROOT = """\
#include "fancontrol.h"
void start() {
    FANCONTROL fc(0);
    fc.StartCore();
    fc.CoreShutdown();
}
"""


def selftest():
    failures = []
    cases = 0

    def run(label, expect_problems, app_files, impl=FIXTURE_IMPL, header=FIXTURE_HEADER_PUBLIC):
        nonlocal cases
        cases += 1
        files = {
            os.path.join("fancontrol", "fancontrol.h"): header,
            os.path.join("fancontrol", "fanstuff.cpp"): impl,
        }
        files.update(app_files)
        problems = check(files)
        if bool(problems) != expect_problems:
            failures.append("%s: expected %s, got %d problem(s): %s"
                            % (label, "problems" if expect_problems else "clean",
                               len(problems), "; ".join(problems)))
            return
        print("  ok    %-52s %s" % (label, "problem found" if problems else "clean"))

    approot = os.path.join("fancontrol", "approot.cpp")

    run("the current shape is clean", False, {approot: FIXTURE_APPROOT})

    run("no caller for the seam is caught", True, {})

    run("a missing shutdown call is caught", True, {approot: """\
void start() {
    FANCONTROL fc(0);
    fc.StartCore();
}
"""})

    run("a protected seam is caught", True, {approot: FIXTURE_APPROOT},
        header=FIXTURE_HEADER_PROTECTED)

    run("a second bridge construction site is caught", True,
        {approot: FIXTURE_APPROOT +
                   "\nvoid other() { other.reset(new tpfancontrol::core::AppBridge(a, b, c)); }\n"})

    run("a StartCore that does not start the core is caught", True,
        {approot: FIXTURE_APPROOT},
        impl="""\
bool FANCONTROL::CoreInit() {
    CoreBridge.reset(new tpfancontrol::core::AppBridge(*CoreBackend, CoreClock, config));
    return true;
}
bool FANCONTROL::StartCore() { return true; }
void FANCONTROL::CoreShutdown() { }
""")

    run("a missing declaration is caught", True, {approot: FIXTURE_APPROOT},
        header="""\
class FANCONTROL
{
\tpublic:
\t\tbool StartCore();
};
""")

    if failures:
        sys.stderr.write("check_core_bootstrap --selftest: %d of %d case(s) wrong\n\n"
                         % (len(failures), cases))
        for failure in failures:
            sys.stderr.write("  %s\n" % failure)
        return 1
    print("check_core_bootstrap --selftest: all %d cases behave as specified" % cases)
    return 0


# --------------------------------------------------------------------------

def main(argv=None):
    parser = argparse.ArgumentParser(
        description="The application must actually start the portable core.")
    parser.add_argument("--selftest", action="store_true",
                        help="prove the rules still fire, over fixtures")
    parser.add_argument("--root", default=".",
                        help="repository root (default: .)")
    args = parser.parse_args(argv)

    if args.selftest:
        return selftest()

    root = os.path.abspath(args.root)
    if not os.path.isdir(os.path.join(root, APP_DIR)):
        sys.stderr.write("check_core_bootstrap: %s/%s does not exist\n"
                         % (root, APP_DIR))
        return 2

    files = read_sources(root, SOURCE_SUFFIXES + HEADER_SUFFIXES)
    if not files:
        sys.stderr.write("check_core_bootstrap: found no application sources under "
                         "%s/%s\n" % (root, APP_DIR))
        return 2

    problems = check(files)

    if problems:
        sys.stderr.write("check_core_bootstrap: %d problem(s) found\n\n"
                         % len(problems))
        for problem in problems:
            sys.stderr.write("  " + problem + "\n\n")
        sys.stderr.write(
            "T5-04. The portable core has to be started by the application, and the\n"
            "seam that starts it has to be reachable from the startup path. CoreInit\n"
            "existed for a whole task with no caller, in a section approot.cpp could\n"
            "not call, and nothing said so.\n")
        return 1

    sources = sum(1 for p in files if p.lower().endswith(SOURCE_SUFFIXES))
    print("check_core_bootstrap: %d application source(s) clean; the core is started "
          "from the startup path through a public seam, and the bridge has one "
          "construction site" % sources)
    return 0


if __name__ == "__main__":
    sys.exit(main())
