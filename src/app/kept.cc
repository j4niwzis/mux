// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.kept: what the accounts file keeps for the program -- the
// accounts, the proxy profiles, the chats muted, the theme,
// how much moves, how much is kept, what is done to files sent -- and its
// writing back. Held apart from what the program does with it: the parts
// that change it write it.
export module mux.app.kept;

import std;
import mux.vault;
import splice;
import knot;
import mux.core;
import mux.config;
import mux.protocols;
import mux.logic.room_events;
import skiff.model;
import mux.kept_root;
import mux.plugins.types;
import mux.composition;

export namespace mux::app {

using chat_choices = mux::config::chat_choices;

using mux::recently_used;
using mux::kept_root;

// The file to be written again: one, however many changes asked for it.
struct write_kept {
  [[nodiscard]] constexpr int key() const { return 0; }
};
// How the window looks changed: what the window's parts read of it put in
// place; and where the theme, the accent or the background behind the
// window did, every style resolved again and the window made anew.
struct looks_changed {
  [[nodiscard]] constexpr int key() const { return 0; }
};
struct restyle_wanted {
  [[nodiscard]] constexpr int key() const { return 0; }
};
// The window's opacity chosen: in effect at once where the window is see-
// through already, else from the next start.
struct opacity_chosen {
  [[nodiscard]] constexpr int key() const { return 0; }
};
// Deleted messages shown or not: the chats' model told, the window shown
// again.
struct deleted_shown {
  [[nodiscard]] constexpr int key() const { return 0; }
};
// How much moves chosen: the window's paint told.
struct motion_chosen {
  [[nodiscard]] constexpr int key() const { return 0; }
};
// A chat's choices changed or gone, or what notifies: the file is written
// again. UnifiedPush turned on or off: its connector with it.
// What the reactions asked for since the program last looked: each a
// mark, however many changes asked for it -- the file written once, the
// window restyled once -- done by the program after the event (settle_model).
struct kept_due {
  bool write = false;
  bool looks = false;
  bool restyle = false;
  bool motion = false;
  bool deleted = false;
  bool opacity = false;
};
struct kept_reactions {
  // Where what they ask for is marked: the kept settings' own (kept_settings::due).
  kept_due* due = nullptr;
  // What they ask for, taken -- routed by the model to the reactions' own
  // on(request), as a request no scope takes is: marked, to be done once.
  skiff::model::Nothing on(const write_kept&) const { return mark(&kept_due::write); }
  skiff::model::Nothing on(const looks_changed&) const { return mark(&kept_due::looks); }
  skiff::model::Nothing on(const restyle_wanted&) const { return mark(&kept_due::restyle); }
  skiff::model::Nothing on(const motion_chosen&) const { return mark(&kept_due::motion); }
  skiff::model::Nothing on(const deleted_shown&) const { return mark(&kept_due::deleted); }
  skiff::model::Nothing on(const opacity_chosen&) const { return mark(&kept_due::opacity); }
  skiff::model::Nothing mark(bool kept_due::*what) const {
    due->*what = true;
    return {};
  }
  [[nodiscard]] write_kept on(skiff::model::Changed<chat_choices>, const chat_choices&) const { return {}; }
  [[nodiscard]] write_kept on(skiff::model::Changed<std::vector<mux::config::chat_placement>>, const auto&) const { return {}; }
  [[nodiscard]] write_kept on(skiff::model::Changed<recently_used>, const recently_used&) const { return {}; }
  [[nodiscard]] write_kept on(skiff::model::Changed<mux::config::notification_settings>,
                              const mux::config::notification_settings&) const {
    return {};
  }
  [[nodiscard]] write_kept on(skiff::model::Changed<mux::config::sending_settings>, const mux::config::sending_settings&) const { return {}; }
  // A plugin's own settings changed (contract::settings_kept): the file
  // written again.
  template <plugin_kept Settings>
  [[nodiscard]] write_kept on(skiff::model::Changed<Settings>, const Settings&) const { return {}; }
  [[nodiscard]] write_kept on(skiff::model::Changed<mux::config::history_settings>, const mux::config::history_settings&) const { return {}; }
  [[nodiscard]] deleted_shown on(skiff::model::Changed<skiff::model::Field<&mux::config::history_settings::show_deleted>>, const auto&) const {
    return {};
  }
  [[nodiscard]] std::tuple<write_kept, looks_changed> on(skiff::model::Changed<mux::config::look_settings>, const mux::config::look_settings&) const {
    return {};
  }
  [[nodiscard]] restyle_wanted on(skiff::model::Changed<skiff::model::Field<&mux::config::look_settings::theme>>, const auto&) const { return {}; }
  [[nodiscard]] restyle_wanted on(skiff::model::Changed<skiff::model::Field<&mux::config::look_settings::accent>>, const auto&) const { return {}; }
  [[nodiscard]] restyle_wanted on(skiff::model::Changed<skiff::model::Field<&mux::config::look_settings::wallpaper_behind>>, const auto&) const {
    return {};
  }
  [[nodiscard]] restyle_wanted on(skiff::model::Changed<skiff::model::Field<&mux::config::look_settings::chat_avatar_rounding>>, const auto&) const {
    return {};
  }
  [[nodiscard]] restyle_wanted on(skiff::model::Changed<skiff::model::Field<&mux::config::look_settings::person_avatar_rounding>>, const auto&) const {
    return {};
  }
  [[nodiscard]] motion_chosen on(skiff::model::Changed<skiff::model::Field<&mux::config::look_settings::motion>>, const auto&) const { return {}; }
  [[nodiscard]] opacity_chosen on(skiff::model::Changed<skiff::model::Field<&mux::config::look_settings::window_opacity>>, const auto&) const {
    return {};
  }
  [[nodiscard]] write_kept on(skiff::model::Changed<mux::config::account_t>, const mux::config::account_t&) const { return {}; }
  [[nodiscard]] write_kept on(skiff::model::Removed<mux::config::account_t>, const mux::config::account_t&, const std::string&) const { return {}; }
  [[nodiscard]] write_kept on(skiff::model::Removed<chat_choices>, const chat_choices&, const conversation_id&) const {
    return {};
  }
};
using kept_model = skiff::model::Model<kept_root, kept_reactions>;

struct kept_settings {
  // What is kept is read and written through: the program's, given by main.
  mux::vault::vault* vault = nullptr;
  std::filesystem::path config_path;
  // The file's accounts of a protocol this build has not: written back as
  // they were, not lost.
  std::vector<mux::config::saved_account> foreign_accounts;
  // What was used lately, the proxy profiles and the chats placed in other
  // lists, as the model holds them.
  [[nodiscard]] const recently_used& recent() const { return state.root().recent.fValue; }
  // The proxy profiles: the plugins' say (contract::proxy_profiles), none
  // where none says.
  [[nodiscard]] std::vector<mux::config::proxy_settings> proxies() const {
    return this->proxies_said_by(composition::providers<contract::proxy_profiles, enabled_plugins>{});
  }
  template <class... Features>
  [[nodiscard]] std::vector<mux::config::proxy_settings> proxies_said_by(plugin_list<Features...>) const {
    std::vector<mux::config::proxy_settings> out;
    ((out = proxy_profiles(contract::proxy_profiles{}, type_tag<Features>{}, this->plugin_settings<plugin_settings_t<Features>>())), ...);
    return out;
  }
  // The same, of a settings file not yet taken (the accounts started before
  // what is kept is: mux.app.program's begin).
  [[nodiscard]] static std::vector<mux::config::proxy_settings> proxies_in(const mux::config::file& saved) {
    return proxies_in_by(saved, composition::providers<contract::proxy_profiles, enabled_plugins>{});
  }
  template <class... Features>
  [[nodiscard]] static std::vector<mux::config::proxy_settings> proxies_in_by(const mux::config::file& saved, plugin_list<Features...>) {
    std::vector<mux::config::proxy_settings> out;
    ((out = proxies_in_of<Features>(saved)), ...);
    return out;
  }
  template <class Feature>
  [[nodiscard]] static std::vector<mux::config::proxy_settings> proxies_in_of(const mux::config::file& saved) {
    if (!saved.plugins)
      return {};
    const auto found = saved.plugins->find(std::string(Feature::name));
    if (found == saved.plugins->end())
      return {};
    // The file's text kept for it, as read_plugin reads it.
    const auto read = knot::try_read<plugin_settings_t<Feature>>(found->second.text);
    if (!read)
      return {};
    return proxy_profiles(contract::proxy_profiles{}, type_tag<Feature>{}, *read);
  }
  [[nodiscard]] const std::vector<mux::config::chat_placement>& placements() const { return state.root().placements.fValue; }
  // A part the model's root holds one of, changed: a copy changed, set
  // again where it changed.
  template <class Part, class F>
  void change_part(F&& change) {
    const Part& had = *state.template look<Part>();
    Part next = had;
    change(next);
    if (!(next == had))
      this->set_part(std::move(next));
  }
  // What is done to a picture dropped before it is sent, as the model holds it.
  [[nodiscard]] const mux::config::sending_settings& sending() const { return state.root().sending.fValue; }
  // What is kept and shown of the history, as the model holds it.
  [[nodiscard]] const mux::config::history_settings& history() const { return state.root().history.fValue; }
  // A plugin's own settings (contract::settings_kept), as the model holds
  // them; and set, whole.
  template <class Settings>
  [[nodiscard]] const Settings& plugin_settings() const { return *state.template look<Settings>(); }
  template <class Settings>
  void set_plugin_settings(Settings now) { this->set_part(std::move(now)); }
  // What draws the window, as it is made: the plugins' say
  // (contract::start_renderer) -- the last one's -- OpenGL where none says.
  [[nodiscard]] mux::config::renderer_t renderer_at_start() const {
    return this->renderer_said_by(composition::providers<contract::start_renderer, enabled_plugins>{});
  }
  template <class... Features>
  [[nodiscard]] mux::config::renderer_t renderer_said_by(plugin_list<Features...>) const {
    mux::config::renderer_t out = mux::config::renderer::opengl{};
    ((out = renderer_at_start(contract::start_renderer{}, type_tag<Features>{}, this->plugin_settings<plugin_settings_t<Features>>())), ...);
    return out;
  }
  // A part the model's root holds one of, made what is given, whole.
  template <class Part>
  void set_part(Part now) {
    (void)state.apply(skiff::model::edit(skiff::model::placeOf<Part, kept_root>(), skiff::model::setTo(std::move(now))));
  }
  // How the window looks, as the model holds it.
  [[nodiscard]] const mux::config::look_settings& appearance() const { return state.root().looks.fValue; }
  // One setting of what the model keeps, chosen in its place by its member
  // pointer: a field of a part the root holds one of.
  template <auto M, class T>
  void choose_field(T now) {
    using Owner = typename skiff::model::detail::MemberPointer<decltype(M)>::Class;
    using Part = std::remove_cvref_t<decltype(std::declval<Owner&>().*M)>;
    (void)state.apply(skiff::model::edit(skiff::model::placeOf<skiff::model::Field<M>, kept_root>(),
                                         skiff::model::setTo(Part(std::move(now)))));
  }
  // What each chat -- or space -- chose for itself, by the chat: only those
  // that chose something.
  // Where the reactions mark what they ask for: before the model, which
  // points at it.
  kept_due due;
  kept_model state{kept_root{}, kept_reactions{&due}};
  // The space each chat is in -- the first found holding it -- set by the
  // program from the model at each refresh: a space's own choices are its
  // rooms', where they have none, nearest first, through spaces in spaces.
  std::map<conversation_id, conversation_id> space_above;
  // What a chat's settings come to, as the window reads them too.
  [[nodiscard]] mux::chat_settings reads() const { return {&state.root(), &space_above}; }

