#!/usr/bin/env python3
"""Structural checks for the Visual Studio solution and project files.

These are the mistakes that only a machine with MSBuild would catch, and that a
reviewer reading a diff is unlikely to notice:

  * a ``Project(`` entry with no matching ``EndProject``;
  * a project file referenced by the solution that does not exist;
  * a ``ClCompile``/``ClInclude`` item naming a file that is not on disk, which
    builds green as long as nobody happens to rebuild;
  * a project that is not mapped into all four solution configurations, so one
    configuration silently skips its tests;
  * a project whose ``OutputFile`` is not its own name, which is how all five
    test projects came to write ``core_tests.exe``: the four executables the
    workflow runs did not exist, and whichever project linked last overwrote the
    others, so a job could run one suite and report success for six.

Each of those produces a build that is less than it appears to be, which is why
this check exists and why it is run in CI.

Run:  python3 scripts/check_projects.py
Exit: 0 on success, 1 with a list of problems.
"""

import os
import re
import sys
import xml.etree.ElementTree as ET

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

EXPECTED_CONFIGURATIONS = [
    "Debug|Win32",
    "Release|Win32",
    "Debug|x64",
    "Release|x64",
]

PROJECT_TYPE_PREFIX = 'Project("{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}")'
PROJECT_ENTRY = re.compile(
    r'Project\("\{[^}]+\}"\)\s*=\s*"([^"]+)",\s*"([^"]+)",\s*"\{([^}]+)\}"')

problems = []


def fail(message):
    problems.append(message)


def check_solution(path):
    """Every Project( is terminated, every referenced project exists, and every
    project is mapped into all four solution configurations."""
    rel = os.path.relpath(path, ROOT)
    with open(path, "r", encoding="utf-8-sig") as handle:
        lines = handle.read().replace("\r\n", "\n").split("\n")

    guids = {}
    directory = os.path.dirname(path)
    index = 0
    while index < len(lines):
        line = lines[index]
        if line.startswith(PROJECT_TYPE_PREFIX):
            match = PROJECT_ENTRY.match(line)
            if not match:
                fail("%s:%d: cannot parse project entry: %s" % (rel, index + 1, line))
                index += 1
                continue
            name, project_path, guid = match.groups()

            following = lines[index + 1].strip() if index + 1 < len(lines) else "<EOF>"
            if following != "EndProject":
                fail("%s:%d: project %r is not terminated by EndProject (found %r)"
                     % (rel, index + 1, name, following))
                if following == "":
                    index += 1        # a stray blank line where EndProject belongs

            resolved = os.path.normpath(
                os.path.join(directory, project_path.replace("\\", os.sep)))
            if not os.path.isfile(resolved):
                fail("%s: project %r references %s, which does not exist"
                     % (rel, name, project_path))

            if guid in guids:
                fail("%s: project GUID %s used twice (%r and %r)"
                     % (rel, guid, guids[guid], name))
            guids[guid] = name
        elif line.strip() == "EndProject":
            previous = lines[index - 1] if index > 0 else ""
            if not previous.startswith(PROJECT_TYPE_PREFIX):
                fail("%s:%d: EndProject with no preceding Project(" % (rel, index + 1))
        index += 1

    if not guids:
        fail("%s: no projects found" % rel)

    body = "\n".join(lines)
    for guid, name in sorted(guids.items(), key=lambda item: item[1]):
        for configuration in EXPECTED_CONFIGURATIONS:
            # The key in the file is {guid}.<Solution>|<Platform>.<suffix>, so the
            # platform is part of the key rather than a separate field. Getting
            # this wrong makes every mapping look missing.
            for suffix in ("ActiveCfg", "Build.0"):
                # The GUID is brace-wrapped in the key, and the regex above
                # captures it without them.
                wanted = "{%s}.%s.%s" % (guid, configuration, suffix)
                if wanted not in body:
                    fail("%s: project %r has no %s mapping (expected %s)"
                         % (rel, name, configuration, wanted))
    return guids


def check_project(path):
    """Every source and header the project lists must exist on disk, and the
    project must expose exactly the four expected configurations."""
    rel = os.path.relpath(path, ROOT)
    try:
        tree = ET.parse(path)
    except ET.ParseError as exc:
        fail("%s: not well-formed XML: %s" % (rel, exc))
        return
    root = tree.getroot()

    # vcxproj files live in the MSBuild 2003 namespace, so every element name
    # carries a "{...}" prefix. Matching bare tag names silently finds nothing,
    # which would make this check pass on any file at all - including a project
    # that lists no sources.
    namespace = ""
    if root.tag.startswith("{"):
        namespace = root.tag[:root.tag.index("}") + 1]

    def find(tag):
        return root.iter(namespace + tag)

    directory = os.path.dirname(path)
    for tag in ("ClCompile", "ClInclude", "ResourceCompile", "None"):
        for item in find(tag):
            include = item.get("Include")
            if not include:
                continue
            resolved = os.path.normpath(
                os.path.join(directory, include.replace("\\", os.sep)))
            if not os.path.isfile(resolved):
                fail("%s: <%s Include=\"%s\"/> does not exist on disk"
                     % (rel, tag, include))

    configurations = set()
    for element in find("ProjectConfiguration"):
        include = element.get("Include")
        if include:
            configurations.add(include)
    if configurations != set(EXPECTED_CONFIGURATIONS):
        fail("%s: configurations are %s, expected %s"
             % (rel, sorted(configurations), sorted(EXPECTED_CONFIGURATIONS)))

    sources = [item.get("Include") for item in find("ClCompile") if item.get("Include")]
    if not sources:
        fail("%s: lists no ClCompile items, so it would build nothing" % rel)

    # The linked binary has to be recognisably this project's. Every test
    # project was cloned from core_tests.vcxproj and inherited its OutputFile,
    # so five projects linked to the same path: the executables the workflow
    # runs were never produced, and a build that ran one suite reported success
    # for all of them. The basename must contain the project's own name, which
    # is the property the workflow's out\tests\...\<suite>.exe paths assume.
    stem = os.path.splitext(os.path.basename(path))[0].lower()
    seen_outputs = set()
    for item in find("OutputFile"):
        value = (item.text or "").strip()
        if not value:
            continue
        # One <OutputFile> per configuration; report each distinct value once.
        if value in seen_outputs:
            continue
        seen_outputs.add(value)
        basename = value.replace("\\", "/").split("/")[-1]
        if stem not in basename.lower():
            fail("%s: OutputFile is %r, which is not recognisably this project's "
                 "(expected the name %r to appear in the file name). A project that "
                 "writes another project's binary overwrites it, and the executable "
                 "the workflow runs for this suite will not exist."
                 % (rel, value, stem))


def main():
    solution = os.path.join(ROOT, "fancontrol", "fancontrol.sln")
    if not os.path.isfile(solution):
        print("check_projects: %s not found" % solution)
        return 1

    guids = check_solution(solution)

    projects = 0
    for project_dir, dirnames, files in os.walk(ROOT):
        dirnames[:] = [d for d in dirnames if d not in (".git", "out")]
        for name in sorted(files):
            if name.endswith(".vcxproj"):
                check_project(os.path.join(project_dir, name))
                projects += 1

    if problems:
        print("check_projects: %d problem(s)" % len(problems))
        for problem in problems:
            print("  " + problem)
        return 1

    print("check_projects: %d solution project(s) well-formed and mapped into all %d "
          "configurations; %d project file(s) list only sources that exist"
          % (len(guids), len(EXPECTED_CONFIGURATIONS), projects))
    return 0


if __name__ == "__main__":
    sys.exit(main())
