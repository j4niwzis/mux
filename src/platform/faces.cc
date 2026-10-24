// SPDX-License-Identifier: AGPL-3.0-only
// mux.platform.faces -- The faces mux ships in its binary, as the window
// (skiff.platform.fonts) takes a program's: Telegram Desktop's own text
// faces, and its emoji.
export module mux.platform.faces;

import std;

// The emoji, as tdesktop has its own rather than the system's: Google's Noto
// Color Emoji (SIL Open Font License, fonts/OFL-NotoColorEmoji.txt), fetched
// at a pinned commit by the build, so they are the same on every machine and
// never boxes. Its 25 MB are in a translation unit of their own
// (emoji_font.c: C, which embeds them cheaply), not in this module's
// interface, which every importer reads.
extern "C" {
extern const unsigned char mux_noto_color_emoji[];
extern const decltype(sizeof 0) mux_noto_color_emoji_size;
}

export namespace mux::platform {
// What the window draws text with first: Open Sans, regular and semibold, as
// Telegram Desktop draws its text (SIL Open Font License, fonts/OFL.txt) --
// two faces, bold drawn with the semibold, not one thickened -- and the emoji
// face before anything of the system's.
struct faces {
  static constexpr unsigned char regular[] = {
#embed "../../fonts/OpenSans-Regular.ttf"
  };
  static constexpr unsigned char semibold[] = {
#embed "../../fonts/OpenSans-SemiBold.ttf"
  };
  static inline const std::ranges::subrange<const unsigned char*> emoji{
      mux_noto_color_emoji, mux_noto_color_emoji + mux_noto_color_emoji_size};
};
}  // namespace mux::platform