  // A chat's own choice of one setting, by its member pointer; as unsaid
  // where it chose nothing.
  template <auto M>
  [[nodiscard]] auto own_of(const conversation_id& chat) const {
    return this->reads().own_of<M>(chat);
  }
  // One setting of a chat's, chosen. Its entry stays where it then has
  // nothing chosen: a room's settings open are bound to it, and taken away
  // -- Default pressed, a chat unmuted -- their buttons edited nothing until
  // the dialog was opened again (a tester's). What is saved leaves out what
  // is not chosen, so an empty one costs nothing in the file.
  template <auto M, class T>
  void choose(const conversation_id& chat, T now) {
    const chat_choices* had = state.root().chats.find(chat);
    chat_choices next = had == nullptr ? chat_choices{} : *had;
    next.*M = now;
    if (had == nullptr) {
      (void)state.apply(skiff::model::put<chat_choices>(chat, std::move(next)));
    } else {
      // Only the one setting, in its place: what shows it is told, and
      // nothing else.
      using Part = std::remove_cvref_t<decltype(next.*M)>;
      (void)state.apply(skiff::model::edit(skiff::model::placeOf<skiff::model::Field<M>, kept_root>(chat),
                                           skiff::model::setTo(Part(std::move(now)))));
    }
  }
  // A flag of a chat's, flipped.
  template <auto M>
  void flip(const conversation_id& chat) {
    this->choose<M>(chat, !this->own_of<M>(chat));
  }
  // The chats with a flag set.
  template <auto M>
  [[nodiscard]] std::set<conversation_id> chats_where() const {
    return mux::chats_where<M>(state.root());
  }
  // A chat's own choice of a setting, else the nearest space's above it.
  template <auto M>
  [[nodiscard]] auto own_or_space(const conversation_id& chat) const {
    return this->reads().own_or_space<M>(chat);
  }
  [[nodiscard]] mux::config::bubble_look panels_of(const conversation_id& chat) {
    return this->reads().panels_of(chat);
  }
  [[nodiscard]] mux::config::bubble_look bubbles_of(const conversation_id& chat) {
    return this->reads().bubbles_of(chat);
  }
  // A chat's background: its own, else its account's, else every chat's,
  // else the theme's.
  [[nodiscard]] mux::config::wallpaper_t wallpaper_of(const conversation_id& chat) {
    return this->reads().wallpaper_of(chat);
  }
  // What notifies, as the model holds it; changed by its edits: one
  // setting in its place, by its member pointer, or all of them at once.
  [[nodiscard]] const mux::config::notification_settings& notifications() const { return state.root().notifications.fValue; }
  template <auto M, class T>
  void choose_notification(T now) {
    using Part = std::remove_cvref_t<decltype(std::declval<mux::config::notification_settings&>().*M)>;
    (void)state.apply(skiff::model::edit(skiff::model::placeOf<skiff::model::Field<M>, kept_root>(),
                                         skiff::model::setTo(Part(std::move(now)))));
  }
  // What the model's reactions asked for, to be done by the program.
  [[nodiscard]] kept_due take_due() { return std::exchange(due, {}); }
  void set_notifications(mux::config::notification_settings now) {
    (void)state.apply(skiff::model::edit(skiff::model::placeOf<mux::config::notification_settings, kept_root>(), skiff::model::setTo(std::move(now))));
  }
  // Why the accounts file could not be read, when it could not: then it is
  // not written over either.
  std::optional<std::string> config_error;
  // The demo: nothing kept.
  bool keeps_nothing = false;

