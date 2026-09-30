// SPDX-License-Identifier: AGPL-3.0-only
// Telegram's chat pattern, in the binary: Telegram Desktop's
// art/background.tgv (GPL-3.0, combined here as AGPL-3.0's section 13
// allows), a gzipped SVG fetched by the build at a pinned commit. A
// translation unit of its own, so its bytes are compiled once and are in no
// module's interface.
extern const unsigned char mux_telegram_pattern[] = {
#embed <background.tgv>
};
extern const decltype(sizeof 0) mux_telegram_pattern_size = sizeof mux_telegram_pattern;
