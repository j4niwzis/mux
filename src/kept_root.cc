// SPDX-License-Identifier: AGPL-3.0-only
// mux.kept_root: what the accounts file keeps, as the settings model holds
// it -- the root the program edits and the window's parts read through
// their binding (mux.app.kept owns the model and writes the file).
export module mux.kept_root;

import std;
import skiff.model;
import mux.core;
import mux.config;
import mux.logic.room_events;

export namespace mux {

// What was used lately, for the next start: the account shown last, by its
// address; the emoji picked lately, newest first; the stickers sent lately,
// and the favourites.
struct recently_used {
  std::optional<std::string> last_account;
  std::vector<std::string> emoji;
  std::vector<mux::emote> stickers, favourite_stickers, favourite_emoji;
  friend bool operator==(const recently_used&, const recently_used&) = default;
};

// What is kept, as the model holds it: each chat's own choices, by the chat.
struct kept_root {
  // The accounts saved, by their addresses, in the order they are listed.
  skiff::model::Keyed<std::string, mux::config::account_t> accounts;
  skiff::model::Keyed<conversation_id, mux::config::chat_choices> chats;
  // What notifies, and how.
  skiff::model::Tracked<mux::config::notification_settings> notifications;
  // What is done to a picture dropped before it is sent.
  skiff::model::Tracked<mux::config::sending_settings> sending;
  // How frames are drawn.
  skiff::model::Tracked<mux::config::frame_settings> frames;
  // What is kept and shown of the history, for every chat that does not say.
  skiff::model::Tracked<mux::config::history_settings> history;
  // How much is kept, in memory and on disk.
  skiff::model::Tracked<mux::config::cache_limits> limits;
  // How the window looks.
  skiff::model::Tracked<mux::config::look_settings> looks;
  // The proxy profiles, in their order.
  skiff::model::Tracked<std::vector<mux::config::proxy_settings>> proxies;
  // The chats listed in other accounts' lists than their own.
  skiff::model::Tracked<std::vector<mux::config::chat_placement>> placements;
  skiff::model::Tracked<recently_used> recent;
};
// The chats whose own choices have a flag set.
template <auto M>
[[nodiscard]] std::set<conversation_id> chats_where(const kept_root& root) {
  return std::views::zip(root.chats.keys(), root.chats.values()) |
         std::views::filter([](const auto& one) { return static_cast<bool>(std::get<1>(one).*M); }) |
         std::views::transform([](const auto& one) { return std::get<0>(one); }) | std::ranges::to<std::set>();
}
// An account's settings, by its address: none where it is not kept.
[[nodiscard]] inline const config::account_t* account_settings(const kept_root& root, std::string_view address) {
  return root.accounts.find(std::string(address));
}

// The space each chat is in, by the chat, as the chats say: what a chat
// has not chosen, its space's choice says.
[[nodiscard]] inline std::map<conversation_id, conversation_id> space_above_of(const skiff::model::Keyed<account_id, account>& accounts) {
  // Each space's own rooms -- not itself -- by the room, the first space
  // that holds one kept.
  const auto rooms_of = [](const conversation& space) {
    return space.children | std::views::filter([&space](const std::string& child) { return child != space.id.id; }) |
           std::views::transform([&space](const std::string& child) { return std::pair{conversation_id{space.id.account, child}, space.id}; });
  };
  return accounts.values() | std::views::transform([](const account& one) { return one.conversations.values(); }) | std::views::join |
         std::views::filter(&conversation::space) | std::views::transform(rooms_of) | std::views::join | std::ranges::to<std::map>();
}

// What a chat's settings come to, read from what is kept and the spaces
// the chats are in: its own choice, else its space's (and theirs up), its
// account's, every chat's. What the program and the window both read.
struct chat_settings {
  const kept_root* root = nullptr;
  const std::map<conversation_id, conversation_id>* space_above = nullptr;

