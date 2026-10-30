// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.edit_history.ui_top -- A message's edit history, as AyuGram
// Desktop's: each version of it the chat's own bubble, on the chat's colour,
// oldest first, each at the time it was written -- the message as it is now
// last, the list scrolled to it. Above the window's messages
// (mux.ui.messages), under the window that shows it as a dialog.
module;
#include "../../inline_in_release.h"
#include "skiff/named.h"
export module mux.plugin.edit_history.ui_top;

import std;
import skiff.scene;
import skiff.compose;
import mux.composition;
import mux.core;
import mux.ui.base;
import mux.ui.messages;
import mux.plugin.edit_history.types;
import mux.plugin.edit_history.ui;

export namespace mux::plugins::edit_history {
using namespace ::mux::ui;

// A version of the message, as the chat shows it: its menu that of a
// version, not edited again.
MUX_INLINE_IN_RELEASE auto version_row(const ui_needs& n, const conversation& in, const message& event, const model* known) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(version_row, c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {4.0f, 12.0f, 4.0f, 12.0f}}),
                   message_alone(n, in, event, true, true, known, true)));
}
MUX_INLINE_IN_RELEASE auto panel(const ui_needs& n, const shown& facts) {
  namespace c = skiff::compose;
  const auto& chat = chat_or_none(facts.known, facts.in);
  const auto versions = edit_versions_of(facts.said, &chat);
  auto rows = versions | std::views::transform([&](const message& event) {
    return version_row(n, chat, event, facts.known);
  }) | std::ranges::to<std::vector>();
  // The newest version in view as it opens: scrolled to its end, as it is
  // built (its rows hold pieces).
  auto list = skiff::compose::lifted([](auto many) {
    auto made = dialog_list(std::move(many));
    made.scrollToEnd(false);
    return made;
  }, c::many(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), std::move(rows)));
  return SKIFF_NAMED(panel, c::column(list_dialog(560.0f),
      page_header<no_back, sends<request::close_shown<shown>>>(*n.colours, "Edit History", {}, {}, false, true), std::move(list)));
}
using box = decltype(panel(std::declval<const ui_needs&>(), std::declval<const shown&>()));
MUX_INLINE_IN_RELEASE auto make_content(const ui_needs& n, const shown& facts) {
  return panel(n, facts);
}
inline dialog_look content_look(std::type_identity<shown>) {
  return {.sheet = sheet::chat{}, .size = dialog_size::fixed{460.0f, 560.0f}};
}

// Its dialog, among the window's (mux.ui:window).
constexpr type_tag<box> extend(contract::window_dialogs, type_tag<feature>) { return {}; }

}  // namespace mux::plugins::edit_history
