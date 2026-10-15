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
import mux.platform.files;
import mux.platform.dialogs;
import mux.media;
import skia;

export namespace mux::app {

class manage_part {
 public:
  manage_part(services& shared, kept_settings& kept) : s_(&shared), k_(&kept) {}
  manage_part(const manage_part&) = delete;
  manage_part& operator=(const manage_part&) = delete;

  // The room's management: made from what the model knows of it now.
  void apply(const request::open_manage&) {
    s_->root().main().close_space_menu_soon();  // the chat menu its Settings came from
    if (const auto chosen = s_->root().main().chosen)
      this->manage_chat(*chosen);
  }
  // A space's settings: Manage, for it.
  void apply(const request::manage_space& one) {
    s_->root().main().close_space_menu_soon();
    const auto by = s_->root().main().current;
    if (!by)
      return;
    this->manage_chat(mux::conversation_id{*by, one.room});
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
    if (k_->own_of<&mux::app::chat_choices::forum>(id) || !holds_spaces)
      k_->flip<&mux::app::chat_choices::forum>(id);
    (void)k_->write();
    s_->refresh_due = true;
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
    k_->flip<&mux::app::chat_choices::hidden_from_home>(id);
    (void)k_->write();
    s_->refresh_due = true;
  }
  void apply(const request::close_forum&) {
    mux::ui::change_shown<mux::ui::chat_shown>(*s_->showing, [](mux::ui::chat_shown& now) { now.forum.reset(); });
  }
  void apply(const request::manage_forum&) {
    if (const auto& open = s_->root().main().forum_open)
      this->apply(request::manage_space{*open});
  }
  // A tab picked: up, and on a narrow window in the tab list's place.
  void apply(const request::manage_tab& one) {
    this->change([&](mux::ui::room_settings_facts& now) {
      now.tab = one.tab;
      now.navigation = false;
    });
  }
  // ←: an editor open on the tab closed first; else, on a narrow window,
  // back to the tab list.
  void apply(const request::manage_back&) {
    this->change([](mux::ui::room_settings_facts& now) {
      if (!mux::ui::close_manage_editor(now))
        now.navigation = now.narrow;
    });
  }
  // ✕ or Esc: an editor open closed first; on a narrow window, the page back
  // to the tab list; else the settings shut.
  void apply(const request::close_manage&) {
    const auto& open = *s_->showing->look<std::optional<mux::ui::room_settings_facts>>();
    if (open) {
      auto now = *open;
      const bool editor = mux::ui::close_manage_editor(now);
      if (editor || (now.narrow && !now.navigation)) {
        now.navigation = !editor || now.navigation;
        mux::ui::show(*s_->showing, std::optional(std::move(now)));
        return;
      }
    }
    mux::ui::show<mux::ui::room_settings_facts>(*s_->showing, std::nullopt);
  }
  // Each frame, where they are up: narrow or not, as the window is.
  void show() {
    const auto& open = *s_->showing->look<std::optional<mux::ui::room_settings_facts>>();
    const bool narrow = s_->showing->look<mux::ui::window_shape>()->width < kNarrow;
    if (open && open->narrow != narrow)
      this->change([&](mux::ui::room_settings_facts& now) {
        // Turned narrow: the tab list first.
        now.navigation = narrow;
        now.narrow = narrow;
      });
  }
  void apply(const request::pick_room_avatar& one) {
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
    this->change([&](mux::ui::room_settings_facts& now) {
      if (now.chat == room)
        mux::ui::manage_avatar_sending(now);
    });
    s_->net->on_account_of(room, [room = room.id, bytes = std::move(*bytes), name = mux::platform::files::name(paths.front()),
        mime = std::string(mux::media::mimetype_of(*kind))](auto& account)
        -> decltype(void(account.upload_room_avatar(room, bytes, name, mime))) { account.upload_room_avatar(room, bytes, name, mime); });
    return true;
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
    s_->net->on_account_of(id, [room = id.id](auto& account)
        -> decltype(void(account.fetch_room_settings(room))) { account.fetch_room_settings(room); });
    mux::ui::room_settings_facts facts{.id = chat->id.id,
                                       .name = chat->name,
                                       .topic = chat->topic.value_or(""),
                                       .avatar = chat->avatar,
                                       .alias = chat->alias,
                                       .other_aliases = chat->other_aliases,
                                       .encrypted = chat->encrypted,
                                       .group = chat->kind.template get_if<conversation_kind::direct>() == nullptr,
                                       .leavable = mux::proto::can_leave(mux::ui::protocol_state_of(s_->ui, chat->id.account), *chat),
                                       .theirs = chat->theirs,
                                       .notify = k_->notify_choices_of(chat->id),
                                       .events_all = k_->own_of<&mux::app::chat_choices::room_events>(chat->id),
                                       .typing = k_->own_of<&mux::app::chat_choices::typing>(chat->id),
                                       .previews = k_->own_of<&mux::app::chat_choices::previews>(chat->id),
                                       .previews_direct = k_->own_of<&mux::app::chat_choices::previews_direct>(chat->id),
                                       .receipts = k_->own_of<&mux::app::chat_choices::receipts>(chat->id),
                                       .jump_search = k_->own_of<&mux::app::chat_choices::jump_search>(chat->id),
                                       .event_kinds = k_->own_of<&mux::app::chat_choices::room_event_kinds>(chat->id),
                                       .space = chat->space,
                                       .holds_spaces = std::ranges::any_of(chat->children, [&](const std::string& child) {
                                         const mux::conversation* in = s_->model->find(mux::conversation_id{chat->id.account, child});
                                         return in && in->space;
                                       }),
                                       .forum = k_->own_of<&mux::app::chat_choices::forum>(chat->id),
                                       .hidden_from_home = k_->own_of<&mux::app::chat_choices::hidden_from_home>(chat->id),
                                       .speaks = chat->id.account.speaks};
    facts.chat = chat->id;
    facts.members = chat->members;
    // Its own choices there to be bound to, chosen or not.
    k_->ensure_chat(chat->id);
    // The spaces it is in: those of its account whose rooms list it. A
    // space's rooms, by their names; and the account's other rooms, by
    // name, to be added to it.
    if (const mux::account* account = s_->model->accounts().find(chat->id.account)) {
      const auto named = [](const std::string& id, const mux::conversation& one) {
        return mux::ui::room_settings_facts::named_room{id, one.name.empty() ? id : one.name};
      };
      for (const auto& [id, one] : account->conversations) {
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
        if (!account->conversations.contains(child))
          facts.children.push_back({child, child});
      std::ranges::sort(facts.addable, {}, &mux::ui::room_settings_facts::named_room::name);
    }
    // What its protocol fills of them: Matrix's own level and privileged users.
    mux::proto::manage_facts(mux::ui::protocol_state_of(s_->ui, chat->id.account), *chat, facts);
    // The protocol's first tab, and what it keeps while they are open.
    spl::visit([&](auto tag) {
      facts.tab = mux::ui::initial_manage_tab(mux::state_of<decltype(tag)>{});
      facts.manage = mux::ui::manage_part_t(std::in_place_type<mux::ui::first_manage_part_t<decltype(tag)>>);
    }, facts.speaks);
    facts.narrow = s_->showing->look<mux::ui::window_shape>()->width < kNarrow;
    facts.navigation = facts.narrow;
    mux::ui::show(*s_->showing, std::optional(facts));
  }
  // Narrower than this, the tabs and the page take the window by turns.
  static constexpr float kNarrow = 600.0f;
  template <class Change>
  void change(Change change) {
    if (const auto& open = *s_->showing->look<std::optional<mux::ui::room_settings_facts>>()) {
      auto now = *open;
      change(now);
      mux::ui::show(*s_->showing, std::optional(std::move(now)));
    }
  }

  std::optional<mux::conversation_id> picking_avatar_;
  services* s_;
  kept_settings* k_;
};

}  // namespace mux::app