  // An account saved, by its address.
  // The account's id, as the model knows it, of a saved one.
  [[nodiscard]] static mux::account_id id_of(const mux::config::account_t& account) {
    const std::string address = mux::config::address_of(account);
    return mux::account_id{mux::proto::protocol_of(address), address};
  }
  // The accounts saved, as the model holds them: by their addresses, in
  // the order they are listed.
  [[nodiscard]] const skiff::model::Keyed<std::string, mux::config::account_t>& accounts() const { return state.root().accounts; }
  [[nodiscard]] const mux::config::account_t* settings_of(std::string_view address) const {
    return this->accounts().find(std::string(address));
  }
  // An account changed: in its place, only where something did; under its
  // new address where that changed, where it was in the list.
  template <class F>
  void change_account(std::string_view address, F&& change) {
    const mux::config::account_t* had = this->settings_of(address);
    if (had == nullptr)
      return;
    mux::config::account_t next = *had;
    change(next);
    if (next == *had)
      return;
    const std::string now = mux::config::address_of(next);
    if (now == address) {
      (void)state.apply(skiff::model::edit(skiff::model::placeOf<mux::config::account_t, kept_root>(std::string(address)),
                                           skiff::model::setTo(std::move(next))));
      return;
    }
    const auto keys = this->accounts().keys();
    const auto at = static_cast<std::size_t>(std::ranges::distance(keys.begin(), std::ranges::find(keys, address)));
    (void)state.applyBatch(skiff::model::take<mux::config::account_t>(std::string(address)),
                           skiff::model::over<skiff::model::Keyed<std::string, mux::config::account_t>>(
                               skiff::model::Put<std::string, mux::config::account_t>{now, std::move(next), at}));
  }
  // An account saved, at the end of the list: false where one has its
  // address already.
  bool add_account(mux::config::account_t account) {
    std::string address = mux::config::address_of(account);
    if (this->settings_of(address) != nullptr)
      return false;
    (void)state.apply(skiff::model::put<mux::config::account_t>(std::move(address), std::move(account)));
    return true;
  }
  // A chat's own choices there to be bound to: where it chose nothing yet,
  // an empty one -- left out of the file while it stays empty.
  void ensure_chat(const conversation_id& chat) {
    if (this->state.root().chats.find(chat) == nullptr)
      (void)state.apply(skiff::model::put<chat_choices>(chat, chat_choices{}));
  }
  bool remove_account(std::string_view address) {
    return this->settings_of(address) != nullptr && state.apply(skiff::model::take<mux::config::account_t>(std::string(address)));
  }
  // Whether a chat shows who has read up to where: its own choice, else its
  // account's, else every account's.
  // How far a jump's search pages back in a chat: its own limit, else its
  // account's, else every account's.
  [[nodiscard]] std::int64_t jump_search_of(const conversation_id& chat) {
    return this->reads().jump_search_of(chat);
  }
  // Whether a chat shows link previews: its own choice, else its account's,
  // else every account's.
  [[nodiscard]] bool previews_shown(const conversation_id& chat) {
    return this->reads().previews_shown(chat);
  }
  // Whether a chat's link previews come from the sites themselves: its own
  // choice, its space's, its account's, else every account's.
  [[nodiscard]] bool previews_direct(const conversation_id& chat) {
    return !proto::offers(state_before(chat.account.speaks), proto::feature::server_previews{}) || this->reads().previews_direct(chat);
  }
  // Whether others in a chat are told one is typing: its own choice, its
  // space's, its account's, else every account's.
  [[nodiscard]] bool typing_sent(const conversation_id& chat) {
    return this->reads().typing_sent(chat);
  }
  [[nodiscard]] bool receipts_shown(const conversation_id& chat) {
    return this->reads().receipts_shown(chat);
  }
  // Whether a chat shows what is done in it: its own choice, else its
  // account's, else every account's.
  [[nodiscard]] bool room_events_shown(const conversation_id& chat) {
    return this->reads().room_events_shown(chat);
  }

