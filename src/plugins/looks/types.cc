// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.looks.types -- What the looks plugin names, and what it says it
// provides: its pickers on the pages a level's looks are chosen on, its own
// settings -- what each level chose -- the looks in effect for a chat, and a
// part of the program. Light: mux.ui sees it.
export module mux.plugin.looks.types;

import std;
import splice;
import knot;
import mux.core;
import mux.config;
import mux.composition;

export namespace mux::plugins::looks {

struct feature {
  static constexpr std::string_view name = "looks";
};

consteval bool provides(contract::level_sections, type_tag<feature>) { return true; }
consteval bool provides(contract::program_parts, type_tag<feature>) { return true; }

// Its own settings (contract::settings_kept), kept in the settings file
// under its name: what each level chose -- every chat, each account by its
// address, each chat -- in words (word_of: a background's, a look's), read
// into their types where they are used (held_of).
struct words {
  std::optional<std::string> wallpaper;
  std::optional<std::string> bubbles;
  std::optional<std::string> panels;
  friend bool operator==(const words&, const words&) = default;
  friend consteval auto json_schema(knot::type<words>) { return knot::schema<words>(); }
};
struct chat_words {
  std::string account;       // the account's address
  std::string conversation;  // the chat's id in it
  words look;
  friend bool operator==(const chat_words&, const chat_words&) = default;
  friend consteval auto json_schema(knot::type<chat_words>) { return knot::schema<chat_words>(); }
};
struct kept {
  words everywhere;
  std::map<std::string, words> accounts;
  std::vector<chat_words> chats;
  friend bool operator==(const kept&, const kept&) = default;
  friend consteval auto json_schema(knot::type<kept>) { return knot::schema<kept>(); }
};
consteval bool provides(contract::settings_kept, type_tag<feature>) { return true; }
constexpr type_tag<kept> settings_type(contract::settings_kept, type_tag<feature>) { return {}; }

// A level's words, read into the looks they say.
[[nodiscard]] inline config::looks_held held_of(const words& one) {
  return {one.wallpaper.transform([](const std::string& word) { return config::wallpaper_of(std::string_view(word)); }),
          one.bubbles.transform([](const std::string& word) { return config::bubble_look_of(word); }),
          one.panels.transform([](const std::string& word) { return config::bubble_look_of(word); })};
}
// What an account's chats, or a chat, chose: none where nothing.
[[nodiscard]] inline const words* account_in(const kept& mine, const std::string& address) {
  const auto found = mine.accounts.find(address);
  return found == mine.accounts.end() ? nullptr : &found->second;
}
[[nodiscard]] inline const words* chat_in(const kept& mine, const conversation_id& chat) {
  const auto found = std::ranges::find_if(mine.chats, [&](const chat_words& one) {
    return one.account == chat.account.address && one.conversation == chat.id;
  });
  return found == mine.chats.end() ? nullptr : &found->look;
}
// A chat's words to change: made where it has none yet.
[[nodiscard]] inline words& chat_at(kept& mine, const conversation_id& chat) {
  const auto found = std::ranges::find_if(mine.chats, [&](const chat_words& one) {
    return one.account == chat.account.address && one.conversation == chat.id;
  });
  if (found != mine.chats.end())
    return found->look;
  return mine.chats.emplace_back(chat_words{chat.account.address, chat.id, {}}).look;
}

// The looks of the chats (contract::chat_looks).
consteval bool provides(contract::chat_looks, type_tag<feature>) { return true; }
// What a level holds: every chat's; the account's or the chat's own, of
// the chat settings are for.
[[nodiscard]] inline config::looks_held chat_looks_held(contract::chat_looks, type_tag<feature>, const kept& mine, const choice_level_t& level,
                                                        const std::optional<conversation_id>& managed) {
  const words* held = spl::visit(spl::overloaded{[&](choice_level::everywhere) { return &mine.everywhere; },
                                                 [&](choice_level::account) { return managed ? account_in(mine, managed->account.address) : nullptr; },
                                                 [&](choice_level::chat) { return managed ? chat_in(mine, *managed) : nullptr; }},
                                 level);
  return held ? held_of(*held) : config::looks_held{};
}
// A chat's looks in effect -- none: every chat's. Each the lowest level's
// that says it: its own or its space's (and theirs up), its account's, every
// chat's; a look's unsaid parts filled from the levels over it, in turn.
[[nodiscard]] inline config::looks_in_effect chat_looks_in_effect(contract::chat_looks, type_tag<feature>, const kept& mine,
                                                                 const std::map<conversation_id, conversation_id>& space_above,
                                                                 const std::optional<conversation_id>& chat) {
  // The chat's own and its spaces', nearest first.
  std::vector<config::looks_held> own;
  if (chat) {
    conversation_id at = *chat;
    for (int steps = 0; steps < 17; ++steps) {
      if (const words* said = chat_in(mine, at))
        own.push_back(held_of(*said));
      const auto up = space_above.find(at);
      if (up == space_above.end())
        break;
      at = up->second;
    }
  }
  const words* by_account = chat ? account_in(mine, chat->account.address) : nullptr;
  const config::looks_held account = by_account ? held_of(*by_account) : config::looks_held{};
  const config::looks_held every = held_of(mine.everywhere);
  const auto nearest = [&](auto field) {
    const auto found = std::ranges::find_if(own, [&](const config::looks_held& one) { return (one.*field).has_value(); });
    return found == own.end() ? std::remove_cvref_t<decltype(account.*field)>{} : (*found).*field;
  };
  const auto look = [&](std::optional<config::bubble_look> config::looks_held::* field) {
    const auto said = std::array{nearest(field), account.*field, every.*field} |
                      std::views::filter([](const std::optional<config::bubble_look>& one) { return one.has_value(); }) |
                      std::views::transform([](const std::optional<config::bubble_look>& one) { return *one; }) | std::ranges::to<std::vector>();
    if (said.empty())
      return config::bubble_look{};
    return std::ranges::fold_left(said | std::views::drop(1), said.front(), [](config::bubble_look below, const config::bubble_look& above) {
      return config::filled_from(std::move(below), above);
    });
  };
  return {.wallpaper = nearest(&config::looks_held::wallpaper)
                           .or_else([&] { return account.wallpaper; })
                           .or_else([&] { return every.wallpaper; })
                           .value_or(config::wallpaper_t{config::wallpaper::theme{}}),
          .bubbles = look(&config::looks_held::bubbles),
          .panels = look(&config::looks_held::panels)};
}

}  // namespace mux::plugins::looks
