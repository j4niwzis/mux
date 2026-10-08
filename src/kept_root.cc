// SPDX-License-Identifier: AGPL-3.0-only
// mux.kept_root: what the accounts file keeps, as the settings model holds
// it -- the root the program edits and the window's parts read through
// their binding (mux.app.kept owns the model and writes the file).
export module mux.kept_root;

import std;
import skiff.model;
import mux.core;
import mux.config;

export namespace mux {

// What was used lately, for the next start: the account shown last, by its
// address; the emoji picked lately, newest first; the stickers sent lately,
// and the favourites.
struct recently_used {
  std::optional<std::string> last_account;
  std::vector<std::string> emoji;
  std::vector<mux::emote> stickers, favourite_stickers;
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

}  // namespace mux
