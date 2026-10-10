"""About metadata regressions; no compiler, provider or Cargo invocation."""
import importlib.util
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest

spec = importlib.util.spec_from_file_location("mux_about", Path(__file__).parents[1] / "cmake/about.py")
about = importlib.util.module_from_spec(spec)
spec.loader.exec_module(about)


class AboutMetadata(unittest.TestCase):
    def test_tool_path_falls_back_to_source_project_version(self):
        with tempfile.TemporaryDirectory() as directory:
            Path(directory, "CMakeLists.txt").write_text(
                "cmake_minimum_required(VERSION 4.1)\nproject(skiff-widgets VERSION 0.1 LANGUAGES CXX)\n")
            self.assertEqual(about.library_version({"source": directory, "version": "/usr/bin/gn", "revision": "abcdef"}), "0.1")

    def test_installed_version_takes_precedence_over_cached_source(self):
        with tempfile.TemporaryDirectory() as directory:
            Path(directory, "CMakeLists.txt").write_text("project(example VERSION 0.1)")
            self.assertEqual(about.library_version({"source": directory, "version": "2.0.1"}), "2.0.1")

    def test_invalid_version_without_source_uses_revision(self):
        self.assertEqual(about.library_version({"source": "", "version": "/usr/bin/gn", "revision": "abcdef"}), "abcdef")
        self.assertEqual(about.library_version({"source": "", "version": "/usr/bin/gn"}), "Not reported by installed package")

    def test_system_ffmpeg_gets_license_text_without_source(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source"
            source.mkdir()
            record = {"name": "ffmpeg", "source": "", "version": "7.1", "primary": True,
                      "dependencies": [], "LICENSE": "LGPL-2.1-or-later", "IMPORT": ""}
            input_file = root / "input.json"
            input_file.write_text(json.dumps([record]))
            output = root / "about.inc"
            about.generate(SimpleNamespace(input=input_file, output=output, source=source, version="0.1"))
            generated = output.read_text()
            for spdx, text in about.ffmpeg_licenses().items():
                self.assertIn(spdx, generated)
                self.assertIn(text, generated)
            self.assertIn("GNU GENERAL PUBLIC LICENSE", about.ffmpeg_licenses()["LGPL-3.0-or-later"])

    def test_ffmpeg_license_inventory_is_absent_without_ffmpeg(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            input_file = root / "input.json"
            input_file.write_text("[]")
            output = root / "about.inc"
            about.generate(SimpleNamespace(input=input_file, output=output, source=root, version="0.1"))
            generated = output.read_text()
            self.assertIn("0> ffmpeg_licenses", generated)
            self.assertNotIn("COPYING.GPL", generated)


if __name__ == "__main__":
    unittest.main()
