// SPDX-License-Identifier: AGPL-3.0-only
// mux.config:config_accounts -- An account's settings read and set, the passphrase's purposes, and accounts to and from the file.
export module mux.config:config_accounts;

import std;
import splice;
import knot;
import mux.vault;
import mux.proto.kept;
import :config_choices;
import :config_settings;
import :config_looks;
import :config_limits;

export namespace mux::config {
// What an account is known by: what its protocol says -- a JID, a user ID.
[[nodiscard]] inline const std::string& address_of(const account_t& one) noexcept {
  return spl::visit([](const auto& each) -> const std::string& { return address_of(each); }, one.own);
}

[[nodiscard]] inline bool& enabled_of(account_t& one) noexcept {
  return one.shared.enabled;
}
[[nodiscard]] inline bool enabled_of(const account_t& one) noexcept {
  return one.shared.enabled;
}

// Whether an account sends read receipts, and the proxy it goes through.
[[nodiscard]] inline bool read_receipts_of(const account_t& one) {
  return one.shared.read_receipts.value_or(true);
}
// Whether an account sends room keys to verified sessions only: where its
// protocol keeps that (only_verified_in(kept), by ADL); none for another.
namespace only_verified_defaults {
template <class Kept>
[[nodiscard]] auto only_verified_in(Kept&) -> std::conditional_t<std::is_const_v<Kept>, const std::optional<bool>*, std::optional<bool>*> {
  return nullptr;
}
}  // namespace only_verified_defaults
[[nodiscard]] inline std::optional<bool>* only_verified_in(account_t& one) {
  return spl::visit([](auto& each) -> std::optional<bool>* {
    using only_verified_defaults::only_verified_in;
    return only_verified_in(each);
  }, one.own);
}
[[nodiscard]] inline bool only_verified_of(const account_t& one) {
  const std::optional<bool>* kept = spl::visit([](const auto& each) -> const std::optional<bool>* {
    using only_verified_defaults::only_verified_in;
    return only_verified_in(each);
  }, one.own);
  return kept && kept->value_or(false);
}
// Whether an account shares the mentions read with its other sessions, and
// seals them there: where its protocol keeps that (mentions_shared_in and
// mentions_sealed_in(kept), by ADL); none for another.
namespace mentions_defaults {
template <class Kept>
[[nodiscard]] auto mentions_shared_in(Kept&) -> std::conditional_t<std::is_const_v<Kept>, const std::optional<bool>*, std::optional<bool>*> {
  return nullptr;
}
template <class Kept>
[[nodiscard]] auto mentions_sealed_in(Kept&) -> std::conditional_t<std::is_const_v<Kept>, const std::optional<bool>*, std::optional<bool>*> {
  return nullptr;
}
}  // namespace mentions_defaults
[[nodiscard]] inline std::optional<bool>* mentions_shared_in(account_t& one) {
  return spl::visit([](auto& each) -> std::optional<bool>* {
    using mentions_defaults::mentions_shared_in;
    return mentions_shared_in(each);
  }, one.own);
}
[[nodiscard]] inline std::optional<bool>* mentions_sealed_in(account_t& one) {
  return spl::visit([](auto& each) -> std::optional<bool>* {
    using mentions_defaults::mentions_sealed_in;
    return mentions_sealed_in(each);
  }, one.own);
}
// As they are now; none where the account's protocol cannot share them.
struct mentions_choice {
  bool shared = false;
  bool sealed = false;
  friend bool operator==(const mentions_choice&, const mentions_choice&) = default;
};
[[nodiscard]] inline std::optional<mentions_choice> mentions_choice_of(const account_t& one) {
  return spl::visit([](const auto& each) -> std::optional<mentions_choice> {
    using mentions_defaults::mentions_shared_in;
    using mentions_defaults::mentions_sealed_in;
    const std::optional<bool>* shared = mentions_shared_in(each);
    const std::optional<bool>* sealed = mentions_sealed_in(each);
    if (!shared || !sealed)
      return std::nullopt;
    return mentions_choice{shared->value_or(false), sealed->value_or(false)};
  }, one.own);
}
[[nodiscard]] inline std::optional<bool>& read_receipts_in(account_t& one) {
  return one.shared.read_receipts;
}
// An account's colour: its own choice, else one of the eight its address
// picks -- the same every time, and accounts apart mostly apart.
[[nodiscard]] inline accent_t default_colour_of(std::string_view address) {
  std::uint32_t hash = 2166136261u;
  for (const char c : address) {
    hash ^= static_cast<unsigned char>(c);
    hash *= 16777619u;
  }
  switch (hash % 8) {
    case 0: return accent::blue{};
    case 1: return accent::green{};
    case 2: return accent::pink{};
    case 3: return accent::orange{};
    case 4: return accent::purple{};
    case 5: return accent::red{};
    case 6: return accent::grey{};
    default: return accent::gold{};
  }
}
[[nodiscard]] inline accent_t colour_of(const account_t& one) {
  const std::optional<std::string>& word = one.shared.colour;
  return word ? accent_of(word) : default_colour_of(address_of(one));
}
[[nodiscard]] inline std::optional<std::string>& colour_in(account_t& one) {
  return one.shared.colour;
}
[[nodiscard]] inline bool strip_of(const account_t& one) {
  return one.shared.strip.value_or(true);
}
[[nodiscard]] inline std::optional<bool>& strip_in(account_t& one) {
  return one.shared.strip;
}
// Whether the account tells whom it talks to that the user is typing.
// What a passphrase is asked for, of the client's own: local data opened at
// the start; encrypted, a new one twice; another one, the one now first; or
// encryption turned off, the one now. A protocol's own purposes are its
// (passphrases(state)); proto::passphrase_for_t is them all.
namespace passphrase_for {
struct unlock {
  friend bool operator==(unlock, unlock) = default;
};
struct encrypt {
  friend bool operator==(encrypt, encrypt) = default;
};
struct change {
  friend bool operator==(change, change) = default;
};
struct decrypt {
  friend bool operator==(decrypt, decrypt) = default;
};
}  // namespace passphrase_for
// A new passphrase: not empty, long enough, the same twice -- or why not.
// Its key is made slowly (Argon2id, 64 MiB), but a short passphrase is still
// few guesses away for whoever has the files.
[[nodiscard]] inline std::optional<std::string> new_passphrase_refused(const std::string& fresh, const std::string& again) {
  if (fresh.empty())
    return "Type a passphrase.";
  if (std::ranges::distance(std::views::filter(fresh, [](char c) { return (c & 0xC0) != 0x80; })) < 10)
    return "A passphrase of at least 10 characters.";
  if (fresh != again)
    return "The new passphrase is not the same twice.";
  return std::nullopt;
}

// Its own choice, if it made one; else as every account's.
[[nodiscard]] inline const std::optional<bool>& send_typing_of(const account_t& one) {
  return one.shared.send_typing;
}
[[nodiscard]] inline std::optional<bool>& send_typing_in(account_t& one) {
  return one.shared.send_typing;
}
// Whether the account's chats show their room events: its own choice, if
// it made one.
[[nodiscard]] inline const std::optional<std::int64_t>& jump_search_of(const account_t& one) {
  return one.shared.jump_search;
}
[[nodiscard]] inline std::optional<std::int64_t>& jump_search_in(account_t& one) {
  return one.shared.jump_search;
}
// An account's chats' background, as word_of(wallpaper_t) says it.
[[nodiscard]] inline const std::optional<std::string>& wallpaper_of(const account_t& one) {
  return one.shared.wallpaper;
}
[[nodiscard]] inline std::optional<std::string>& wallpaper_in(account_t& one) {
  return one.shared.wallpaper;
}
[[nodiscard]] inline const std::optional<std::string>& bubbles_of(const account_t& one) {
  return one.shared.bubbles;
}
[[nodiscard]] inline std::optional<std::string>& bubbles_in(account_t& one) {
  return one.shared.bubbles;
}
[[nodiscard]] inline const std::optional<std::string>& panels_of(const account_t& one) {
  return one.shared.panels;
}
[[nodiscard]] inline std::optional<std::string>& panels_in(account_t& one) {
  return one.shared.panels;
}
[[nodiscard]] inline const std::optional<bool>& home_hides_of(const account_t& one) {
  return one.shared.home_hides_spaced;
}
[[nodiscard]] inline const std::optional<bool>& home_direct_of(const account_t& one) {
  return one.shared.home_hides_direct;
}
[[nodiscard]] inline std::optional<bool>& home_direct_in(account_t& one) {
  return one.shared.home_hides_direct;
}
[[nodiscard]] inline std::optional<bool>& home_hides_in(account_t& one) {
  return one.shared.home_hides_spaced;
}
[[nodiscard]] inline const std::optional<bool>& link_previews_of(const account_t& one) {
  return one.shared.link_previews;
}
[[nodiscard]] inline std::optional<bool>& link_previews_in(account_t& one) {
  return one.shared.link_previews;
}
// Whether its chats' link previews come from the sites themselves.
[[nodiscard]] inline const std::optional<bool>& previews_direct_of(const account_t& one) {
  return one.shared.previews_direct;
}
[[nodiscard]] inline std::optional<bool>& previews_direct_in(account_t& one) {
  return one.shared.previews_direct;
}
[[nodiscard]] inline const std::optional<bool>& show_receipts_of(const account_t& one) {
  return one.shared.show_receipts;
}
[[nodiscard]] inline std::optional<bool>& show_receipts_in(account_t& one) {
  return one.shared.show_receipts;
}
[[nodiscard]] inline const std::optional<bool>& room_events_of(const account_t& one) {
  return one.shared.room_events;
}
[[nodiscard]] inline std::optional<bool>& room_events_in(account_t& one) {
  return one.shared.room_events;
}
[[nodiscard]] inline const std::optional<bool>& notify_of(const account_t& one) {
  return one.shared.notify;
}
[[nodiscard]] inline std::optional<bool>& notify_in(account_t& one) {
  return one.shared.notify;
}
[[nodiscard]] inline const std::optional<bool>& notify_sound_of(const account_t& one) {
  return one.shared.notify_sound;
}
[[nodiscard]] inline std::optional<bool>& notify_sound_in(account_t& one) {
  return one.shared.notify_sound;
}
[[nodiscard]] inline const std::optional<room_event_kinds>& room_event_kinds_of(const account_t& one) {
  return one.shared.room_event_kinds;
}
[[nodiscard]] inline std::optional<room_event_kinds>& room_event_kinds_in(account_t& one) {
  return one.shared.room_event_kinds;
}
[[nodiscard]] inline std::optional<std::string>& proxy_in(account_t& one) {
  return one.shared.proxy;
}
[[nodiscard]] inline const std::optional<std::string>& proxy_of(const account_t& one) {
  return one.shared.proxy;
}
// The profile of that name, where there is one.
[[nodiscard]] inline const proxy_settings* find_proxy(const std::vector<proxy_settings>& all,
                                                      const std::optional<std::string>& name) {
  if (!name)
    return nullptr;
  const auto found = std::ranges::find(all, *name, &proxy_settings::name);
  return found == all.end() ? nullptr : &*found;
}

// The name of an account's protocol, as the user reads it.
[[nodiscard]] inline std::string_view protocol_name(const account_t& one) noexcept {
  return spl::visit([](const auto& each) { return protocol_name(each); }, one.own);
}

constexpr bool is_matrix(std::string_view address) noexcept { return address.starts_with('@'); }

// An account from an address alone, as on the command line: the first
// protocol of the list that owns the address -- its owns_address(state, ...).
template <class... Tags>
[[nodiscard]] account_t account_from(protocol_list<Tags...>, std::string address, std::string password) {
  std::optional<account_t> made;
  (void)((owns_address(state_of<Tags>{}, address) ? (made = account_t{.own = kept_t{kept_from(state_of<Tags>{}, address, password)}}, true) : false) ||
         ...);
  return made.value_or(account_t{});
}
[[nodiscard]] inline account_t account_from(std::string address, std::string password) {
  return account_from(protocols{}, std::move(address), std::move(password));
}

// The accounts of a file as the program holds them, and back.
[[nodiscard]] inline std::optional<account_t> account_of(const saved_account& one) {
  return spl::visit(spl::overloaded{[](const knot::value&) { return std::optional<account_t>(); },
                                          [&](const auto& own) {
                                            return std::optional<account_t>(account_t{.own = kept_t{own}, .shared = one.shared});
                                          }},
                       one.own.data());
}
[[nodiscard]] inline saved_account saved_of(const account_t& one) {
  return spl::visit([&](const auto& own) {
    return saved_account{.protocol = std::string(protocol_word(own)), .own = kept_saved_t{own}, .shared = one.shared};
  }, one.own);
}
// What an old file kept flat in each account, as every account keeps it now.
// All the accounts of a file of a protocol this build has (an old file's
// lists among them, read into accounts as load() reads it).
[[nodiscard]] inline std::vector<account_t> accounts_of(const file& from) {
  std::vector<account_t> out;
  for (const auto& one : from.accounts.value_or(std::vector<saved_account>{}))
    if (auto held = account_of(one))
      out.push_back(std::move(*held));
  return out;
}
// The accounts of a protocol this build has not: kept to be written back.
[[nodiscard]] inline std::vector<saved_account> foreign_of(const file& from) {
  return std::ranges::to<std::vector>(std::views::filter(from.accounts.value_or(std::vector<saved_account>{}), [](const saved_account& one) { return !account_of(one).has_value(); }));
}
[[nodiscard]] inline file file_of(std::span<const account_t> accounts, std::span<const saved_account> foreign = {}) {
  file out;
  out.accounts = std::ranges::to<std::vector>(std::views::transform(accounts, [](const account_t& one) { return saved_of(one); }));
  out.accounts->append_range(foreign);
  return out;
}

// What is wrong with an account as typed: its protocol's check(kept), by ADL.
std::optional<std::string> check(const account_t& one) {
  return spl::visit([](const auto& each) { return check(each); }, one.own);
}

// Where what the program keeps between runs, and could make again, is put:
// $XDG_STATE_HOME/mux, or ~/.local/state/mux.
std::filesystem::path state_path(std::string_view name) {
  if (const char* xdg = std::getenv("XDG_STATE_HOME"); xdg && *xdg)
    return std::filesystem::path(xdg) / "mux" / name;
  if (const char* home = std::getenv("HOME"); home && *home)
    return std::filesystem::path(home) / ".local" / "state" / "mux" / name;
  return std::filesystem::path(std::format("mux-{}", name));
}

}  // namespace mux::config
