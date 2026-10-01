// SPDX-License-Identifier: AGPL-3.0-only
// mux.logic.emoji: which emoji a picker shows -- a group's, its skin-tone
// variants left for their base, or those whose names have the words asked.
// The emoji are alef's: Unicode's emoji-test.txt, read while alef compiles.
export module mux.logic.emoji;

import std;
import mux.bytes;
import mux.logic.text;
export import alef.emoji;

// The emoji's keywords, as Unicode's CLDR annotates them (emoji_keywords.cc).
extern "C++" {
extern const unsigned char mux_cldr_en[];
extern const decltype(sizeof 0) mux_cldr_en_size;
extern const unsigned char mux_cldr_ru[];
extern const decltype(sizeof 0) mux_cldr_ru_size;
}

namespace mux::logic {
// An emoji as the annotations key it: without its variation selector.
[[nodiscard]] inline std::string bare_emoji(std::string_view text) {
  std::string out;
  for (std::size_t at = 0; at < text.size();) {
    if (text.substr(at).starts_with("\xEF\xB8\x8F")) {
      at += 3;
      continue;
    }
    out += text[at++];
  }
  return out;
}
// Each emoji's keywords, folded, one after another: read once from the
// annotations -- each "<annotation cp=\"X\">a | b | c</annotation>"; the
// ones with a type (tts) are the names, already searched.
[[nodiscard]] inline const std::unordered_map<std::string, std::string>& emoji_keywords() {
  static const std::unordered_map<std::string, std::string> read = [] {
    std::unordered_map<std::string, std::string> out;
    // Copied once, a byte at a time, into text.
    const std::array<std::string, 2> texts{
        mux::bytes::text_of(std::span<const std::uint8_t>(mux_cldr_en, mux_cldr_en_size)),
        mux::bytes::text_of(std::span<const std::uint8_t>(mux_cldr_ru, mux_cldr_ru_size))};
    const std::array<std::string_view, 2> files{texts[0], texts[1]};
    static constexpr std::string_view kOpen = "<annotation cp=\"";
    for (const std::string_view xml : files)
      for (std::size_t at = xml.find(kOpen); at != std::string_view::npos; at = xml.find(kOpen, at)) {
        at += kOpen.size();
        const std::size_t quote = xml.find('"', at);
        const std::size_t close = quote == std::string_view::npos ? quote : xml.find('>', quote);
        const std::size_t end = close == std::string_view::npos ? close : xml.find("</annotation>", close);
        if (end == std::string_view::npos)
          break;
        if (!xml.substr(quote, close - quote).contains("type=")) {
          std::string& words = out[bare_emoji(xml.substr(at, quote - at))];
          words += " | ";
          words += folded(xml.substr(close + 1, end - close - 1));
        }
        at = end;
      }
    return out;
  }();
  return read;
}
}  // namespace mux::logic

export namespace mux::logic {

// An emoji's text, as the window's text takes it.
[[nodiscard]] inline std::string emoji_text(const alef::emoji& one) { return std::string(one.text); }

// A variant of another for a skin tone (the table says which) is shown
// through its base, not beside it, as tdesktop's panel does.

// The table's groups -- how many, each one's name and first emoji -- and
// everything below that reads it: not inline, made here once. alef builds the
// table while its interface compiles, and clang builds it again (5.5 s) in
// every unit that has code reading it -- every unit that made the picker.
[[nodiscard]] std::size_t emoji_group_count() { return alef::emoji_groups.size(); }
[[nodiscard]] std::string_view emoji_group_name(std::size_t group) { return alef::emoji_groups[group].name; }
[[nodiscard]] const alef::emoji& emoji_group_face(std::size_t group) { return alef::emoji_groups[group].all.front(); }

// A group's emoji, their skin-tone variants left out.
[[nodiscard]] std::vector<const alef::emoji*> emoji_of_group(std::size_t group) {
  std::vector<const alef::emoji*> out;
  if (group >= alef::emoji_groups.size())
    return out;
  for (const alef::emoji& one : alef::emoji_groups[group].all)
    if (!one.toned)
      out.push_back(&one);
  return out;
}

// An emoji's skin tones, as tdesktop's panel offers them: the five
// single-tone variants, light to dark -- in the table, the entries after it
// named "<its name>: <tone> skin tone". None for an emoji that takes none.
[[nodiscard]] std::vector<const alef::emoji*> tones_of(const alef::emoji& base) {
  static constexpr std::array<std::string_view, 5> kTones{"light", "medium-light", "medium", "medium-dark", "dark"};
  std::vector<const alef::emoji*> out;
  for (const alef::emoji_group& group : alef::emoji_groups)
    for (std::size_t i = 0; i < group.all.size(); ++i) {
      if (&group.all[i] != &base)
        continue;
      for (const std::string_view tone : kTones) {
        const std::string wanted = std::format("{}: {} skin tone", base.name, tone);
        for (std::size_t j = i + 1; j < group.all.size() && group.all[j].toned; ++j)
          if (group.all[j].name == wanted) {
            out.push_back(&group.all[j]);
            break;
          }
      }
      return out;
    }
  return out;
}

// Those whose names -- or keywords, English or Russian, as CLDR has them --
// have what is asked, in any case, in Unicode's order; at most `most` of
// them.
[[nodiscard]] std::vector<const alef::emoji*> emoji_found(std::string_view query, std::size_t most = 200) {
  std::vector<const alef::emoji*> out;
  const std::string asked = folded(query);
  if (asked.empty())
    return out;
  for (const alef::emoji_group& group : alef::emoji_groups)
    for (const alef::emoji& one : group.all)
      if (!one.toned && (folded(one.name).contains(asked) || [&] {
            const auto words = emoji_keywords().find(bare_emoji(one.text));
            return words != emoji_keywords().end() && words->second.contains(asked);
          }())) {
        out.push_back(&one);
        if (out.size() == most)
          return out;
      }
  return out;
}

}  // namespace mux::logic
