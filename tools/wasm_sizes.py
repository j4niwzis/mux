#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
# tools/wasm_sizes.py MODULE.wasm MODULE.js.symbols
#
# What a wasm binary is made of: its sections, then each function's code
# size by its name (the symbol map emscripten writes beside a stripped
# binary), summed by namespace too -- the biggest first.
import collections, re, subprocess, sys

data = open(sys.argv[1], "rb").read()
names = {}
for line in open(sys.argv[2], encoding="utf-8", errors="replace"):
    index, _, name = line.rstrip("\n").partition(":")
    if index.isdigit():
        names[int(index)] = name

def leb(at):
    value = shift = 0
    while True:
        byte = data[at]
        at += 1
        value |= (byte & 0x7F) << shift
        shift += 7
        if byte < 0x80:
            return value, at

KINDS = {0: "custom", 1: "type", 2: "import", 3: "function", 7: "export", 9: "elem", 10: "code", 11: "data"}
sections = {}
at = 8
print("sections:")
while at < len(data):
    kind = data[at]
    size, start = leb(at + 1)
    sections[kind] = (start, size)
    print(f"  {KINDS.get(kind, kind):10} {size / 2**20:9.2f} MB")
    at = start + size

# Imported functions come first in the index space.
imported = 0
start, size = sections[2]
count, at = leb(start)
for _ in range(count):
    for _ in range(2):
        length, at = leb(at)
        at += length
    kind = data[at]
    at += 1
    if kind == 0:
        imported += 1
        _, at = leb(at)
    elif kind == 1:
        at += 1
        flags, at = leb(at)
        _, at = leb(at)
        if flags & 1:
            _, at = leb(at)
    elif kind == 2:
        flags, at = leb(at)
        _, at = leb(at)
        if flags & 1:
            _, at = leb(at)
    elif kind == 3:
        at += 2
    elif kind == 4:
        at += 1
        _, at = leb(at)

start, size = sections[10]
count, at = leb(start)
sizes = []
for index in range(count):
    body, after = leb(at)
    sizes.append((after - at + body, names.get(imported + index, f"function {imported + index}")))
    at = after + body

def demangled(all_names):
    try:
        out = subprocess.run(["c++filt"], input="\n".join(all_names), capture_output=True, text=True, check=True).stdout
        return out.split("\n")
    except Exception:
        return all_names

pretty = demangled([name for _, name in sizes])
by_space = collections.Counter()
for (bytes_, _), name in zip(sizes, pretty):
    space = re.match(r"^[^(<]*?(\w+::\w+)", name)
    by_space[space.group(1) if space else "(other)"] += bytes_

print(f"\nfunctions: {len(sizes)}, code {sum(b for b, _ in sizes) / 2**20:.2f} MB")
print("\nby namespace (first two levels):")
for space, bytes_ in by_space.most_common(40):
    print(f"  {bytes_ / 2**20:8.2f} MB  {space}")
print("\nbiggest functions:")
for (bytes_, _), name in sorted(zip(sizes, pretty), key=lambda pair: -pair[0][0])[:60]:
    print(f"  {bytes_ / 1024:9.1f} KB  {name[:200]}")
