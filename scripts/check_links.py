#!/usr/bin/env python3
"""Check that every relative link in the repository's Markdown files resolves.

Used by CI (task T1-10) and by the documentation audit in T0.

Scope and deliberate limitations:
  * Only Markdown files reachable from the repository are checked.
  * External http(s), mailto and protocol-relative links are reported as a
    count but never fetched, so the check stays hermetic and offline.
  * Pure in-document anchors ("#section") are resolved against the headings of
    the same file, using the GitHub slug rules, so renamed sections are caught.
  * Files are enumerated with `git ls-files` so untracked build output and
    scratch files are never treated as documentation.
"""

from __future__ import annotations

import os
import re
import subprocess
import sys
import unicodedata
from urllib.parse import unquote, urlsplit

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# [text](target) / [text](<target> "title") / ![img](target)
INLINE_LINK = re.compile(r"!?\[[^\]]*\]\(\s*(<[^>]*>|[^()\s]+)(?:\s+[\"'][^)]*[\"'])?\s*\)")
# [text][ref] and [ref] shortcut references
REF_LINK = re.compile(r"(?<!\!)\[[^\]]+\]\[([^\]]*)\]")
REF_DEF = re.compile(r"^\s*\[([^\]]+)\]:\s*(<[^>]*>|\S+)", re.MULTILINE)
FENCED_CODE = re.compile(r"```.*?```|~~~.*?~~~", re.DOTALL)
INLINE_CODE = re.compile(r"`[^`\n]*`")
HTML_COMMENT = re.compile(r"<!--.*?-->", re.DOTALL)
ATX_HEADING = re.compile(r"^#{1,6}\s+(.*?)\s*#*\s*$", re.MULTILINE)


def git_tracked_markdown() -> list[str]:
    try:
        out = subprocess.run(
            ["git", "-C", REPO_ROOT, "ls-files", "*.md"],
            check=True, capture_output=True, text=True,
        ).stdout
    except (OSError, subprocess.CalledProcessError):
        return []
    return [p for p in out.splitlines() if p.strip()]


def github_slug(heading: str) -> str:
    """Approximate GitHub's heading-anchor algorithm."""
    h = re.sub(r"`([^`]*)`", r"\1", heading)
    h = re.sub(r"\[([^\]]*)\]\([^)]*\)", r"\1", h)      # links -> text
    h = re.sub(r"[*_~]", "", h)
    h = unicodedata.normalize("NFKD", h)
    h = "".join(c for c in h if not unicodedata.combining(c))
    h = re.sub(r"[^\w\- ]", "", h, flags=re.UNICODE)      # drop punctuation
    return h.strip().lower().replace(" ", "-")


def strip_noise(text: str) -> str:
    return INLINE_CODE.sub(" ", FENCED_CODE.sub(" ", HTML_COMMENT.sub(" ", text)))


def anchors_of(md: str) -> set[str]:
    return {github_slug(m) for m in ATX_HEADING.findall(md)}


def check_file(rel_path: str) -> tuple[list[str], int]:
    abs_path = os.path.join(REPO_ROOT, rel_path)
    with open(abs_path, encoding="utf-8-sig") as fh:
        raw = fh.read()
    md = strip_noise(raw)
    own_anchors = anchors_of(md)

    # Collect [ref]: targets so [text][ref] can be resolved.
    ref_targets = {m.group(1).lower(): m.group(2) for m in REF_DEF.finditer(md)}

    problems: list[str] = []
    external = 0

    def resolve(target: str, lineno: int) -> None:
        nonlocal external
        target = target.strip()
        if target.startswith("<") and target.endswith(">"):
            target = target[1:-1].strip()
        if not target:
            return
        parts = urlsplit(target)
        if parts.scheme in ("http", "https", "mailto", "ftp") or target.startswith("//"):
            external += 1
            return
        if parts.scheme and parts.scheme not in ("file",):
            problems.append(f"{rel_path}:{lineno}: unsupported scheme in {target!r}")
            return

        path = unquote(parts.path)
        if not path:
            return                                   # pure "#anchor"
        if os.path.isabs(path):
            problems.append(f"{rel_path}:{lineno}: absolute filesystem path {target!r}")
            return

        resolved = os.path.normpath(os.path.join(os.path.dirname(abs_path), path))
        if not os.path.exists(resolved):
            problems.append(f"{rel_path}:{lineno}: broken link -> {target}")
            return

        if parts.fragment and resolved.lower().endswith(".md"):
            with open(resolved, encoding="utf-8-sig") as fh:
                target_anchors = anchors_of(fh.read())
            frag = unquote(parts.fragment).lower()
            if frag not in target_anchors:
                problems.append(
                    f"{rel_path}:{lineno}: anchor #{frag} not found in "
                    f"{os.path.relpath(resolved, REPO_ROOT)}"
                )

    for lineno, line in enumerate(md.splitlines(), 1):
        for m in INLINE_LINK.finditer(line):
            resolve(m.group(1), lineno)
        for m in REF_LINK.finditer(line):
            key = (m.group(1) or m.group(0)[1:-1]).lower()
            if key in ref_targets:
                resolve(ref_targets[key], lineno)

    return problems, external


def main() -> int:
    files = git_tracked_markdown()
    if not files:
        print("check_links: no tracked Markdown files found", file=sys.stderr)
        return 1

    all_problems: list[str] = []
    external = 0
    for rel in files:
        problems, ext = check_file(rel)
        all_problems.extend(problems)
        external += ext

    print(f"check_links: scanned {len(files)} Markdown file(s), "
          f"{external} external link(s) skipped (not fetched)")

    if all_problems:
        print(f"check_links: {len(all_problems)} problem(s):", file=sys.stderr)
        for p in all_problems:
            print(f"  {p}", file=sys.stderr)
        return 1

    print("check_links: all relative links and anchors resolve")
    return 0


if __name__ == "__main__":
    sys.exit(main())
