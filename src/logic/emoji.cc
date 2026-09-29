// SPDX-License-Identifier: AGPL-3.0-only
// mux.logic.emoji: which emoji a picker shows -- a group's, its skin-tone
// variants left for their base, or those whose names have the words asked.
// The emoji are alef's: Unicode's emoji-test.txt, read while alef compiles.
export module mux.logic.emoji;

import std;
import mux.logic.text;
export import alef.emoji;

export namespace mux::logic {

// An emoji's text, as the window's text takes it.
[[nodiscard]] inline std::string emoji_text(const alef::emoji& one) { return std::string(one.text); }

// A variant of another for a skin tone (the table says which) is shown
// through its base, not beside it, as tdesktop's panel does.

// A group's emoji, their skin-tone variants left out.
[[nodiscard]] inline std::vector<const alef::emoji*> emoji_of_group(std::size_t group) {
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
[[nodiscard]] inline std::vector<const alef::emoji*> tones_of(const alef::emoji& base) {
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

// Those whose names have what is asked, in any case, in Unicode's order; at
// most `most` of them.
[[nodiscard]] inline std::vector<const alef::emoji*> emoji_found(std::string_view query, std::size_t most = 200) {
  std::vector<const alef::emoji*> out;
  const std::string asked = folded(query);
  if (asked.empty())
    return out;
  for (const alef::emoji_group& group : alef::emoji_groups)
    for (const alef::emoji& one : group.all)
      if (!one.toned && folded(one.name).contains(asked)) {
        out.push_back(&one);
        if (out.size() == most)
          return out;
      }
  return out;
}

}  // namespace mux::logic
