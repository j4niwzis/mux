// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.room_settings.app -- A room's or a space's settings' part of
// the program: made from what the model knows of the room now, what the
// program keeps for it and what the plugins fill (contract::opened); its
// tabs, ←, ✕ and Esc; narrow or not as the window is; a room avatar chosen
// and sent. It says which chat they are for (services::managing).
export module mux.plugin.room_settings.app;

import std;
import splice;
import skia;
import mux.composition;
import mux.core;
import mux.config;
import mux.media;
import mux.protocols;
import mux.plugins.types;
import mux.ui;
import mux.app.network;
import mux.app.kept;
import mux.app.services;
import mux.platform.files;
import skiff.platform.dialogs;
import mux.plugin.room_settings.types;
import mux.plugin.room_settings.ui;

export namespace mux::plugins::room_settings {

// What the plugins fill of the facts as they open, in their order.
template <class... Features>
void filled_by(plugin_list<Features...>, room_settings_facts& facts, const conversation& chat) {
  (room_opened(contract::opened{}, type_tag<Features>{}, facts, chat), ...);
}

class part {
 public:
  explicit part(app::services& shared) : s_(&shared) {}

  // The room's settings: made from what the model knows of it now.
  void apply(const ui::request::open_manage&) {
    if (const auto chosen = s_->chosen())
      this->manage_chat(*chosen);
  }
  // A space's settings, from its menu or from the list of chats open (a forum).
  void apply(const ui::request::manage_space& one) {
    if (const auto by = s_->listed())
      this->manage_chat(conversation_id{*by, one.room});
  }
  void apply(const ui::request::manage_scope&) {
    if (const auto& open = s_->showing->look<ui::chat_shown>()->scope)
      this->apply(ui::request::manage_space{*open});
  }
  // A tab picked: up, and on a narrow window in the tab list's place.
  void apply(const manage_tab& one) {
    this->change([&](room_settings_facts& now) {
      now.tab = one.tab;
      now.navigation = false;
    });
  }
  // ←: an editor open on the tab closed first; else, on a narrow window,
  // back to the tab list.
  void apply(const manage_back&) {
    this->change([](room_settings_facts& now) {
      if (!close_manage_editor(now))
        now.navigation = now.narrow;
    });
  }
  // ✕ or Esc: an editor open closed first; on a narrow window, the page back
  // to the tab list; else the settings shut.
  void apply(const ui::request::close_shown<room_settings_facts>&) {
    if (const auto& open = *s_->showing->look<std::optional<room_settings_facts>>()) {
      auto now = *open;
      const bool editor = close_manage_editor(now);
      if (editor || (now.narrow && !now.navigation)) {
        now.navigation = !editor || now.navigation;
        ui::show(*s_->showing, std::optional(std::move(now)));
        return;
      }
    }
    ui::show<room_settings_facts>(*s_->showing, std::nullopt);
    s_->managing.reset();
  }
  // Each frame, where they are up: narrow or not, as the window is.
  void tick() {
    const auto& open = *s_->showing->look<std::optional<room_settings_facts>>();
    if (!open) {
      s_->managing.reset();
      return;
    }
    const bool narrow = s_->showing->look<ui::window_shape>()->narrow();
    if (open->narrow != narrow)
      this->change([&](room_settings_facts& now) {
        // Turned narrow: the tab list first.
        now.navigation = narrow;
        now.narrow = narrow;
      });
  }
  // A room's avatar chosen from the files, and sent.
  void apply(const ui::request::pick_room_avatar& one) {
    if (s_->demo()) return;
    picking_avatar_ = one.room;
    s_->system_dialogs->choose_files();
  }
  bool took_files(const std::vector<std::string>& paths, bool dropped) {
    if (!picking_avatar_ || dropped) return false;
    const auto room = *std::exchange(picking_avatar_, std::nullopt);
    if (paths.empty()) return true;
    auto bytes = mux::platform::files::read(paths.front());
    const auto kind = bytes ? mux::media::picture_of(*bytes) : std::nullopt;
    if (!kind || !skia::decodeImage(bytes->data(), bytes->size())) {
      s_->notice("Room Avatar", "Choose an image mux can show."); return true;
    }
    this->change([&](room_settings_facts& now) {
      if (now.chat == room)
        manage_avatar_sending(now);
    });
    s_->net->on_account_of(room, [room = room.id, bytes = std::move(*bytes), name = mux::platform::files::name(paths.front()),
        mime = std::string(mux::media::mimetype_of(*kind))](auto& account)
        -> decltype(void(account.upload_room_avatar(room, bytes, name, mime))) { account.upload_room_avatar(room, bytes, name, mime); });
    return true;
  }

