// SPDX-License-Identifier: AGPL-3.0-only
// No emoji face in the binary: a page's, whose browser draws the words with
// its own faces (emoji_font.c is the window's). Empty, so the emoji face is
// none and nothing is loaded for it.
#include <stddef.h>

const unsigned char mux_noto_color_emoji[1] = {0};
const size_t mux_noto_color_emoji_size = 0;
