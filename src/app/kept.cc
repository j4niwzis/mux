// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.kept: what the accounts file keeps for the program -- the
// accounts, the proxy profiles, the chats muted, the theme, the renderer,
// how much moves, how much is kept, what is done to files sent -- and its
// writing back. Held apart from what the program does with it: the parts
// that change it write it.
export module mux.app.kept;

import std;
import mux.core;
import mux.config;
import mux.logic.room_events;

export namespace mux::app {

struct kept_settings {
  std::filesystem::path config_path;
  std::vector<mux::config::account_t> saved;
  // How much moves, as read, to be written back as it was.
  std::optional<std::string> motion;
  // The account shown last, by its address, for the next start.
  std::optional<std::string> last_account;
  // The emoji picked lately, newest first.
  std::vector<std::string> recent_emoji;
  // The theme and the renderer, for the next start.
  mux::config::theme_t theme = mux::config::theme::tinted{};
  mux::config::accent_t accent = mux::config::accent::theme_own{};
  mux::config::renderer_t renderer = mux::config::renderer::opengl{};
  // How much is kept, in memory and on disk.
  mux::config::cache_limits limits;
  // What is done to a picture dropped before it is sent.
  mux::config::sending_settings sending;
  // What is kept of the history: deleted messages, or not.
  mux::config::history_settings history;
  // The chats muted, and the proxy profiles.
  std::set<conversation_id> muted;
  // The chats that chose for themselves whether their room events show.
  std::map<conversation_id, bool> room_events;
  // Chats' own choice of showing who has read up to where.
  std::map<conversation_id, bool> receipts_shown_in;
  // Chats' own choice of link previews.
  std::map<conversation_id, bool> previews_shown_in;
  // Chats' own limit on a jump's search, in events; 0 no limit.
  std::map<conversation_id, std::int64_t> jump_search_in;
  // And each kind of them, where a chat chose apart.
  std::map<conversation_id, mux::config::room_event_kinds> room_event_kinds;
  // What notifies, and the chats that chose everything or mentions alone
  // (a muted chat is in `muted`).
  mux::config::notification_settings notifications;
  std::map<conversation_id, mux::config::notify_mode_t> notify_modes;
  std::vector<mux::config::proxy_settings> proxies;
  // Why the accounts file could not be read, when it could not: then it is
  // not written over either.
  std::optional<std::string> config_error;
  // The demo: nothing kept.
  bool keeps_nothing = false;

  // An account saved, by its address.
  [[nodiscard]] std::vector<mux::config::account_t>::iterator find(std::string_view address) {
    return std::ranges::find(saved, address,
                             [](const auto& one) -> std::string_view { return mux::config::address_of(one); });
  }
  [[nodiscard]] const mux::config::account_t* settings_of(std::string_view address) {
    const auto found = this->find(address);
    return found == saved.end() ? nullptr : &*found;
  }
  // Whether a chat shows who has read up to where: its own choice, else its
  // account's, else every account's.
  // How far a jump's search pages back in a chat: its own limit, else its
  // account's, else every account's.
  [[nodiscard]] std::int64_t jump_search_of(const conversation_id& chat) {
    if (const auto own = jump_search_in.find(chat); own != jump_search_in.end())
      return own->second;
    if (const auto* account = this->settings_of(chat.account.address))
      if (const auto& chosen = mux::config::jump_search_of(*account))
        return *chosen;
    return history.jump_search;
  }
  // Whether a chat shows link previews: its own choice, else its account's,
  // else every account's.
  [[nodiscard]] bool previews_shown(const conversation_id& chat) {
    if (const auto own = previews_shown_in.find(chat); own != previews_shown_in.end())
      return own->second;
    if (const auto* account = this->settings_of(chat.account.address))
      if (const auto& chosen = mux::config::link_previews_of(*account))
        return *chosen;
    return history.link_previews;
  }
  [[nodiscard]] bool receipts_shown(const conversation_id& chat) {
    if (const auto own = receipts_shown_in.find(chat); own != receipts_shown_in.end())
      return own->second;
    if (const auto* account = this->settings_of(chat.account.address))
      if (const auto& chosen = mux::config::show_receipts_of(*account))
        return *chosen;
    return history.show_receipts;
  }
  // Whether a chat shows what is done in it: its own choice, else its
  // account's, else every account's.
  [[nodiscard]] bool room_events_shown(const conversation_id& chat) {
    if (const auto own = room_events.find(chat); own != room_events.end())
      return own->second;
    if (const auto* account = this->settings_of(chat.account.address))
      if (const auto& chosen = mux::config::room_events_of(*account))
        return *chosen;
    return history.show_room_events;
  }

