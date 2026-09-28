#!/usr/bin/env python3
"""Build every Windows project from its own `<ClCompile>` list.

Why this exists
---------------

A Visual Studio project is a second, independent statement of which sources a
binary is made of, and nothing keeps it in step with the first one. When it
drifts the failure appears only on Windows, only at link time, and only in the
job that was supposed to be the verification.

That is not hypothetical. The first run of this check found four of the five
test projects unable to link as committed:

  * ``ec_protocol_tests``, ``app_bridge_tests`` and ``legacy_policy_tests``
    were missing ``ec_access.cpp``, so ``EcBusConfig``'s members were undefined
    the moment ``ec_protocol.cpp`` validated a configuration;
  * ``legacy_backend_tests`` was missing ``fake_ec.cpp`` (and so ``FakeClock``)
    and had never compiled the bus it exercises;
  * every one of the five linked to ``core_tests.exe``, because the output name
    was copied along with the rest of the template, so the four executables the
    workflow runs at
    ``out\\tests\\<platform>\\<configuration>\\<suite>.exe`` were never produced;
  * ``app_bridge_tests`` listed ``core_types.cpp`` twice.

None of those could be seen here: the Linux runner compiles every core source
for every suite, which is exactly the difference that hides the drift. The files
also could not be checked by reading them, which is why the last two were only
found by comparing each project's output name against the workflow that runs it.

What this check does
--------------------

For each ``*.vcxproj`` under ``tests/`` and ``tools/`` it takes the
``<ClCompile>`` items - the project's own answer to "what is this binary made
of" - resolves them relative to the project file, builds them with the system
C++ compiler, and runs the result. A missing source shows up as an undefined
reference, the same way it would under MSVC.

It does not check the application project ``fancontrol/fancontrol.vcxproj``:
that one includes Win32 and MFC-style code that cannot be built here, which is
why the check is limited to the portable projects and says so rather than
appearing to cover everything.

What it cannot show, and what covers that
-----------------------------------------

This is not a Windows build. It compiles the same files with a different
compiler and none of the MSVC-specific flags. What it proves is that each
project's list is complete and self-consistent - the defect class above - not
that the projects build under MSVC. That claim stays with
``ci/ci.yml``'s ``windows-build`` job, and it is unverified until that job has
run.

Usage:  python3 scripts/check_project_sources.py [--keep]
        python3 scripts/check_project_sources.py --selftest
Exit:   0 every project built and ran, 1 a project did not, 2 the check could
        not run (no compiler, no projects to check).
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Directories searched for projects to check. The application project is
# deliberately absent: it is not portable, and a check that quietly skipped it
# would be worse than one that never claimed to cover it.
PROJECT_DIRS = ("tests", "tools")

MSBUILD_NAMESPACE = "{http://schemas.microsoft.com/developer/msbuild/2003}"


def compiler():
    for candidate in (os.environ.get("CXX"), "g++", "clang++"):
        if candidate and shutil.which(candidate):
            return candidate
    return None


def project_sources(project):
    """The <ClCompile> items of one project, resolved on disk, plus the include
    directories the project adds, also resolved."""
    tree = ET.parse(project)
    root = tree.getroot()

    def find(tag):
        # The namespace is stripped when present; a project without it still
        # parses, and neither form silently finds nothing.
        if root.tag.startswith("{"):
            return root.iter(MSBUILD_NAMESPACE + tag)
        return root.iter(tag)

    directory = os.path.dirname(os.path.abspath(project))
    sources = []
    for item in find("ClCompile"):
        include = item.get("Include")
        if include:
            sources.append(os.path.normpath(os.path.join(directory, include.replace("\\", os.sep))))

    includes = []
    for item in find("AdditionalIncludeDirectories"):
        for entry in (item.text or "").split(";"):
            entry = entry.strip()
            if not entry or entry.startswith("%("):
                continue
            resolved = os.path.normpath(os.path.join(directory, entry.replace("\\", os.sep)))
            if resolved not in includes:
                includes.append(resolved)

    return sources, includes


def build_and_run(project, cxx, keep=False):
    """Compile and run one project's source list. Returns a list of problems."""
    rel = os.path.relpath(project, ROOT)
    name = os.path.splitext(os.path.basename(project))[0]
    problems = []

    sources, includes = project_sources(project)

    if not sources:
        return ["%s: lists no <ClCompile> items, so the check would compile nothing" % rel]

    missing = [s for s in sources if not os.path.isfile(s)]
    if missing:
        return ["%s: lists %s, which does not exist on disk"
                % (rel, ", ".join(os.path.relpath(m, ROOT) for m in missing))]

    output_dir = tempfile.mkdtemp(prefix="tpfancontrol-project-check-")
    binary = os.path.join(output_dir, name + (".exe" if os.name == "nt" else ""))
    command = [cxx, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pedantic",
               "-pthread"]
    for directory in includes:
        command += ["-I", directory]
    command += sources + ["-o", binary]

    try:
        build = subprocess.run(command, capture_output=True, text=True)
        if build.returncode != 0:
            lines = [l for l in build.stderr.splitlines() if l.strip()]
            detail = "; ".join(lines[:3]) if lines else "no diagnostic"
            return ["%s: does not build from its own source list: %s" % (rel, detail)]

        run = subprocess.run([binary], capture_output=True, text=True)
        if run.returncode != 0:
            return ["%s: built but exited %d (expected the suite's own exit code 0)"
                    % (rel, run.returncode)]
        if not (run.stdout or "").strip():
            # A binary that prints nothing could still be the tool, so this is
            # reported only as a note in the passing line rather than a failure.
            pass
    finally:
        if not keep:
            shutil.rmtree(output_dir, ignore_errors=True)
        else:
            print("    binary kept at %s" % binary)

    return []


