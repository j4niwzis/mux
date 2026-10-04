#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Host-only packaging checks; no Android SDK or device needed."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zipfile

spec = importlib.util.spec_from_file_location('android_apk', Path(__file__).parents[1] / 'tools/android_apk.py')
apk = importlib.util.module_from_spec(spec)
spec.loader.exec_module(apk)


class Packaging(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.app = self.library('libmux.so', b'app')
        self.sdl = self.library('libSDL3.so', b'sdl')

    def library(self, name, data):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        return path

    def metadata(self, path, _):
        return path.name, {'libmux.so': ['libSDL3.so', 'libc.so'],
                           'libSDL3.so': ['libandroid.so', 'libc.so']}[path.name]

    def test_dependency_closure_omits_platform_libraries(self):
        with patch.object(apk, 'elf', side_effect=self.metadata):
            result = apk.native_closure([self.app], [self.root], 'readelf')
        self.assertEqual(result, {'libmux.so': self.app, 'libSDL3.so': self.sdl})

    def test_missing_dependency_fails_before_packaging(self):
        with patch.object(apk, 'elf', side_effect=self.metadata):
            with self.assertRaisesRegex(ValueError, 'missing runtime dependency libSDL3.so'):
                apk.native_closure([self.app], [], 'readelf')

    def test_conflicting_candidates_are_rejected(self):
        self.library('other/libSDL3.so', b'different sdl')
        with patch.object(apk, 'elf', side_effect=self.metadata):
            with self.assertRaisesRegex(ValueError, 'ambiguous runtime dependency'):
                apk.native_closure([self.app], [self.root], 'readelf')

    def test_identical_candidates_are_accepted(self):
        self.library('other/libSDL3.so', b'sdl')
        with patch.object(apk, 'elf', side_effect=self.metadata):
            self.assertEqual(len(apk.native_closure([self.app], [self.root], 'readelf')), 2)

    def test_desktop_soname_is_rejected(self):
        with patch.object(apk, 'elf', return_value=('libSDL3.so.0', [])):
            with self.assertRaisesRegex(ValueError, 'unversioned'):
                apk.native_closure([self.sdl], [], 'readelf')

    def test_elf_architecture_and_page_alignment(self):
        valid = ('Type: DYN (Shared object file)\nMachine: AArch64\n'
                 'LOAD 0x000000 0x000000 0x000000 0x001000 0x001000 R E 0x4000\n'
                 '0x1 (NEEDED) Shared library: [libc.so]\n'
                 '0xe (SONAME) Library soname: [libmux.so]\n')
        with patch.object(apk.subprocess, 'check_output', return_value=valid):
            self.assertEqual(apk.elf(self.app, 'readelf'), ('libmux.so', ['libc.so']))
        for text, expected in ((valid.replace('AArch64', 'X86-64'), 'ARM64'),
                               (valid.replace('0x4000', '0x1000'), '16 KB')):
            with self.subTest(expected=expected):
                with patch.object(apk.subprocess, 'check_output', return_value=text):
                    with self.assertRaisesRegex(ValueError, expected):
                        apk.elf(self.app, 'readelf')

    def test_reproducible_apk_and_uncompressed_resource_table(self):
        resources = self.root / 'resources.apk'
        with zipfile.ZipFile(resources, 'w') as archive:
            archive.writestr('AndroidManifest.xml', b'manifest')
            archive.writestr('resources.arsc', b'resources')
        dex = self.library('classes.dex', b'dex')
        first, second = self.root / 'one.apk', self.root / 'two.apk'
        apk.package(resources, dex, {'libmux.so': self.app, 'libSDL3.so': self.sdl}, first)
        apk.package(resources, dex, {'libSDL3.so': self.sdl, 'libmux.so': self.app}, second)
        self.assertEqual(first.read_bytes(), second.read_bytes())
        with zipfile.ZipFile(first) as archive:
            self.assertEqual(archive.getinfo('resources.arsc').compress_type, zipfile.ZIP_STORED)
            self.assertEqual(archive.read('lib/arm64-v8a/libmux.so'), b'app')
            self.assertEqual(archive.read('classes.dex'), b'dex')

    def test_resource_apk_must_not_already_contain_code(self):
        resources = self.root / 'resources.apk'
        with zipfile.ZipFile(resources, 'w') as archive:
            archive.writestr('AndroidManifest.xml', b'manifest')
            archive.writestr('classes.dex', b'old code')
        with self.assertRaisesRegex(ValueError, 'already contains code'):
            apk.package(resources, self.root / 'unused.dex', {}, self.root / 'bad.apk')


if __name__ == '__main__':
    unittest.main()