 private:
  // Settings, for a chat: what the model knows of it, what the program
  // keeps for it, and what the plugins fill.
  void manage_chat(const conversation_id& id) {
    const conversation* chat = s_->model->find(id);
    if (!chat)
      return;
    s_->net->on_account_of(id, [room = id.id](auto& account)
        -> decltype(void(account.fetch_room_settings(room))) { account.fetch_room_settings(room); });
    auto& k = *s_->kept;
    room_settings_facts facts{.id = chat->id.id,
                              .name = chat->name,
                              .topic = chat->topic.value_or(""),
                              .avatar = chat->avatar,
                              .alias = chat->alias,
                              .other_aliases = chat->other_aliases,
                              .encrypted = chat->encrypted,
                              .group = chat->kind.template get_if<conversation_kind::direct>() == nullptr,
                              .leavable = proto::can_leave(ui::protocol_state_of(s_->ui, chat->id.account), *chat),
                              .theirs = chat->theirs,
                              .notify = k.notify_choices_of(chat->id),
                              .events_all = k.own_of<&app::chat_choices::room_events>(chat->id),
                              .jump_search = k.own_of<&app::chat_choices::jump_search>(chat->id),
                              .event_kinds = k.own_of<&app::chat_choices::room_event_kinds>(chat->id),
                              .space = chat->space,
                              .holds_spaces = std::ranges::any_of(chat->children, [&](const std::string& child) {
                                const conversation* in = s_->model->find(conversation_id{chat->id.account, child});
                                return in && in->space;
                              }),
                              .speaks = chat->id.account.speaks};
    facts.chat = chat->id;
    facts.members = chat->members;
    // Its own choices there to be bound to, chosen or not.
    k.ensure_chat(chat->id);
    // The spaces it is in: those of its account whose rooms list it. A
    // space's rooms, by their names; and the account's other rooms, by
    // name, to be added to it.
    if (const account* known = s_->model->accounts().find(chat->id.account)) {
      const auto named = [](const std::string& room, const conversation& one) {
        return room_settings_facts::named_room{room, one.name.empty() ? room : one.name};
      };
      for (const auto& [room, one] : known->conversations) {
        if (one.space && std::ranges::contains(one.children, chat->id.id))
          facts.parents.push_back(named(room, one));
        if (!chat->space || room == chat->id.id)
          continue;
        if (std::ranges::contains(chat->children, room))
          facts.children.push_back(named(room, one));
        else
          facts.addable.push_back(named(room, one));
      }
      // The children the account is not in: by their IDs.
      for (const std::string& child : chat->children)
        if (!known->conversations.contains(child))
          facts.children.push_back({child, child});
      std::ranges::sort(facts.addable, {}, &room_settings_facts::named_room::name);
    }
    // What the plugins fill: room_admin's tab up first, what it keeps, the
    // levels it knows.
    filled_by(composition::providers<contract::opened, enabled_plugins>{}, facts, *chat);
    facts.narrow = s_->showing->look<ui::window_shape>()->narrow();
    facts.navigation = facts.narrow;
    s_->managing = chat->id;
    ui::show(*s_->showing, std::optional(facts));
  }
  template <class Change>
  void change(Change change) {
    if (const auto& open = *s_->showing->look<std::optional<room_settings_facts>>()) {
      auto now = *open;
      change(now);
      ui::show(*s_->showing, std::optional(std::move(now)));
    }
  }

  app::services* s_;
  std::optional<conversation_id> picking_avatar_;
};

// Its part, among the program's (mux.app.program).
inline part extend(::mux::contract::program_parts, type_tag<feature>, app::services& shared) { return part{shared}; }

}  // namespace mux::plugins::room_settings
