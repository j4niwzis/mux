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

// What a plugin fills as a kind's settings open where it has nothing for
// that kind: nothing.
namespace opened_defaults {
inline void room_opened(contract::opened, const auto&, const auto&, auto&, const conversation&) {}
}  // namespace opened_defaults
// What the plugins fill of the facts as they open, in their order.
template <class Kind, class... Features>
void filled_by(plugin_list<Features...>, room_settings_of<Kind>& facts, const conversation& chat) {
  using opened_defaults::room_opened;
  (room_opened(contract::opened{}, type_tag<Features>{}, type_tag<Kind>{}, facts, chat), ...);
}
// What its part takes for every kind, as templates: listed for the
// runtime walks outside a release build, which cannot read a type out of a
// template's apply.
template <class... Kinds>
auto requests_of(ui::type_list<Kinds...>)
    -> proto::request_list<manage_tab<Kinds>..., ui::request::close_shown<room_settings_of<Kinds>>...>;

class part {
 public:
  explicit part(app::services& shared) : s_(&shared) {}
  using requests_taken = decltype(requests_of(room_kinds{}));

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
  template <class Kind>
  void apply(const manage_tab<Kind>& one) {
    this->change<Kind>([&](room_settings_of<Kind>& now) {
      now.tab = one.tab;
      now.navigation = false;
    });
  }
  // ←: an editor open on the tab closed first; else, on a narrow window,
  // back to the tab list. Of whichever kind is open.
  void apply(const manage_back&) {
    this->each_open([](auto& now) {
      if (!close_manage_editor(now))
        now.navigation = now.narrow;
    });
  }
  // ✕ or Esc: an editor open closed first; on a narrow window, the page back
  // to the tab list; else the settings shut.
  template <class Kind>
  void apply(const ui::request::close_shown<room_settings_of<Kind>>&) {
    if (const auto& open = *s_->showing->look<std::optional<room_settings_of<Kind>>>()) {
      auto now = *open;
      const bool editor = close_manage_editor(now);
      if (editor || (now.narrow && !now.navigation)) {
        now.navigation = !editor || now.navigation;
        ui::show(*s_->showing, std::optional(std::move(now)));
        return;
      }
    }
    ui::show<room_settings_of<Kind>>(*s_->showing, std::nullopt);
    s_->managing.reset();
  }
  // Each frame, where they are up: narrow or not, as the window is.
  void tick() { this->tick_kinds(room_kinds{}); }
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
    this->each_open([&](auto& now) {
      if (now.chat == room)
        manage_avatar_sending(now);
    });
    s_->net->on_account_of(room, [room = room.id, bytes = std::move(*bytes), name = mux::platform::files::name(paths.front()),
        mime = std::string(mux::media::mimetype_of(*kind))](auto& account)
        -> decltype(void(account.upload_room_avatar(room, bytes, name, mime))) { account.upload_room_avatar(room, bytes, name, mime); });
    return true;
  }

 private:
  // Settings, for a chat: of its kind -- its protocol's rooms', or, where
  // a one-to-one chat of its protocol is no room (XMPP's), its chats' --
  // read once here, from its protocol's type.
  void manage_chat(const conversation_id& id) {
    const conversation* chat = s_->model->find(id);
    if (!chat)
      return;
    const bool direct = chat->kind.template get_if<conversation_kind::direct>() != nullptr;
    spl::visit([&]<class Protocol>(Protocol) {
      if (direct && !proto::direct_chats_are_rooms<state_of<Protocol>>())
        this->manage_as<room_kind::chat_of<Protocol>>(*chat);
      else
        this->manage_as<room_kind::room_of<Protocol>>(*chat);
    }, chat->id.account.speaks);
  }
  // A kind's settings, for a chat: what the model knows of it, what the
  // program keeps for it, and what the plugins fill.
  template <class Kind>
  void manage_as(const conversation& chat_ref) {
    const conversation* chat = &chat_ref;
    const conversation_id id = chat->id;
    s_->net->on_account_of(id, [room = id.id](auto& account)
        -> decltype(void(account.fetch_room_settings(room))) { account.fetch_room_settings(room); });
    auto& k = *s_->kept;
    using facts_t = room_settings_of<Kind>;
    facts_t facts{.id = chat->id.id,
                  .name = chat->name,
                  .topic = chat->topic.value_or(""),
                  .avatar = chat->avatar,
                  .alias = chat->alias,
                  .other_aliases = chat->other_aliases,
                  .encrypted = chat->encrypted,
                  .theirs = their_part_of<Kind>(chat->theirs),
                  .notify = k.notify_choices_of(chat->id),
                  .events_all = k.own_of<&app::chat_choices::room_events>(chat->id),
                  .jump_search = k.own_of<&app::chat_choices::jump_search>(chat->id),
                  .event_kinds = k.own_of<&app::chat_choices::room_event_kinds>(chat->id),
                  .space = chat->space,
                  .holds_spaces = std::ranges::any_of(chat->children, [&](const std::string& child) {
                    const conversation* in = s_->model->find(conversation_id{chat->id.account, child});
                    return in && in->space;
                  })};
    facts.chat = chat->id;
    facts.members = chat->members;
    // Its own choices there to be bound to, chosen or not.
    k.ensure_chat(chat->id);
    // The spaces it is in: those of its account whose rooms list it. A
    // space's rooms, by their names; and the account's other rooms, by
    // name, to be added to it.
    if (const account* known = s_->model->accounts().find(chat->id.account)) {
      const auto named = [](const std::string& room, const conversation& one) {
        return ui::named_room{room, one.name.empty() ? room : one.name};
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
      std::ranges::sort(facts.addable, {}, &ui::named_room::name);
    }
    // What the plugins fill: room_admin's tab up first, what it keeps, the
    // levels it knows.
    filled_by(composition::providers<contract::opened, enabled_plugins>{}, facts, *chat);
    facts.narrow = s_->showing->look<ui::window_shape>()->narrow();
    facts.navigation = facts.narrow;
    s_->managing = chat->id;
    close_room_settings(*s_->showing);
    ui::show(*s_->showing, std::optional(std::move(facts)));
  }
  // The room's protocol part, as the kind holds it: the part where the kind
  // has its type; none else (a plain chat's) -- by overloading.
  template <class Kind>
  struct part_into {
    their_part_t<Kind> operator()(const auto&) const { return proto::no_room_part{}; }
    template <class Part>
      requires std::constructible_from<their_part_t<Kind>, const Part&>
    their_part_t<Kind> operator()(const Part& part) const {
      return part;
    }
  };
  template <class Kind>
  static their_part_t<Kind> their_part_of(const room_part_t& all) {
    return spl::visit(part_into<Kind>{}, all);
  }
  // Each frame: the kind open, narrow or not as the window is -- shown again
  // only where that changed. None open: no chat managed.
  template <class... Kinds>
  void tick_kinds(ui::type_list<Kinds...>) {
    const bool narrow = s_->showing->look<ui::window_shape>()->narrow();
    const bool any = (this->tick_kind<Kinds>(narrow) | ...);
    if (!any)
      s_->managing.reset();
  }
  template <class Kind>
  bool tick_kind(bool narrow) {
    const auto& open = *s_->showing->look<std::optional<room_settings_of<Kind>>>();
    if (!open)
      return false;
    if (open->narrow != narrow)
      this->change<Kind>([&](room_settings_of<Kind>& now) {
        // Turned narrow: the tab list first.
        now.navigation = narrow;
        now.narrow = narrow;
      });
    return true;
  }
  template <class Kind, class Change>
  void change(Change change) {
    if (const auto& open = *s_->showing->look<std::optional<room_settings_of<Kind>>>()) {
      auto now = *open;
      change(now);
      ui::show(*s_->showing, std::optional(std::move(now)));
    }
  }
  // Whichever kind's settings are open, changed.
  template <class Change, class... Kinds>
  void each_open(Change change, ui::type_list<Kinds...>) {
    (this->change<Kinds>(change), ...);
  }
  template <class Change>
  void each_open(Change change) {
    this->each_open(std::move(change), room_kinds{});
  }

  app::services* s_;
  std::optional<conversation_id> picking_avatar_;
};

// Its part, among the program's (mux.app.program).
inline part extend(::mux::contract::program_parts, type_tag<feature>, app::services& shared) { return part{shared}; }

}  // namespace mux::plugins::room_settings
