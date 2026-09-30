// SPDX-License-Identifier: AGPL-3.0-only
// The emoji's keywords, in the binary: Unicode's CLDR annotations, English
// and Russian (common/annotations/en.xml and ru.xml, under the Unicode
// licence), fetched by the build at a pinned commit. A translation unit of
// its own, so their bytes are compiled once and are in no module's
// interface.
extern const unsigned char mux_cldr_en[] = {
#embed <en.xml>
};
extern const decltype(sizeof 0) mux_cldr_en_size = sizeof mux_cldr_en;
extern const unsigned char mux_cldr_ru[] = {
#embed <ru.xml>
};
extern const decltype(sizeof 0) mux_cldr_ru_size = sizeof mux_cldr_ru;
