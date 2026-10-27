"""Provide ranges::enumerate until Emscripten ships libc++ 23's view."""
import argparse
from pathlib import Path
import shutil

parser = argparse.ArgumentParser()
parser.add_argument('--emscripten', type=Path, required=True)
parser.add_argument('--libcxx', type=Path, required=True)
args = parser.parse_args()
places = [(args.emscripten / 'system/lib/libcxx/include', args.emscripten / 'system/lib/libcxx/modules/std/ranges.inc'),
          (args.emscripten / 'cache/sysroot/include/c++/v1', args.emscripten / 'cache/sysroot/share/libc++/v1/std/ranges.inc')]
exports = '\n    using std::ranges::enumerate_view;\n    namespace views { using std::ranges::views::enumerate; }\n'
for include, module in places:
    if not (include / 'ranges').is_file():
        continue
    ranges = (include / 'ranges').read_text()
    if 'enumerate_view.h' not in ranges:
        shutil.copy2(args.libcxx / '__ranges/enumerate_view.h', include / '__ranges/enumerate_view.h')
        anchor = '#    include <__ranges/elements_view.h>\n'
        if anchor not in ranges:
            raise SystemExit(f'Unknown libc++ ranges header: {include}')
        (include / 'ranges').write_text(ranges.replace(anchor, anchor + '#    include <__ranges/enumerate_view.h>\n', 1))
    text = module.read_text()
    if 'views::enumerate' not in text:
        anchor = '      using std::ranges::views::chunk_by;\n    }\n'
        if anchor not in text:
            raise SystemExit(f'Unknown libc++ ranges module: {module}')
        module.write_text(text.replace(anchor, anchor + exports, 1))
