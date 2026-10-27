#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Keep the linked debug binary and strip a copy from the same compilation."""
import argparse
from pathlib import Path
import shutil
import subprocess


def split(source, output, strip_tool):
    debug = output / 'debug' / source.name
    stripped = output / 'stripped' / source.name
    debug.parent.mkdir(parents=True, exist_ok=True)
    stripped.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, debug)
    shutil.copy2(source, stripped)
    subprocess.run([strip_tool, '--strip-all', str(stripped)], check=True)
    return debug, stripped


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--strip', required=True)
    args = parser.parse_args()
    for path in split(args.binary, args.output, args.strip):
        print(path)


if __name__ == '__main__':
    main()
