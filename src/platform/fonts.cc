// SPDX-License-Identifier: AGPL-3.0-only
// mux.platform.fonts -- The faces the window's text is drawn with: those
// shipped in the binary first, the system's behind them.
export module mux.platform.fonts;

import std;
import skia;
import skiff.paint;

// And emoji, as tdesktop has its own rather than the system's: Google's
// Noto Color Emoji (SIL Open Font License, fonts/OFL-NotoColorEmoji.txt),
// fetched at a pinned commit by the build, so they are the same on every
// machine and never boxes. Its 25 MB are in a translation unit of their
// own (emoji_font.cc), not in this module's interface, which every
// importer reads.
extern "C++" {
extern const unsigned char mux_noto_color_emoji[];
extern const decltype(sizeof 0) mux_noto_color_emoji_size;
}

namespace mux::platform::fonts::shipped {
// Telegram Desktop's own faces, in the binary: Open Sans, regular and
// semibold, as it draws its text with (SIL Open Font License, fonts/OFL.txt).
constexpr unsigned char open_sans_regular[] = {
#embed "../../fonts/OpenSans-Regular.ttf"
};
constexpr unsigned char open_sans_semibold[] = {
#embed "../../fonts/OpenSans-SemiBold.ttf"
};

}  // namespace mux::platform::fonts::shipped

export namespace mux::platform::fonts {

// Open Sans, shipped, as Telegram Desktop's text is: regular, and semibold
// for what is bold -- two faces, not one thickened. The system's fonts are
// behind them for what they do not cover -- CJK, emoji.
inline void load_fonts(const std::string& directory) {
  auto manager = skia::SkFontMgr_New_Custom_Directory(directory.c_str());
  if (!manager) {
    // No fonts found: the default face, rather than no text at all.
    static skia::SkFont font;
    skiff::paint::defaultFont() = &font;
    return;
  }
  skia::Sp<skia::SkTypeface> primary;
  for (const char* family : {"Inter", "Noto Sans", "DejaVu Sans", "Liberation Sans", "Cantarell"}) {
    primary = manager->matchFamilyStyle(family, skia::SkFontStyle());
    if (primary)
      break;
  }
  if (!primary && manager->countFamilies() > 0)
    primary = manager->createStyleSet(0)->createTypeface(0);
  const auto shipped_face = [&](const unsigned char* bytes, std::size_t size) {
    return manager->makeFromData(skia::SkData::MakeWithoutCopy(bytes, size));
  };
  auto regular = shipped_face(shipped::open_sans_regular, sizeof shipped::open_sans_regular);
  auto semibold = shipped_face(shipped::open_sans_semibold, sizeof shipped::open_sans_semibold);
  if (regular && semibold) {
    primary = regular;
    skiff::paint::fonts().setPrimary(std::move(regular), std::move(semibold));
  } else if (primary) {
    skiff::paint::fonts().setPrimary(primary);
  }
  // Emoji from the face shipped for them, before anything of the system's.
  if (auto emoji = shipped_face(mux_noto_color_emoji, mux_noto_color_emoji_size))
    skiff::paint::fonts().addFallback(std::move(emoji));
  // Where a character no face loaded here has is looked for: the system's.
  skiff::paint::fonts().setFontManager(manager);
  // Code in a monospace face of the system's, as Telegram draws it: the
  // first of the usual ones that is there.
  for (const char* family : {"DejaVu Sans Mono", "Noto Sans Mono", "Liberation Mono", "Ubuntu Mono", "JetBrains Mono",
                             "Fira Mono", "Source Code Pro", "Cascadia Mono", "Consolas", "Menlo", "Courier New"})
    if (auto face = manager->matchFamilyStyle(family, skia::SkFontStyle())) {
      skiff::paint::fonts().setMonospace(std::move(face));
      break;
    }
  // What every Text and widget draws with. Without it they draw nothing:
  // the window was its boxes and no words.
  static skia::SkFont font(primary);
  // Smoothed, and fitted to the pixels only lightly, as Qt draws on Linux:
  // full hinting is what made the text look heavier than Telegram's.
  font.setEdging(skia::SkFont::Edging::kAntiAlias);
  font.setHinting(skia::SkFontHinting::kSlight);
  font.setSubpixel(true);
  skiff::paint::defaultFont() = &font;
  for (const std::int32_t sample : {0x3042, 0xAC00, 0x4E00, 0x0627, 0x05D0, 0x0915, 0x1F600}) {
    if (auto face = manager->matchFamilyStyleCharacter(nullptr, skia::SkFontStyle(), nullptr, 0, sample))
      skiff::paint::fonts().addFallback(std::move(face));
  }
}

}  // namespace mux::platform::fonts
