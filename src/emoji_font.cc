// SPDX-License-Identifier: AGPL-3.0-only
// The emoji face, in the binary: Google's Noto Color Emoji (SIL Open Font
// License, fonts/OFL-NotoColorEmoji.txt), fetched by the build at a pinned
// commit. A translation unit of its own, so its bytes are compiled once and
// are in no module's interface.
extern const unsigned char mux_noto_color_emoji[] = {
#embed <NotoColorEmoji-Regular.ttf>
};
extern const decltype(sizeof 0) mux_noto_color_emoji_size = sizeof mux_noto_color_emoji;
