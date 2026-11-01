// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.previews.app -- A link's preview fetched for a message shown
// (contract::shown_message_wants): its first link's, once, where the chat
// shows them -- never in an encrypted room, whose links its server would be
// handed (/preview_url), as Element does not by default -- and the
// preview's picture: through the server, or from the site itself where the
// chat fetches them so, or its protocol's servers fetch none.
export module mux.plugin.previews.app;

import std;
import mux.composition;
import mux.core;
import mux.protocols;
import mux.levels;
import mux.ui;
import mux.app.kept;
import mux.app.network;
import mux.app.services;
import mux.plugin.previews.types;

export namespace mux::plugins::previews {

// What the chat's levels say of it.
[[nodiscard]] inline choices chosen_for(app::services& s, const conversation_id& chat) {
  return choices_in_effect(s.kept->plugin_settings<level_choices<choices>>(), s.kept->space_above, chat);
}
[[nodiscard]] inline bool direct_for(app::services& s, const conversation_id& chat) {
  return !proto::offers(state_before(chat.account.speaks), proto::feature::server_previews{}) || chosen_for(s, chat).direct.value_or(false);
}
consteval bool provides(contract::shown_message_wants, type_tag<feature>) { return true; }
template <class Wants>
void wanted_for(contract::shown_message_wants, type_tag<feature>, app::services& s, const conversation& chat, const message& said,
                const Wants& wants) {
  if (chat.encrypted || !chosen_for(s, chat.id).shown.value_or(true))
    return;
  const auto link = ui::first_link_of(said);
  if (!link)
    return;
  if (const auto found = s.model->previews.find(*link); found != s.model->previews.end()) {
    if (found->second.image && !found->second.from_site)
      wants.picture(found->second.image, *found->second.image);
    else if (found->second.image && direct_for(s, chat.id))
      wants.site_picture(*found->second.image);
  } else if (wants.once(*link)) {
    s.net->fetch_preview(chat.id.account, *link, direct_for(s, chat.id));
  }
}

}  // namespace mux::plugins::previews
