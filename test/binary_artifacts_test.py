#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Exercise the debug/stripped artifact path with a real ELF file."""
import importlib.util
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import zipfile


def tool(name):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).parents[1] / 'tools' / (name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


splitter, apk = tool('split_binary'), tool('android_apk')


@unittest.skipUnless(all(shutil.which(x) for x in ['cc', 'strip', 'readelf']), 'Needs ELF host tools')
class Artifacts(unittest.TestCase):
    def test_same_build_keeps_debug_data_out_of_stripped_binary_and_apk(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / 'main.c'
            source.write_text('int main(void) { return 0; }\n')
            binary = root / 'mux'
            subprocess.run(['cc', '-g', '-Wl,--build-id=sha1', str(source), '-o', str(binary)], check=True)
            original = binary.read_bytes()
            debug, stripped = splitter.split(binary, root / 'binaries', shutil.which('strip'))
            sections = lambda p: subprocess.check_output(['readelf', '-SW', str(p)], text=True)
            notes = lambda p: subprocess.check_output(['readelf', '-n', str(p)], text=True).split('Build ID:')[1].splitlines()[0]
            self.assertIn('.debug_info', sections(debug))
            self.assertNotIn('.debug_info', sections(stripped))
            self.assertEqual(notes(debug), notes(stripped))
            self.assertEqual(debug.read_bytes(), original)
            self.assertEqual(binary.read_bytes(), original)
            subprocess.run([str(debug)], check=True)
            subprocess.run([str(stripped)], check=True)
            resources = root / 'resources.apk'
            with zipfile.ZipFile(resources, 'w') as out:
                out.writestr('AndroidManifest.xml', b'manifest')
            dex = root / 'classes.dex'
            dex.write_bytes(b'dex fixture')
            for variant, strip_tool in [('debug', None), ('stripped', shutil.which('strip'))]:
                packaged = root / (variant + '.apk')
                apk.package(resources, dex, {'libmux.so': binary}, packaged, strip_tool=strip_tool)
                with zipfile.ZipFile(packaged) as archive:
                    native = root / (variant + '.so')
                    native.write_bytes(archive.read('lib/arm64-v8a/libmux.so'))
                self.assertEqual('.debug_info' in sections(native), variant == 'debug')
                self.assertEqual(notes(native), notes(debug))
            self.assertEqual(binary.read_bytes(), original)


if __name__ == '__main__':
    unittest.main()
