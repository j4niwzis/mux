// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.edit_history.ui -- Edit History, in the menu of a message that
// was edited; and the versions it lists, as messages of their own.
export module mux.plugin.edit_history.ui;

import std;
import mux.composition;
import mux.core;
import skiff.compose;
import mux.ui.base;
import mux.plugin.edit_history.types;

export namespace mux::plugins::edit_history {

// Each version of a message as a message of its own, oldest first, the
// message as it is now last. Version IDs are the events that wrote their
// bodies; legacy histories without edit IDs remain readable, but no
// reaction target is guessed for them.
[[nodiscard]] inline std::vector<message> edit_versions_of(const message& now, const conversation* chat = nullptr) {
  const auto as_message = [&](const mux::body& body, auto at, std::string id) {
    message out = now;
    out.id = std::move(id);
    out.body = body;
    out.at = at;
    out.versions.clear();
    out.latest_edit_event.clear();
    out.edited = false;
    out.reactions.clear();
    out.reaction_events.clear();
    if (chat)
      if (const auto* event = ui::held_message(*chat, out.id)) {
        out.reactions = event->reactions;
        out.reaction_events = event->reaction_events;
        out.redacted = event->redacted;
      }
    return out;
  };
  auto versions = std::views::iota(std::size_t{0}, now.versions.size()) | std::views::transform([&](std::size_t i) {
                    return as_message(now.versions[i].body, i == 0 ? now.at : now.versions[i - 1].until,
                                      now.versions[i].event.empty() && i == 0 ? now.id : now.versions[i].event);
                  }) | std::ranges::to<std::vector>();
  versions.push_back(as_message(now.body, now.versions.empty() ? now.at : now.versions.back().until,
                                now.latest_edit_event.empty() && now.versions.empty() ? now.id : now.latest_edit_event));
  return versions;
}

// Edit History, in an edited message's menu (mux.ui:context_menu).
inline auto extend(contract::message_menu, type_tag<feature>, const ui::palette& colours, const ui::emoji_kept&,
                   const ui::menu_facts& facts) {
  return skiff::compose::visible(facts.history,
      ui::row_item(colours, "Edit History", ui::sends<show>{show{facts.id}}, ui::icon::pencil{}, std::nullopt, ui::menu_row_look));
}

}  // namespace mux::plugins::edit_history
