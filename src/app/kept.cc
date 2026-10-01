// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.kept: what the accounts file keeps for the program -- the
// accounts, the proxy profiles, the chats muted, the theme, the renderer,
// how much moves, how much is kept, what is done to files sent -- and its
// writing back. Held apart from what the program does with it: the parts
// that change it write it.
export module mux.app.kept;

import std;
import splice;
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
  // The stickers sent lately, and the favourites.
  std::vector<mux::emote> recent_stickers, favourite_stickers;
  // The theme and the renderer, for the next start.
  mux::config::theme_t theme = mux::config::theme::tinted{};
  mux::config::accent_t accent = mux::config::accent::theme_own{};
  mux::config::renderer_t renderer = mux::config::renderer::opengl{};
  // Read by the host at each frame: only the damage repainted; and it
  // outlined.
  bool partial_redraw = false;
  bool flash_redraws = false;
  bool vsync = true;
  int window_opacity = 100;
  bool wallpaper_behind = false;
  bool live_blur = false;
  double frost_blur = 10.0;
  // The space bars: whether there are any, whether the top one is, and
  // where each item is put, in order.
  bool spaces = true;
  bool top_bar = true;
  // Home without what spaces hold -- but direct messages -- for every
  // account that does not say.
  bool home_hides_spaced = false;
  bool home_hides_direct = false;  // and direct messages, where that is so
  std::vector<mux::config::space_placed> space_places;
  bool show_fps = false;
  // How much is kept, in memory and on disk.
  mux::config::cache_limits limits;
  // What is done to a picture dropped before it is sent.
  mux::config::sending_settings sending;
  // What is kept of the history: deleted messages, or not.
  mux::config::history_settings history;
  // The chats muted, and the proxy profiles.
  std::set<conversation_id> muted;
  // The chats listed in other accounts' lists than their own.
  std::vector<mux::config::chat_placement> placements;
  // The chats that chose for themselves whether their room events show.
  std::map<conversation_id, bool> room_events;
  // Chats' own choice of showing who has read up to where.
  std::map<conversation_id, bool> receipts_shown_in;
  // Chats' own choice of link previews.
  std::map<conversation_id, bool> previews_shown_in;
  // Chats' (and spaces') own choice of where link previews come from.
  std::map<conversation_id, bool> previews_direct_in;
  // Chats' (and spaces') own choice of telling others one is typing.
  std::map<conversation_id, bool> typing_sent_in;
  // Every chat's background, and chats' own.
  std::optional<mux::config::wallpaper_t> wallpaper;
  std::map<conversation_id, mux::config::wallpaper_t> wallpaper_in;
  // Every chat's bubbles, and chats' own.
  std::optional<mux::config::bubble_look> bubbles;
  std::map<conversation_id, mux::config::bubble_look> bubbles_in;
  // Every chat's panels, and chats' own.
  std::optional<mux::config::bubble_look> panels;
  std::map<conversation_id, mux::config::bubble_look> panels_in;
  // The spaces shown as one chat each, their rooms in them as topics.
  std::set<conversation_id> forums;
  // Spaces whose rooms Home leaves out: each one's own choice, none by default.
  std::set<conversation_id> hidden_from_home;
  // The space each chat is in -- the first found holding it -- set by the
  // program from the model at each refresh: a space's own choices are its
  // rooms', where they have none, nearest first, through spaces in spaces.
  std::map<conversation_id, conversation_id> space_above;
  // A chat's own entry in a map of chats' choices, else the nearest space's
  // above it; none, the map's end.
  template <class Map>
  [[nodiscard]] auto own_or_space(const Map& in, const conversation_id& chat) const -> decltype(in.find(chat)) {
    auto own = in.find(chat);
    conversation_id at = chat;
    for (int steps = 0; own == in.end() && steps < 16; ++steps) {
      const auto up = space_above.find(at);
      if (up == space_above.end())
        break;
      at = up->second;
      own = in.find(at);
    }
    return own;
  }
  // A chat's look: the lowest level's that has one -- its own or its
  // space's, its account's, every chat's -- what it leaves unsaid taken
  // from the levels over it, in turn.
  [[nodiscard]] mux::config::bubble_look look_of(std::map<conversation_id, mux::config::bubble_look>& in,
                                                 const std::optional<std::string>& account_word,
                                                 const std::optional<mux::config::bubble_look>& everywhere,
                                                 const conversation_id& chat) {
    std::vector<mux::config::bubble_look> levels;
    if (const auto own = this->own_or_space(in, chat); own != in.end())
      levels.push_back(own->second);
    if (account_word)
      levels.push_back(mux::config::bubble_look_of(*account_word));
    if (everywhere)
      levels.push_back(*everywhere);
    if (levels.empty())
      return mux::config::bubble_look{};
    return std::ranges::fold_left(levels | std::views::drop(1), levels.front(),
                                  [](mux::config::bubble_look below, const mux::config::bubble_look& above) {
                                    return mux::config::filled_from(std::move(below), above);
                                  });
  }
  [[nodiscard]] mux::config::bubble_look panels_of(const conversation_id& chat) {
    const auto* account = this->settings_of(chat.account.address);
    return this->look_of(panels_in, account ? mux::config::panels_of(*account) : std::nullopt, panels, chat);
  }
  [[nodiscard]] mux::config::bubble_look bubbles_of(const conversation_id& chat) {
    const auto* account = this->settings_of(chat.account.address);
    return this->look_of(bubbles_in, account ? mux::config::bubbles_of(*account) : std::nullopt, bubbles, chat);
  }
  // A chat's background: its own, else its account's, else every chat's,
  // else the theme's.
  [[nodiscard]] mux::config::wallpaper_t wallpaper_of(const conversation_id& chat) {
    if (const auto own = this->own_or_space(wallpaper_in, chat); own != wallpaper_in.end())
      return own->second;
    if (const auto* account = this->settings_of(chat.account.address))
      if (const auto& chosen = mux::config::wallpaper_of(*account))
        return mux::config::wallpaper_of(std::string_view(*chosen));
    return wallpaper.value_or(mux::config::wallpaper_t{mux::config::wallpaper::theme{}});
  }
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
    if (const auto own = this->own_or_space(jump_search_in, chat); own != jump_search_in.end())
      return own->second;
    if (const auto* account = this->settings_of(chat.account.address))
      if (const auto& chosen = mux::config::jump_search_of(*account))
        return *chosen;
    return history.jump_search;
  }
  // Whether a chat shows link previews: its own choice, else its account's,
  // else every account's.
  [[nodiscard]] bool previews_shown(const conversation_id& chat) {
    if (const auto own = this->own_or_space(previews_shown_in, chat); own != previews_shown_in.end())
      return own->second;
    if (const auto* account = this->settings_of(chat.account.address))
      if (const auto& chosen = mux::config::link_previews_of(*account))
        return *chosen;
    return history.link_previews;
  }
  // Whether a chat's link previews come from the sites themselves: its own
  // choice, its space's, its account's, else every account's.
  [[nodiscard]] bool previews_direct(const conversation_id& chat) {
    if (const auto own = this->own_or_space(previews_direct_in, chat); own != previews_direct_in.end())
      return own->second;
    if (const auto* account = this->settings_of(chat.account.address))
      if (const auto& chosen = mux::config::previews_direct_of(*account))
        return *chosen;
    return history.previews_direct;
  }
  // Whether others in a chat are told one is typing: its own choice, its
  // space's, its account's, else every account's.
  [[nodiscard]] bool typing_sent(const conversation_id& chat) {
    if (const auto own = this->own_or_space(typing_sent_in, chat); own != typing_sent_in.end())
      return own->second;
    if (const auto* account = this->settings_of(chat.account.address))
      if (const auto& chosen = mux::config::send_typing_of(*account))
        return *chosen;
    return history.send_typing;
  }
  [[nodiscard]] bool receipts_shown(const conversation_id& chat) {
    if (const auto own = this->own_or_space(receipts_shown_in, chat); own != receipts_shown_in.end())
      return own->second;
    if (const auto* account = this->settings_of(chat.account.address))
      if (const auto& chosen = mux::config::show_receipts_of(*account))
        return *chosen;
    return history.show_receipts;
  }
  // Whether a chat shows what is done in it: its own choice, else its
  // account's, else every account's.
  [[nodiscard]] bool room_events_shown(const conversation_id& chat) {
    if (const auto own = this->own_or_space(room_events, chat); own != room_events.end())
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
    const bool wanted = splice::visit(splice::overloaded{[](mux::config::notify_mode::off) { return false; },
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
    const auto own_all = this->own_or_space(room_events, chat);
    const auto own_kinds = this->own_or_space(room_event_kinds, chat);
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
    const auto kept_of = [](const std::vector<mux::emote>& all) {
      return all | std::views::transform([](const mux::emote& one) {
               return mux::config::sticker_kept{one.shortcode, one.url, one.body, one.w, one.h, one.size, one.mimetype};
             }) |
             std::ranges::to<std::vector>();
    };
    if (!recent_stickers.empty())
      out.recent_stickers = kept_of(recent_stickers);
    if (!favourite_stickers.empty())
      out.favourite_stickers = kept_of(favourite_stickers);
    if (!proxies.empty())
      out.proxies = proxies;
    out.theme = mux::config::word_of(theme);
    if (wallpaper)
      out.wallpaper = mux::config::word_of(*wallpaper);
    if (bubbles)
      out.bubbles = mux::config::word_of(*bubbles);
    if (panels)
      out.panels = mux::config::word_of(*panels);
    out.accent = mux::config::word_of(accent);
    out.renderer = mux::config::word_of(renderer);
    if (partial_redraw)
      out.partial_redraw = true;
    if (flash_redraws)
      out.flash_redraws = true;
    if (!vsync)
      out.vsync = false;
    if (window_opacity != 100)
      out.window_opacity = window_opacity;
    if (wallpaper_behind)
      out.wallpaper_behind = true;
    if (live_blur)
      out.live_blur = true;
    if (frost_blur != 10.0)
      out.frost = frost_blur;
    out.frost_blur = std::nullopt;
    if (!spaces)
      out.spaces = false;
    if (!top_bar)
      out.top_bar = false;
    if (home_hides_spaced)
      out.home_hides_spaced = true;
    if (home_hides_direct)
      out.home_hides_direct = true;
    if (!space_places.empty())
      out.space_places = space_places | std::views::transform([](const mux::config::space_placed& one) {
                           return mux::config::space_place{one.account, mux::config::word_of(one.item),
                                                           std::string(mux::config::word_of(one.bar))};
                         }) |
                         std::ranges::to<std::vector>();
    if (show_fps)
      out.show_fps = true;
    out.cache = limits;
    out.sending = sending;
    out.history = history;
    out.notifications = notifications;
    if (!notify_modes.empty()) {
      out.chat_notify.emplace();
      for (const auto& [chat, mode] : notify_modes)
        out.chat_notify->push_back({chat.account.address, chat.id, mux::config::word_of(mode)});
    }
    if (!room_events.empty() || !room_event_kinds.empty() || !receipts_shown_in.empty() || !jump_search_in.empty() ||
        !previews_shown_in.empty() || !typing_sent_in.empty() || !previews_direct_in.empty() || !wallpaper_in.empty() || !bubbles_in.empty() ||
        !panels_in.empty() || !forums.empty() || !hidden_from_home.empty()) {
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
      for (const auto& [chat, direct] : previews_direct_in) {
        auto& one = chosen[chat];
        one.account = chat.account.address;
        one.conversation = chat.id;
        one.previews_direct = direct;
      }
      for (const auto& [chat, send] : typing_sent_in) {
        auto& one = chosen[chat];
        one.account = chat.account.address;
        one.conversation = chat.id;
        one.typing = send;
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
      for (const auto& [chat, chosen_wallpaper] : wallpaper_in) {
        auto& one = chosen[chat];
        one.account = chat.account.address;
        one.conversation = chat.id;
        one.wallpaper = mux::config::word_of(chosen_wallpaper);
      }
      for (const auto& [chat, look] : bubbles_in) {
        auto& one = chosen[chat];
        one.account = chat.account.address;
        one.conversation = chat.id;
        one.bubbles = mux::config::word_of(look);
      }
      for (const auto& [chat, look] : panels_in) {
        auto& one = chosen[chat];
        one.account = chat.account.address;
        one.conversation = chat.id;
        one.panels = mux::config::word_of(look);
      }
      for (const conversation_id& chat : forums) {
        auto& one = chosen[chat];
        one.account = chat.account.address;
        one.conversation = chat.id;
        one.forum = true;
      }
      for (const conversation_id& chat : hidden_from_home) {
        auto& one = chosen[chat];
        one.account = chat.account.address;
        one.conversation = chat.id;
        one.hide_from_home = true;
      }
      out.room_events.emplace();
      for (auto& [chat, one] : chosen)
        out.room_events->push_back(std::move(one));
    }
    if (!placements.empty())
      out.placements = placements;
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
