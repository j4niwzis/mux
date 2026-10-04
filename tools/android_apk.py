#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Add generated DEX and the ARM64 native dependency closure to an aapt2 APK."""
import argparse
import hashlib
import re
import subprocess
import zipfile
from pathlib import Path

SYSTEM_LIBRARIES = {
    'libc.so', 'libm.so', 'libdl.so', 'liblog.so', 'libandroid.so',
    'libEGL.so', 'libGLESv1_CM.so', 'libGLESv2.so', 'libGLESv3.so',
    'libOpenSLES.so', 'libaaudio.so', 'libmediandk.so', 'libcamera2ndk.so',
    'libvulkan.so', 'libz.so', 'libnativewindow.so', 'libjnigraphics.so',
}
STAMP = (2001, 1, 1, 0, 0, 0)


def elf(path, readelf):
    text = subprocess.check_output([readelf, '-h', '-lW', '-d', str(path)], text=True)
    if not re.search(r'Machine:\s+AArch64\b', text) or not re.search(r'Type:\s+DYN\b', text):
        raise ValueError(f'{path}: expected an ARM64 shared library')
    loads = [line.split() for line in text.splitlines() if line.lstrip().startswith('LOAD ')]
    if not loads or any(int(line[-1], 16) < 16384 for line in loads):
        raise ValueError(f'{path}: load segments need 16 KB alignment')
    needed = re.findall(r'\(NEEDED\).*?\[(.*?)\]', text)
    soname = re.search(r'\(SONAME\).*?\[(.*?)\]', text)
    return soname.group(1) if soname else path.name, needed


def native_closure(roots, directories, readelf):
    candidates = {}
    for directory in directories:
        if directory.is_dir():
            for path in sorted(directory.rglob('*.so*')):
                if path.is_file():
                    candidates.setdefault(path.name, set()).add(path.resolve())
    explicit = {p.name: p.resolve() for p in roots}
    queue = list(roots)
    result = {}
    while queue:
        path = queue.pop().resolve()
        soname, needed = elf(path, readelf)
        if '/' in soname or not soname.endswith('.so'):
            raise ValueError(f'{path}: Android requires an unversioned .so SONAME, got {soname}')
        if soname in result:
            if hashlib.sha256(result[soname].read_bytes()).digest() != hashlib.sha256(path.read_bytes()).digest():
                raise ValueError(f'conflicting libraries named {soname}')
            continue
        result[soname] = path
        for name in needed:
            if name in SYSTEM_LIBRARIES or name in result:
                continue
            if name in explicit:
                queue.append(explicit[name])
                continue
            found = candidates.get(name, set())
            # Never quietly choose between different copies of a dependency.
            if not found:
                raise ValueError(f'{path.name}: missing runtime dependency {name}; add --library-dir')
            hashes = {hashlib.sha256(p.read_bytes()).digest() for p in found}
            if len(hashes) != 1:
                raise ValueError(f'ambiguous runtime dependency {name}: {sorted(map(str, found))}')
            queue.append(sorted(found)[0])
    return result


def package(resources, dex, libraries, output):
    with zipfile.ZipFile(resources) as source:
        entries = {info.filename: source.read(info) for info in source.infolist() if not info.is_dir()}
    if 'AndroidManifest.xml' not in entries:
        raise ValueError('resource APK has no manifest')
    if any(name.startswith(('lib/', 'META-INF/')) or name == 'classes.dex' for name in entries):
        raise ValueError('resource APK already contains code or signatures')
    entries['classes.dex'] = dex.read_bytes()
    for name, path in libraries.items():
        entries[f'lib/arm64-v8a/{name}'] = path.read_bytes()
    # extractNativeLibs=true in the manifest: compression is deliberate. The
    # loader uses extracted, 16 KB-aligned ELF files, not unaligned ZIP offsets.
    with zipfile.ZipFile(output, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as apk:
        for name, data in sorted(entries.items()):
            info = zipfile.ZipInfo(name, STAMP)
            # Android 11+ requires the resource table to be uncompressed.
            # zipalign supplies its required four-byte alignment afterwards.
            info.compress_type = zipfile.ZIP_STORED if name == 'resources.arsc' else zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            apk.writestr(info, data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--resources', type=Path, required=True)
    parser.add_argument('--dex', type=Path, required=True)
    parser.add_argument('--library', type=Path, action='append', required=True)
    parser.add_argument('--library-dir', type=Path, action='append', default=[])
    parser.add_argument('--readelf', default='readelf')
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    try:
        libraries = native_closure(args.library, args.library_dir, args.readelf)
        package(args.resources, args.dex, libraries, args.out)
    except (ValueError, OSError, subprocess.CalledProcessError, zipfile.BadZipFile) as error:
        parser.exit(1, f'APK: {error}\n')
    print(f'{args.out}: {", ".join(sorted(libraries))}, generated classes.dex')


if __name__ == '__main__':
    main()
