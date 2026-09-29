// SPDX-License-Identifier: AGPL-3.0-only
// mux.logic.drafts: the drafts kept, as their file says them, and back.
export module mux.logic.drafts;

import std;
import knot;
import mux.core;
import mux.logic.text;

export namespace mux::logic {

using drafts_t = std::map<conversation_id, std::string>;

// A chat's draft set to what is in its field: blank, none. Whether it
// changed.
inline bool keep_draft(drafts_t& drafts, const conversation_id& in, const std::string& text) {
  const bool blank = std::ranges::all_of(text, [](unsigned char c) { return std::isspace(c) != 0; });
  const auto found = drafts.find(in);
  if (blank ? found == drafts.end() : (found != drafts.end() && found->second == text))
    return false;
  if (blank)
    drafts.erase(in);
  else
    drafts.insert_or_assign(in, text);
  return true;
}

// The file's text: an object of "<account>\n<chat>" to the draft.
[[nodiscard]] inline std::string drafts_text(const drafts_t& drafts) {
  knot::value::object all;
  for (const auto& [id, draft] : drafts)
    all.emplace(id.account.address + "\n" + id.id, knot::value(draft));
  return knot::to_json_string(knot::value(std::move(all)));
}
[[nodiscard]] inline drafts_t drafts_from(std::string_view text) {
  drafts_t out;
  auto parsed = knot::try_read<knot::value>(text);
  if (!parsed || !parsed->is<knot::value::object>())
    return out;
  for (const auto& [key, draft] : parsed->as<knot::value::object>()) {
    const auto cut = key.find('\n');
    if (cut == std::string::npos || !draft.is<std::string>())
      continue;
    out.insert_or_assign(conversation_id{account_of(key.substr(0, cut)), key.substr(cut + 1)}, draft.as<std::string>());
  }
  return out;
}

}  // namespace mux::logic
