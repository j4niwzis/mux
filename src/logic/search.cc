// SPDX-License-Identifier: AGPL-3.0-only
// mux.logic.search: what finding in a chat finds, from the messages given.
export module mux.logic.search;

import std;
import mux.core;
import mux.logic.text;

export namespace mux::logic {

// The ids of the messages with the words asked in their text or in the name
// of what they carry, in any case, newest first; none for nothing asked.
// Removed messages are not found.
[[nodiscard]] inline std::vector<std::string> found_in(const std::map<std::string, message>& all,
                                                       std::string_view query) {
  const std::string asked = folded(query);
  if (asked.empty())
    return {};
  std::vector<const message*> hits;
  for (const auto& [id, one] : all) {
    if (one.redacted)
      continue;
    const bool in_text = folded(one.body.plain).contains(asked);
    const bool in_name = one.attachment && folded(one.attachment->name).contains(asked);
    if (in_text || in_name)
      hits.push_back(&one);
  }
  std::ranges::sort(hits, std::ranges::greater{}, &message::at);
  std::vector<std::string> out;
  out.reserve(hits.size());
  for (const message* one : hits)
    out.push_back(one->id);
  return out;
}

// Which of those found is shown after a step, older or newer, from the one
// shown: the first where none is; held at either end.
[[nodiscard]] inline std::optional<std::size_t> stepped(std::optional<std::size_t> at, std::size_t count, bool older) {
  if (count == 0)
    return std::nullopt;
  if (!at)
    return 0;
  if (older)
    return std::min(*at + 1, count - 1);
  return *at > 0 ? *at - 1 : 0;
}

}  // namespace mux::logic