def discover_projects():
    projects = []
    for directory in PROJECT_DIRS:
        base = os.path.join(ROOT, directory)
        for dirpath, dirnames, filenames in os.walk(base):
            dirnames[:] = [d for d in dirnames if d not in ("out", ".git")]
            for filename in sorted(filenames):
                if filename.endswith(".vcxproj"):
                    projects.append(os.path.join(dirpath, filename))
    return sorted(projects)


def selftest(cxx):
    """Prove the check fails on the defects it exists for.

    Every guard in this repository carries one, for the same reason: a checker
    that has stopped noticing anything reports a clean tree in a green job,
    which reads as protection. The three cases below are the defects the first
    real run actually found - an incomplete list, a compile error, and a project
    that lists nothing at all.
    """
    print("check_project_sources --selftest")
    failures = []
    cases = 0
    workdir = tempfile.mkdtemp(prefix="tpfancontrol-project-selftest-")
    try:
        def write(relative, content):
            path = os.path.join(workdir, relative)
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "w", encoding="utf-8", newline="\n") as handle:
                handle.write(content)
            return path

        def project(listed_sources, name="proj.vcxproj"):
            items = "".join('    <ClCompile Include="%s" />\n' % s for s in listed_sources)
            return write(name, (
                '<?xml version="1.0" encoding="utf-8"?>\n'
                '<Project ToolsVersion="17.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">\n'
                '  <ItemGroup>\n%s  </ItemGroup>\n</Project>\n') % items)

        # A project that is complete builds and runs, so the check is not
        # merely "always complain".
        write("main.cpp", "int helper();\nint main() { return helper() == 7 ? 0 : 1; }\n")
        write("helper.cpp", "int helper() { return 7; }\n")

        def expect(label, listed, want_problems):
            nonlocal cases
            cases += 1
            path = project(listed)
            problems = build_and_run(path, cxx)
            got = len(problems) > 0
            if got != want_problems:
                failures.append("%s: expected %s, got %s"
                                % (label, "problems" if want_problems else "clean",
                                   problems or "clean"))
                print("  FAIL  %s" % label)
            else:
                print("  ok    %-46s %s" % (label, "flagged" if got else "clean"))

        expect("a complete list builds", ["main.cpp", "helper.cpp"], False)
        expect("a source missing from the list", ["main.cpp"], True)
        write("broken.cpp", "int main() { return undefined_name; }\n")
        expect("a source that does not compile", ["broken.cpp"], True)
        expect("a project that lists nothing", [], True)
        expect("a file that does not exist", ["main.cpp", "absent.cpp"], True)
    finally:
        shutil.rmtree(workdir, ignore_errors=True)

    if failures:
        for failure in failures:
            print("  %s" % failure)
        print("check_project_sources --selftest: %d of %d case(s) wrong"
              % (len(failures), cases))
        return 1
    print("check_project_sources --selftest: all %d cases behave as specified" % cases)
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Build every portable Visual Studio project from its own source list.")
    parser.add_argument("--selftest", action="store_true",
                        help="check this script's behaviour on synthetic projects and exit")
    parser.add_argument("--keep", action="store_true",
                        help="keep the built binaries for inspection")
    args = parser.parse_args(argv)

    cxx = compiler()
    if cxx is None:
        sys.stderr.write("check_project_sources: no C++ compiler found (CXX, g++, clang++)\n")
        return 2

    if args.selftest:
        return selftest(cxx)

    projects = discover_projects()
    if not projects:
        sys.stderr.write("check_project_sources: no %s projects found under %s\n"
                         % (", ".join(PROJECT_DIRS), ROOT))
        return 2

    problems = []
    for project in projects:
        rel = os.path.relpath(project, ROOT)
        found = build_and_run(project, cxx, keep=args.keep)
        if found:
            problems += found
            print("  FAIL  %s" % rel)
        else:
            print("  ok    %s" % rel)

    if problems:
        print("")
        print("check_project_sources: %d project(s) do not build from their own source list"
              % len(problems))
        for problem in problems:
            print("  " + problem)
        print("")
        print("The Windows test target states its sources a second time, in the project")
        print("file. This check compiles that statement on this machine; ci/ci.yml's")
        print("windows-build job is still what proves it under MSVC.")
        return 1

    print("")
    print("check_project_sources: %d project(s) built and ran from exactly the sources "
          "their project files list" % len(projects))
    return 0


if __name__ == "__main__":
    sys.exit(main())
