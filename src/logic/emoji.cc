// SPDX-License-Identifier: AGPL-3.0-only
// mux.logic.emoji: which emoji a picker shows -- a group's, its skin-tone
// variants left for their base, or those whose names have the words asked.
export module mux.logic.emoji;

import std;
import mux.logic.text;
export import mux.logic.emoji_list;

export namespace mux::logic {

// An emoji's text, as the window's text takes it.
[[nodiscard]] inline std::string emoji_text(const emoji_entry& one) {
  return std::string(one.glyph);
}

// A variant of another for a skin tone (the table says which) is shown
// through its base, not beside it, as tdesktop's panel does.

// A group's emoji, their skin-tone variants left out.
[[nodiscard]] inline std::vector<const emoji_entry*> emoji_of_group(std::size_t group) {
  std::vector<const emoji_entry*> out;
  if (group >= std::size(emoji_groups))
    return out;
  for (const emoji_entry& one : emoji_groups[group].entries)
    if (!one.toned)
      out.push_back(&one);
  return out;
}

// Those whose names have what is asked, in any case, in Unicode's order; at
// most `most` of them.
[[nodiscard]] inline std::vector<const emoji_entry*> emoji_found(std::string_view query, std::size_t most = 200) {
  std::vector<const emoji_entry*> out;
  const std::string asked = folded(query);
  if (asked.empty())
    return out;
  for (const emoji_group& group : emoji_groups)
    for (const emoji_entry& one : group.entries)
      if (!one.toned && folded(one.name).contains(asked)) {
        out.push_back(&one);
        if (out.size() == most)
          return out;
      }
  return out;
}

}  // namespace mux::logic
