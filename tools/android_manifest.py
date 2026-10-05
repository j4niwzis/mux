#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""The toolchain manifest for one Android ABI: android/toolchain-sources.json,
its target and its kernel asm headers those of android/abis.json, the API
level its own. Or one field of that ABI, for a workflow to use."""
import argparse
import json
import re
from pathlib import Path

ANDROID = Path(__file__).resolve().parent.parent / 'android'


def manifest_for(abi):
    base = json.loads((ANDROID / 'toolchain-sources.json').read_text())
    chosen = json.loads((ANDROID / 'abis.json').read_text())[abi]
    base['target'] = {**chosen['target'], 'api': base['target']['api']}
    asm = re.compile(r'^libc/kernel/uapi/asm-[^/]+/asm$')
    base['headers'] = [
        {**entry, 'find': f"libc/kernel/uapi/{chosen['kernel-asm']}/asm"} if asm.match(entry['find']) else entry
        for entry in base['headers']]
    return base


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--abi', required=True, choices=sorted(json.loads((ANDROID / 'abis.json').read_text())))
    parser.add_argument('--out', type=Path)
    parser.add_argument('--field', choices=['rust', 'sysroot-triple'])
    args = parser.parse_args()
    manifest = manifest_for(args.abi)
    if args.field == 'rust':
        print(json.loads((ANDROID / 'abis.json').read_text())[args.abi]['rust'])
    elif args.field == 'sysroot-triple':
        print(manifest['target'].get('sysroot-triple', manifest['target']['triple']))
    if args.out:
        args.out.write_text(json.dumps(manifest, indent=2) + '\n')


if __name__ == '__main__':
    main()
