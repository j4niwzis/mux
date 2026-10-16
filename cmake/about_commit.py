#!/usr/bin/env python3
# The commit and branch mux is built from, for its About page: written to
# its own file, only where they changed, so a new commit recompiles one unit
# (src/ui/about_build.cc) and re-runs nothing else.
import argparse
import os
import subprocess
from pathlib import Path


def git(source, *args):
    result = subprocess.run(["git", "-C", str(source), *args], capture_output=True, text=True)
    return result.stdout.strip() if result.returncode == 0 else ""


def literal(value):
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    commit = git(args.source, "rev-parse", "HEAD") or "Unknown"
    branch = git(args.source, "describe", "--tags", "--exact-match") or git(args.source, "symbolic-ref", "--short", "HEAD")
    # CI checkouts are detached: retain the ref that actually selected HEAD.
    branch = branch or os.environ.get("GITHUB_HEAD_REF") or os.environ.get("GITHUB_REF_NAME") or "Detached HEAD"
    text = ("// Generated at build time from the checkout (cmake/about_commit.py).\n"
            "namespace about_build {\n"
            "inline constexpr std::string_view commit = " + literal(commit) + ";\n"
            "inline constexpr std::string_view branch = " + literal(branch) + ";\n"
            "}\n")
    path = Path(args.output)
    if not path.is_file() or path.read_text() != text:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)


if __name__ == "__main__":
    main()
