// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.manage: a room's or a space's settings (Manage) -- made from
// what the model knows of it now, and what its protocol adds; a space shown
// as a forum or in Home, or not; what is done to the room from there. A
// part of the program: it reaches the settings it is given and the rest
// through the services.
export module mux.app.manage;

import std;
import splice;
import mux.core;
import mux.config;
import mux.protocols;
import mux.ui;
import mux.ui.proto;
import mux.app.network;
import mux.app.kept;
import mux.app.services;
import mux.app.requests;

export namespace mux::app {

class manage_part {
 public:
  manage_part(services& shared, kept_settings& kept) : s_(&shared), k_(&kept) {}
  manage_part(const manage_part&) = delete;
  manage_part& operator=(const manage_part&) = delete;

  // The room's management: made from what the model knows of it now.
  void apply(const request::open_manage&) {
    (void)s_->root().main().close_space_menu();  // the chat menu its Settings came from
    s_->manage_target.reset();
    if (const auto chosen = s_->root().main().chosen)
      this->manage_chat(*chosen);
  }
  // A space's settings: Manage, for it.
  void apply(const request::manage_space& one) {
    (void)s_->root().main().close_space_menu();
    const auto by = s_->root().main().current;
    if (!by)
      return;
    s_->manage_target = mux::conversation_id{*by, one.room};
    this->manage_chat(*s_->manage_target);
  }
  // A space shown as one chat, its rooms as topics -- or as a space. Not one
  // that holds spaces: it is a space.
  void apply(const request::flip_forum& one) {
    const auto by = s_->root().main().current;
    if (!by)
      return;
    const mux::conversation_id id{*by, one.room};
    const mux::conversation* space = s_->model->find(id);
    if (!space || !space->space)
      return;
    const bool holds_spaces = std::ranges::any_of(space->children, [&](const std::string& child) {
      const mux::conversation* in = s_->model->find(mux::conversation_id{*by, child});
      return in && in->space;
    });
    if (k_->forums.contains(id))
      k_->forums.erase(id);
    else if (!holds_spaces)
      k_->forums.insert(id);
    (void)k_->write();
    s_->refresh_due = true;
    if (auto* managing = s_->root().manage_up())
      managing->show_tab(managing->tab);
  }
  // A space's rooms in Home, or not: its own choice.
  void apply(const request::flip_home_hide& one) {
    const auto by = s_->root().main().current;
    if (!by)
      return;
    const mux::conversation_id id{*by, one.room};
    const mux::conversation* space = s_->model->find(id);
    if (!space || !space->space)
      return;
    if (!k_->hidden_from_home.erase(id))
      k_->hidden_from_home.insert(id);
    (void)k_->write();
    s_->refresh_due = true;
    if (auto* managing = s_->root().manage_up())
      managing->show_tab(managing->tab);
  }
  void apply(const request::close_forum&) { s_->root().main().close_forum(); }
  void apply(const request::manage_forum&) {
    if (const auto& open = s_->root().main().forum_open)
      this->apply(request::manage_space{*open});
  }
  void apply(const request::close_manage&) {
    s_->manage_target.reset();
    s_->root().close_manage();
  }
  // Done to the room being read, by its account.
  void apply(const request::room_act& one) {
    const auto chosen = s_->managed();
    if (!chosen || s_->demo())
      return;
    s_->net->manage(*chosen, one.action);
  }

 private:
  // Manage, for a chat: what the model knows of it, and what the program
  // keeps for it.
  void manage_chat(const mux::conversation_id& id) {
    const mux::conversation* chat = s_->model->find(id);
    if (!chat)
      return;
    mux::ui::room_settings_facts facts{.id = chat->id.id,
                                       .name = chat->name,
                                       .topic = chat->topic.value_or(""),
                                       .alias = chat->alias,
                                       .other_aliases = chat->other_aliases,
                                       .encrypted = chat->encrypted,
                                       .theirs = chat->theirs,
                                       .notify = k_->notify_choices_of(chat->id),
                                       .events_all = k_->room_events.contains(chat->id)
                                                         ? std::optional<bool>(k_->room_events.at(chat->id))
                                                         : std::nullopt,
                                       .typing = k_->typing_sent_in.contains(chat->id) ? std::optional<bool>(k_->typing_sent_in.at(chat->id))
                                                                                   : std::nullopt,
                                       .previews = k_->previews_shown_in.contains(chat->id)
                                                       ? std::optional<bool>(k_->previews_shown_in.at(chat->id))
                                                       : std::nullopt,
                                       .previews_direct = k_->previews_direct_in.contains(chat->id)
                                                              ? std::optional<bool>(k_->previews_direct_in.at(chat->id))
                                                              : std::nullopt,
                                       .receipts = k_->receipts_shown_in.contains(chat->id)
                                                       ? std::optional<bool>(k_->receipts_shown_in.at(chat->id))
                                                       : std::nullopt,
                                       .jump_search = k_->jump_search_in.contains(chat->id)
                                                          ? std::optional<std::int64_t>(k_->jump_search_in.at(chat->id))
                                                          : std::nullopt,
                                       .event_kinds = k_->room_event_kinds.contains(chat->id)
                                                          ? std::optional<mux::config::room_event_kinds>(k_->room_event_kinds.at(chat->id))
                                                          : std::nullopt,
                                       .space = chat->space,
                                       .holds_spaces = std::ranges::any_of(chat->children, [&](const std::string& child) {
                                         const mux::conversation* in = s_->model->find(mux::conversation_id{chat->id.account, child});
                                         return in && in->space;
                                       }),
                                       .forum = k_->forums.contains(chat->id),
                                       .hidden_from_home = k_->hidden_from_home.contains(chat->id),
                                       .speaks = chat->id.account.speaks};
    // The spaces it is in: those of its account whose rooms list it. A
    // space's rooms, by their names; and the account's other rooms, by
    // name, to be added to it.
    if (const auto account = s_->model->accounts().find(chat->id.account); account != s_->model->accounts().end()) {
      const auto named = [](const std::string& id, const mux::conversation& one) {
        return mux::ui::room_settings_facts::named_room{id, one.name.empty() ? id : one.name};
      };
      for (const auto& [id, one] : account->second.conversations) {
        if (one.space && std::ranges::contains(one.children, chat->id.id))
          facts.parents.push_back(named(id, one));
        if (!chat->space || id == chat->id.id)
          continue;
        if (std::ranges::contains(chat->children, id))
          facts.children.push_back(named(id, one));
        else
          facts.addable.push_back(named(id, one));
      }
      // The children the account is not in: by their IDs.
      for (const std::string& child : chat->children)
        if (!account->second.conversations.contains(child))
          facts.children.push_back({child, child});
      std::ranges::sort(facts.addable, {}, &mux::ui::room_settings_facts::named_room::name);
    }
    // What its protocol fills of them: Matrix's own level and privileged users.
    mux::proto::manage_facts(mux::ui::protocol_state_of(s_->ui, chat->id.account), *chat, facts);
    s_->root().open_manage(facts);
  }

  services* s_;
  kept_settings* k_;
};

}  // namespace mux::app
