// SPDX-License-Identifier: AGPL-3.0-only
// The emoji face, in the binary: Google's Noto Color Emoji (SIL Open Font
// License, fonts/OFL-NotoColorEmoji.txt), fetched by the build at a pinned
// commit. A translation unit of its own, so its bytes are compiled once and
// are in no module's interface.
//
// C, not C++: clang keeps a C array's #embed as the bytes it read, and in
// C++ makes an expression of every one of them -- 25 MB took over 1.1 GB
// and was killed, where as C they take 0.1 s and 184 MB.
#include <stddef.h>

const unsigned char mux_noto_color_emoji[] = {
#embed <NotoColorEmoji-Regular.ttf>
};
const size_t mux_noto_color_emoji_size = sizeof mux_noto_color_emoji;
