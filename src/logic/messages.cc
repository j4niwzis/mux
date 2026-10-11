// SPDX-License-Identifier: AGPL-3.0-only
// mux.logic.messages: what is done to a message from its menu, decided from
// the message alone -- a reaction put or taken back, a reply's line.
export module mux.logic.messages;

import std;
import mux.core;

export namespace mux::logic {

[[nodiscard]] inline forward_source forward_source_of(const message& one) {
  return {one.id, one.edited ? std::optional(one.body) : std::nullopt};
}

// Selected messages in their displayed order. Keep each timeline's order
// when timestamps tie, including optimistic messages awaiting their echo.
// A quoted copy never replaces the version already kept in a timeline.
[[nodiscard]] inline std::vector<const message*> selected_messages(const conversation& chat, const auto& ids) {
  const auto chosen = [&](const auto& messages) {
    return std::views::transform(std::views::filter(messages, [&](const message& one) {
      return !one.redacted && std::ranges::find(ids, one.id) != std::ranges::end(ids);
    }), [](const message& one) { return &one; });
  };
  auto ordered = std::ranges::to<std::vector>(chosen(chat.timeline));
  ordered.append_range(chosen(std::views::join(std::views::values(chat.threads))));
  ordered.append_range(chosen(std::views::values(chat.quoted)));
  std::set<std::string_view> seen;
  std::erase_if(ordered, [&](const message* one) { return !seen.insert(one->id).second; });
  std::ranges::stable_sort(ordered, {}, [](const message* one) { return one->local_order.value_or(one->at); });
  return ordered;
}

// A reaction pressed: put where the user's is not, taken back where it is.
[[nodiscard]] inline bool reaction_turns_on(const message& said, const std::string& key, const std::string& me) {
  const auto who = said.reactions.find(key);
  return who == said.reactions.end() || !who->second.contains(me);
}

// The line a reply shows of the message it answers, as tdesktop's: its
// text on one line, or what it carries where it has none.
[[nodiscard]] inline std::string reply_line(const message* said, std::string text) {
  if (text.empty() && said && said->attachment)
    text = is_picture(said->attachment->kind) ? std::string("Photo") : said->attachment->name;
  std::ranges::replace(text, '\n', ' ');
  return text;
}

}  // namespace mux::logic