  // What a message coming to a chat notifies with: nothing where the chat
  // is muted or asks for mentions and it is none; else as its account
  // says, else as every account's.
  struct notify_decision {
    bool popup = false;
    bool sound = false;
  };
  // A chat's own notification choices -- or a space's -- as chosen for it:
  // muted is notifications off.
  [[nodiscard]] mux::config::notify_choices notify_choices_of(const conversation_id& chat) const {
    auto out = this->own_of<&chat_choices::notify>(chat);
    if (this->own_of<&chat_choices::muted>(chat))
      out.on = false;
    return out;
  }
  // A setting in effect for a chat: its own, else its space's (and theirs
  // up), else its account's, else every chat's.
  template <class Setting>
  [[nodiscard]] bool notify_value(const conversation_id& chat, Setting) {
    conversation_id at = chat;
    for (int steps = 0; steps < 17; ++steps) {
      if (const std::optional<bool> own = this->notify_choices_of(at).*Setting::chat)
        return *own;
      const auto up = space_above.find(at);
      if (up == space_above.end())
        break;
      at = up->second;
    }
    if (const mux::config::account_t* account = this->settings_of(chat.account.address))
      if (const std::optional<bool>& chosen = account->shared.*Setting::account)
        return *chosen;
    return Setting::of(this->notifications());
  }
  // A setting as a chat would have it with no choice of its own: its
  // space's (and theirs up), else its account's, else every chat's. Without
  // a chat, the account's would-be: every account's.
  template <class Setting>
  [[nodiscard]] bool inherited_notify(const mux::account_id& by, const std::optional<std::string>& room, Setting) {
    if (!room)
      return Setting::of(this->notifications());
    const conversation_id chat{by, *room};
    if (const auto up = space_above.find(chat); up != space_above.end())
      return this->notify_value(up->second, Setting{});
    if (const mux::config::account_t* account = this->settings_of(by.address))
      if (const std::optional<bool>& chosen = account->shared.*Setting::account)
        return *chosen;
    return Setting::of(this->notifications());
  }
  [[nodiscard]] notify_decision notify_for(const conversation_id& chat, bool mentions_me) {
    namespace setting = mux::config::notify_setting;
    if (!this->notify_value(chat, setting::on{}) || (this->notify_value(chat, setting::mentions{}) && !mentions_me))
      return {};
    return {.popup = true,
            .sound = this->notify_value(chat, setting::sound{})};
  }