  // What a message coming to a chat notifies with: nothing where the chat
  // is muted or asks for mentions and it is none; else as its account
  // says, else as every account's.
  struct notify_decision {
    bool popup = false;
    bool sound = false;
  };
  [[nodiscard]] mux::config::notify_mode_t notify_mode_of(const conversation_id& chat) const {
    if (muted.contains(chat))
      return mux::config::notify_mode::off{};
    const auto own = notify_modes.find(chat);
    return own == notify_modes.end() ? mux::config::notify_mode_t{mux::config::notify_mode::by_default{}} : own->second;
  }
  [[nodiscard]] notify_decision notify_for(const conversation_id& chat, bool mentions_me) {
    const bool wanted = std::visit(mux::overloaded{[](mux::config::notify_mode::off) { return false; },
                                                   [&](mux::config::notify_mode::mentions) { return mentions_me; },
                                                   [](const auto&) { return true; }},
                                   this->notify_mode_of(chat));
    if (!wanted)
      return {};
    const mux::config::account_t* account = this->settings_of(chat.account.address);
    return {account ? mux::config::notify_of(*account).value_or(notifications.desktop) : notifications.desktop,
            account ? mux::config::notify_sound_of(*account).value_or(notifications.sound) : notifications.sound};
  }

  // Which room events a chat shows, kind by kind: its own choices, its
  // account's, every account's.
  [[nodiscard]] mux::room_event_filter room_event_filter_of(const conversation_id& chat) {
    const mux::config::account_t* account = this->settings_of(chat.account.address);
    const auto own_all = room_events.find(chat);
    const auto own_kinds = room_event_kinds.find(chat);
    return mux::logic::filter_of(
        own_kinds == room_event_kinds.end() ? std::nullopt : std::optional<mux::config::room_event_kinds>(own_kinds->second),
        own_all == room_events.end() ? std::nullopt : std::optional<bool>(own_all->second),
        account ? mux::config::room_event_kinds_of(*account) : std::nullopt,
        account ? mux::config::room_events_of(*account) : std::nullopt, history.room_event_kinds,
        history.show_room_events);
  }

  // The file as all of this says it.
  [[nodiscard]] mux::config::file file() const {
    auto out = mux::config::file_of(saved);
    out.motion = motion;
    out.last_account = last_account;
    if (!recent_emoji.empty())
      out.recent_emoji = recent_emoji;
    if (!proxies.empty())
      out.proxies = proxies;
    out.theme = mux::config::word_of(theme);
    out.accent = mux::config::word_of(accent);
    out.renderer = mux::config::word_of(renderer);
    out.cache = limits;
    out.sending = sending;
    out.history = history;
    out.notifications = notifications;
    if (!notify_modes.empty()) {
      out.chat_notify.emplace();
      for (const auto& [chat, mode] : notify_modes)
        out.chat_notify->push_back({chat.account.address, chat.id, mux::config::word_of(mode)});
    }
    if (!room_events.empty() || !room_event_kinds.empty() || !receipts_shown_in.empty() || !jump_search_in.empty() || !previews_shown_in.empty()) {
      std::map<conversation_id, mux::config::room_events_choice> chosen;
      for (const auto& [chat, show] : room_events) {
        auto& one = chosen[chat];
        one.account = chat.account.address;
        one.conversation = chat.id;
        one.show = show;
      }
      for (const auto& [chat, kinds] : room_event_kinds) {
        auto& one = chosen[chat];
        one.account = chat.account.address;
        one.conversation = chat.id;
        one.kinds = kinds;
      }
      for (const auto& [chat, show] : previews_shown_in) {
        auto& one = chosen[chat];
        one.account = chat.account.address;
        one.conversation = chat.id;
        one.previews = show;
      }
      for (const auto& [chat, most] : jump_search_in) {
        auto& one = chosen[chat];
        one.account = chat.account.address;
        one.conversation = chat.id;
        one.jump_search = most;
      }
      for (const auto& [chat, show] : receipts_shown_in) {
        auto& one = chosen[chat];
        one.account = chat.account.address;
        one.conversation = chat.id;
        one.receipts = show;
      }
      out.room_events.emplace();
      for (auto& [chat, one] : chosen)
        out.room_events->push_back(std::move(one));
    }
    if (!muted.empty()) {
      std::vector<mux::config::muted_chat> kept;
      for (const auto& one : muted)
        kept.push_back({one.account.address, one.id});
      out.muted = std::move(kept);
    }
    return out;
  }
  // Written back: nothing, or why not.
  [[nodiscard]] std::optional<std::string> write() const {
    if (keeps_nothing)
      return std::nullopt;
    if (config_error)
      return "Not saved: " + *config_error;
    if (auto done = mux::config::save(config_path, this->file()); !done)
      return "Not saved: " + done.error();
    return std::nullopt;
  }
};

}  // namespace mux::app