  [[nodiscard]] const config::look_settings& appearance() const { return root->looks.fValue; }
  [[nodiscard]] const config::history_settings& history() const { return root->history.fValue; }
  [[nodiscard]] const config::account_t* settings_of(std::string_view address) const { return account_settings(*root, address); }
  // A chat's own choice of one setting, by its member pointer; as unsaid
  // where it chose nothing.
  template <auto M>
  [[nodiscard]] auto own_of(const conversation_id& chat) const {
    using T = std::remove_cvref_t<decltype(std::declval<const config::chat_choices&>().*M)>;
    const config::chat_choices* found = root->chats.find(chat);
    return found == nullptr ? T{} : found->*M;
  }
  // A chat's own choice of a setting, else the nearest space's above it.
  template <auto M>
  [[nodiscard]] auto own_or_space(const conversation_id& chat) const {
    conversation_id at = chat;
    for (int steps = 0; steps < 17; ++steps) {
      if (auto own = this->own_of<M>(at))
        return own;
      const auto up = space_above->find(at);
      if (up == space_above->end())
        break;
      at = up->second;
    }
    return decltype(this->own_of<M>(chat)){};
  }
  [[nodiscard]] config::style_settings style_of(const std::optional<account_id>& account,
                                               const std::optional<conversation_id>& chat) const {
    config::style_settings out;
    if (chat) {
      auto at = *chat;
      for (int steps = 0; steps < 17; ++steps) {
        out = config::filled_from(std::move(out), own_of<&config::chat_choices::style>(at));
        const auto up = space_above->find(at);
        if (up == space_above->end()) break;
        at = up->second;
      }
    }
    const auto owner = chat ? std::optional(chat->account) : account;
    if (owner)
      if (const auto* saved = settings_of(owner->address))
        out = config::filled_from(std::move(out), saved->shared.style);
    auto every = appearance().style;
    every.theme = appearance().theme;
    out = config::filled_from(std::move(out), every);
    if (!out.theme) out.theme = appearance().theme;
    return out;
  }
  // A chat's look: the lowest level's that has one -- its own or its
  // space's, its account's, every chat's -- what it leaves unsaid taken
  // from the levels over it, in turn.
  template <auto In>
  [[nodiscard]] config::bubble_look look_of(const std::optional<std::string>& account_word, const std::optional<config::bubble_look>& everywhere,
                                            const conversation_id& chat) const {
    std::vector<config::bubble_look> levels;
    if (const auto own = this->own_or_space<In>(chat))
      levels.push_back(*own);
    if (account_word)
      levels.push_back(config::bubble_look_of(*account_word));
    if (everywhere)
      levels.push_back(*everywhere);
    if (levels.empty())
      return config::bubble_look{};
    return std::ranges::fold_left(std::views::drop(levels, 1), levels.front(), [](config::bubble_look below, const config::bubble_look& above) {
      return config::filled_from(std::move(below), above);
    });
  }
  [[nodiscard]] config::bubble_look panels_of(const conversation_id& chat) const {
    const auto* account = this->settings_of(chat.account.address);
    return this->look_of<&config::chat_choices::panels>(account ? config::panels_of(*account) : std::nullopt, this->appearance().panels, chat);
  }
  [[nodiscard]] config::bubble_look bubbles_of(const conversation_id& chat) const {
    const auto* account = this->settings_of(chat.account.address);
    return this->look_of<&config::chat_choices::bubbles>(account ? config::bubbles_of(*account) : std::nullopt, this->appearance().bubbles, chat);
  }
  // A chat's background: its own, else its account's, else every chat's,
  // else the theme's.
  [[nodiscard]] config::wallpaper_t wallpaper_of(const conversation_id& chat) const {
    if (const auto own = this->own_or_space<&config::chat_choices::wallpaper>(chat))
      return *own;
    if (const auto* account = this->settings_of(chat.account.address))
      if (const auto& chosen = config::wallpaper_of(*account))
        return config::wallpaper_of(std::string_view(*chosen));
    return this->appearance().wallpaper.value_or(config::wallpaper_t{config::wallpaper::theme{}});
  }
  // A flag in effect for a chat: its own or its space's, else its
  // account's (as Of reads it), else every account's.
  template <auto M, class Of, class Everyone>
  [[nodiscard]] auto in_effect(const conversation_id& chat, Of of, Everyone everyone) const {
    if (const auto own = this->own_or_space<M>(chat))
      return *own;
    if (const auto* account = this->settings_of(chat.account.address))
      if (const auto& chosen = of(*account))
        return *chosen;
    return everyone;
  }
  // How far a jump's search pages back in a chat.
  [[nodiscard]] std::int64_t jump_search_of(const conversation_id& chat) const {
    return this->in_effect<&config::chat_choices::jump_search>(chat, [](const config::account_t& a) { return config::jump_search_of(a); },
                                                               this->history().jump_search);
  }
  // Whether a chat shows link previews.
  [[nodiscard]] bool previews_shown(const conversation_id& chat) const {
    return this->in_effect<&config::chat_choices::previews>(chat, [](const config::account_t& a) { return config::link_previews_of(a); },
                                                            this->history().link_previews);
  }
  // Whether a chat's link previews come from the sites themselves.
  [[nodiscard]] bool previews_direct(const conversation_id& chat) const {
    return this->in_effect<&config::chat_choices::previews_direct>(
        chat, [](const config::account_t& a) { return config::previews_direct_of(a); }, this->history().previews_direct.value_or(false));
  }
  // Whether others in a chat are told one is typing.
  [[nodiscard]] bool typing_sent(const conversation_id& chat) const {
    return this->in_effect<&config::chat_choices::typing>(chat, [](const config::account_t& a) { return config::send_typing_of(a); },
                                                          this->history().send_typing.value_or(true));
  }
  // Whether a chat shows who has read up to where.
  [[nodiscard]] bool receipts_shown(const conversation_id& chat) const {
    return this->in_effect<&config::chat_choices::receipts>(chat, [](const config::account_t& a) { return config::show_receipts_of(a); },
                                                            this->history().show_receipts);
  }
  // Whether a chat shows what is done in it.
  [[nodiscard]] bool room_events_shown(const conversation_id& chat) const {
    return this->in_effect<&config::chat_choices::room_events>(chat, [](const config::account_t& a) { return config::room_events_of(a); },
                                                               this->history().show_room_events);
  }
  // Which room events a chat shows, kind by kind: its own choices, its
  // account's, every account's.
  [[nodiscard]] room_event_filter room_event_filter_of(const conversation_id& chat) const {
    const config::account_t* account = this->settings_of(chat.account.address);
    return logic::filter_of(this->own_or_space<&config::chat_choices::room_event_kinds>(chat),
                            this->own_or_space<&config::chat_choices::room_events>(chat),
                            account ? config::room_event_kinds_of(*account) : std::nullopt,
                            account ? config::room_events_of(*account) : std::nullopt, this->history().room_event_kinds,
                            this->history().show_room_events);
  }
};

}  // namespace mux