  // Which room events a chat shows, kind by kind: its own choices, its
  // account's, every account's.
  [[nodiscard]] mux::room_event_filter room_event_filter_of(const conversation_id& chat) {
    return this->reads().room_event_filter_of(chat);
  }

  // The file as all of this says it.
  // The settings as the file keeps them, taken in: at the start, or once
  // local data is opened -- what file() writes, read back.
  void take(const mux::config::file& saved) {
    const auto chat_of = [](const std::string& account, const std::string& conversation) {
      return mux::conversation_id{{mux::proto::protocol_of(account), account}, conversation};
    };
    // Each chat's choices, and how the window looks, put together first,
    // then the model made of them.
    mux::config::look_settings looks_read;
    std::map<conversation_id, chat_choices> chats;
    const auto accounts_read = mux::config::accounts_of(saved);
    this->foreign_accounts = mux::config::foreign_of(saved);
    looks_read.motion = mux::config::motion_of(saved.motion);
    looks_read.enter = mux::config::enter_of(saved.enter);
    looks_read.layout = mux::config::layout_of(saved.layout);
    // The account shown last, shown again once it is in the model: accounts
    // arrive after the first frame, and the first one there is not the one.
    // The emoji picked lately, the stickers sent lately, and the favourites.
    const auto emotes_of = [](const std::optional<std::vector<mux::config::sticker_kept>>& kept) {
      return kept.value_or(std::vector<mux::config::sticker_kept>{}) | std::views::transform([](const mux::config::sticker_kept& one) {
               return mux::emote{.shortcode = one.shortcode, .url = one.url, .body = one.body, .w = one.w, .h = one.h, .size = one.size,
                                 .mimetype = one.mimetype};
             }) | std::ranges::to<std::vector>();
    };
    const recently_used recent_read{.last_account = saved.last_account,
                                    .emoji = saved.recent_emoji.value_or(std::vector<std::string>{}),
                                    .stickers = emotes_of(saved.recent_stickers),
                                    .favourite_stickers = emotes_of(saved.favourite_stickers),
                                    .favourite_emoji = emotes_of(saved.favourite_emoji)};
    looks_read.theme = mux::config::theme_of(saved.theme);
    looks_read.theme_overrides = saved.theme_overrides.value_or(std::map<std::string, mux::config::custom_theme>{});
    if (saved.wallpaper)
      looks_read.wallpaper = mux::config::wallpaper_of(std::string_view(*saved.wallpaper));
    if (saved.bubbles)
      looks_read.bubbles = mux::config::bubble_look_of(*saved.bubbles);
    if (saved.panels)
      looks_read.panels = mux::config::bubble_look_of(*saved.panels);
    looks_read.accent = mux::config::accent_of(saved.accent);
    looks_read.window_opacity = std::clamp(saved.window_opacity.value_or(100), 20, 100);
    looks_read.wallpaper_behind = saved.wallpaper_behind.value_or(false);
    looks_read.live_blur = saved.live_blur.value_or(false);
    looks_read.frost_blur = std::clamp(
        saved.frost.value_or(saved.frost_blur ? static_cast<double>(*saved.frost_blur) / 3.0 : 10.0),
        0.0, 100.0);
    looks_read.chat_avatar_rounding = saved.chat_avatar_rounding;
    looks_read.person_avatar_rounding = saved.person_avatar_rounding;
    looks_read.interface_scale = saved.interface_scale.value_or(100);
    const auto sending_read = saved.sending.value_or(mux::config::sending_settings{});
    const auto history_read = saved.history.value_or(mux::config::history_settings{});
    looks_read.style = saved.style.value_or(mux::config::style_settings{});
    const auto notifications_read = saved.notifications.value_or(mux::config::notification_settings{});
    for (const auto& one : saved.chat_notify.value_or(std::vector<mux::config::chat_notify>{}))
      chats[chat_of(one.account, one.conversation)].notify = mux::config::notify_choices{
              .on = one.on,
              .mentions = spl::visit(spl::overloaded{[](mux::config::notify_mode::mentions) { return std::optional<bool>(true); },
                                                           [](mux::config::notify_mode::all) { return std::optional<bool>(false); },
                                                           [](const auto&) { return std::optional<bool>(); }},
                                        one.mode.value_or(mux::config::notify_mode_t{mux::config::notify_mode::by_default{}})),
              .sound = one.sound};
    for (const auto& one : saved.room_events.value_or(std::vector<mux::config::room_events_choice>{})) {
      auto& chosen = chats[chat_of(one.account, one.conversation)];
      chosen.style = one.style;
      chosen.room_events = one.show;
      chosen.room_event_kinds = one.kinds;
      chosen.receipts = one.receipts;
      chosen.previews = one.previews;
      chosen.typing = one.typing;
      chosen.previews_direct = one.previews_direct;
      chosen.jump_search = one.jump_search;
      if (one.wallpaper)
        chosen.wallpaper = mux::config::wallpaper_of(std::string_view(*one.wallpaper));
      if (one.bubbles)
        chosen.bubbles = mux::config::bubble_look_of(*one.bubbles);
      if (one.panels)
        chosen.panels = mux::config::bubble_look_of(*one.panels);
    }
    const auto placements_read = saved.placements.value_or(std::vector<mux::config::chat_placement>{});
    for (const auto& one : saved.muted.value_or(std::vector<mux::config::muted_chat>{}))
      chats[chat_of(one.account, one.conversation)].muted = true;
    std::erase_if(chats, [](const auto& one) { return one.second == chat_choices{}; });
    kept_root root;
    root.accounts.putAll(accounts_read | std::views::transform([](const mux::config::account_t& one) {
                           return std::pair{mux::config::address_of(one), one};
                         }));
    root.chats.putAll(chats);
    root.notifications.fValue = notifications_read;
    root.sending.fValue = sending_read;
    root.history.fValue = history_read;
    root.looks.fValue = looks_read;
    root.placements.fValue = placements_read;
    root.recent.fValue = recent_read;
    this->state = kept_model(std::move(root), kept_reactions{&due});
    // Each plugin's own, as the file kept it under its name; what no plugin
    // turned on reads, kept to be written back as it was.
    plugins_unread_ = saved.plugins.value_or(std::map<std::string, knot::raw>{});
    this->read_plugins(mux::settings_plugins{});
  }
  template <class... Features>
  void read_plugins(mux::plugin_list<Features...>) {
    (this->read_plugin<Features>(), ...);
  }
  template <class Feature>
  void read_plugin() {
    using settings = mux::plugin_settings_t<Feature>;
    const auto found = plugins_unread_.find(std::string(Feature::name));
    if (found == plugins_unread_.end())
      return;
    if (auto read = knot::try_read<settings>(found->second.text))
      this->set_part(std::move(*read));
    plugins_unread_.erase(found);
  }
  template <class... Features>
  [[nodiscard]] std::map<std::string, knot::raw> plugins_said(mux::plugin_list<Features...>) const {
    auto out = plugins_unread_;
    (out.insert_or_assign(std::string(Features::name), knot::raw{knot::to_json_string(this->plugin_settings<mux::plugin_settings_t<Features>>())}), ...);
    return out;
  }
  // What the file kept for plugins not turned on now.
  std::map<std::string, knot::raw> plugins_unread_;
  [[nodiscard]] mux::config::file file() const {
    // Copied: the file is made of a contiguous list of them.
    const auto listed = this->accounts().values() | std::ranges::to<std::vector>();
    auto out = mux::config::file_of(listed, foreign_accounts);
    out.motion = mux::config::said_of<mux::config::motion_said_t>(this->appearance().motion);
    out.enter = mux::config::said_of<mux::config::enter_said_t>(this->appearance().enter);
    out.layout = mux::config::said_of<mux::config::layout_said_t>(this->appearance().layout);
    out.last_account = this->recent().last_account;
    if (!this->recent().emoji.empty())
      out.recent_emoji = this->recent().emoji;
    const auto kept_of = [](const std::vector<mux::emote>& all) {
      return all | std::views::transform([](const mux::emote& one) {
               return mux::config::sticker_kept{one.shortcode, one.url, one.body, one.w, one.h, one.size, one.mimetype};
             }) | std::ranges::to<std::vector>();
    };
    if (!this->recent().stickers.empty())
      out.recent_stickers = kept_of(this->recent().stickers);
    if (!this->recent().favourite_stickers.empty())
      out.favourite_stickers = kept_of(this->recent().favourite_stickers);
    if (!this->recent().favourite_emoji.empty())
      out.favourite_emoji = kept_of(this->recent().favourite_emoji);
    if (auto said = this->plugins_said(mux::settings_plugins{}); !said.empty())
      out.plugins = std::move(said);
    out.theme = mux::config::said_of<mux::config::theme_said_t>(this->appearance().theme);
    if (!this->appearance().theme_overrides.empty()) out.theme_overrides = this->appearance().theme_overrides;
    if (this->appearance().wallpaper)
      out.wallpaper = mux::config::word_of(*this->appearance().wallpaper);
    if (this->appearance().bubbles)
      out.bubbles = mux::config::word_of(*this->appearance().bubbles);
    if (this->appearance().panels)
      out.panels = mux::config::word_of(*this->appearance().panels);
    out.accent = mux::config::said_of<mux::config::accent_said_t>(this->appearance().accent);
    if (this->appearance().window_opacity != 100)
      out.window_opacity = this->appearance().window_opacity;
    if (this->appearance().wallpaper_behind)
      out.wallpaper_behind = true;
    if (this->appearance().live_blur)
      out.live_blur = true;
    if (this->appearance().frost_blur != 10.0)
      out.frost = this->appearance().frost_blur;
    out.frost_blur = std::nullopt;
    // The avatars' rounding, where it is set: it was never written, and
    // came back round at every start.
    out.chat_avatar_rounding = this->appearance().chat_avatar_rounding;
    out.person_avatar_rounding = this->appearance().person_avatar_rounding;
    if (this->appearance().interface_scale != 100)
      out.interface_scale = this->appearance().interface_scale;
    out.sending = this->sending();
    out.history = this->history();
    out.style = this->appearance().style;
    out.notifications = this->notifications();
    std::vector<mux::config::chat_notify> notify;
    std::vector<mux::config::room_events_choice> choices;
    std::vector<mux::config::muted_chat> muted;
    const auto& chats = state.root().chats;
    for (std::size_t i = 0; i < chats.size(); ++i) {
      const conversation_id& chat = chats.keyAt(i);
      const chat_choices& chosen = chats.valueAt(i);
      if (const auto& own = chosen.notify; own != mux::config::notify_choices{})
        notify.push_back({.account = chat.account.address,
                          .conversation = chat.id,
                          .mode = own.mentions.transform([](bool only) {
                            return only ? mux::config::notify_mode_t{mux::config::notify_mode::mentions{}}
                                        : mux::config::notify_mode_t{mux::config::notify_mode::all{}};
                          }),
                          .on = own.on,
                          .sound = own.sound});
      if (chosen.muted)
        muted.push_back({chat.account.address, chat.id});
      const mux::config::room_events_choice one{
          .account = chat.account.address,
          .conversation = chat.id,
          .show = chosen.room_events,
          .kinds = chosen.room_event_kinds,
          .receipts = chosen.receipts,
          .previews = chosen.previews,
          .previews_direct = chosen.previews_direct,
          .typing = chosen.typing,
          .jump_search = chosen.jump_search,
          .wallpaper = chosen.wallpaper.transform([](const auto& look) { return std::string(mux::config::word_of(look)); }),
          .bubbles = chosen.bubbles.transform([](const auto& look) { return std::string(mux::config::word_of(look)); }),
          .panels = chosen.panels.transform([](const auto& look) { return std::string(mux::config::word_of(look)); }),
          .style = chosen.style};
      if (one != mux::config::room_events_choice{.account = one.account, .conversation = one.conversation})
        choices.push_back(one);
    }
    if (!notify.empty())
      out.chat_notify = std::move(notify);
    if (!choices.empty())
      out.room_events = std::move(choices);
    if (!this->placements().empty())
      out.placements = this->placements();
    if (!muted.empty())
      out.muted = std::move(muted);
    return out;
  }
  // Written back: nothing, or why not.
  [[nodiscard]] std::optional<std::string> write() {
    if (keeps_nothing)
      return std::nullopt;
    if (config_error)
      return "Not saved: " + *config_error;
    if (auto done = mux::config::save(config_path, this->file(), *vault); !done)
      return "Not saved: " + done.error();
    return std::nullopt;
  }
};

}  // namespace mux::app

// Made once (src/app/model_kept.cc): an edit's end of batch -- the model's reactions and
// its change log, a walk over the whole root's type -- was made again in
// every unit that edited this model. In a named module, members defined in a
// class are not implicitly inline: the other units call it.
// (Its end of batch alone, not the whole class: clang 23 crashed making the
// class's constexpr constructor in an explicit instantiation of it.)
extern template void skiff::model::Model<mux::app::kept_root, mux::app::kept_reactions>::endBatch();
