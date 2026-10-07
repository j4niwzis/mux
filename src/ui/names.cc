// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:names -- How chats and people are named and told of: presence, names, times.
export module mux.ui:names;

import std;
import mux.logic.text;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
import mux.core;
import mux.config;
import mux.protocols;
import :base;

export namespace mux::ui {

// When something was last seen, as a date and a minute: 03.10.2026 14:05.
[[nodiscard]] inline std::string seen_at(std::chrono::sys_time<std::chrono::milliseconds> at) {
  return std::format("{:%d.%m.%Y %H:%M}", std::chrono::floor<std::chrono::minutes>(at));
}

// What a presence says, in a word or two.
[[nodiscard]] inline std::string presence_text(const availability_t& state) {
  return spl::visit(spl::overloaded{[](const availability::online&) { return std::string("online"); },
                               [](const availability::chat&) { return std::string("online"); },
                               [](const availability::away&) { return std::string("away"); },
                               [](const availability::extended_away&) { return std::string("away for a while"); },
                               [](const availability::do_not_disturb&) { return std::string("busy"); },
                               [](const availability::offline&) { return std::string("offline"); }},
                    state);
}
// A contact's presence, in a word or two; one never heard of, as their
// protocol says (proto::unheard_presence).
[[nodiscard]] inline std::string presence_of(const ui_shared& shared, const model& now, const account_id& account, const std::string& contact) {
  const auto unknown = [&] { return proto::unheard_presence(protocol_state_of(shared, account)); };
  const auto found = now.accounts().find(account);
  if (found == now.accounts().end())
    return unknown();
  const auto kept = found->second.presences.find(contact);
  return kept == found->second.presences.end() ? unknown() : presence_text(kept->second.state);
}
// Whom a direct chat is with, as its protocol says (proto::direct_contact).
[[nodiscard]] inline std::string contact_of(const ui_shared& shared, const conversation& one) {
  return proto::direct_contact(protocol_state_of(shared, one.id.account), one);
}

[[nodiscard]] inline bool is_group(const conversation& one) {
  return spl::visit(spl::overloaded{[](const conversation_kind::direct&) { return false; }, [](const auto&) { return true; }},
                    one.kind);
}

[[nodiscard]] inline std::string display_name(const conversation& one) {
  return one.name.empty() ? one.id.id : one.name;
}

// The name someone goes by in a conversation: as a member of it, or their
// address's local part.
// A name as it may be shown: without the characters that turn text around
// or hide in it -- bidi overrides and isolates, zero-width ones, the soft
// hyphen, controls. "Alice\u202Eecila" or "Ali\u200Bce" otherwise passed for
// someone else.
[[nodiscard]] inline std::string shown_plainly(std::string_view name) {
  const auto code_of = [](std::string_view character) {
    const auto at = [&](std::size_t i) { return static_cast<std::uint32_t>(static_cast<unsigned char>(character[i])); };
    return character.size() == 1   ? at(0)
           : character.size() == 2 ? ((at(0) & 0x1F) << 6) | (at(1) & 0x3F)
           : character.size() == 3 ? ((at(0) & 0x0F) << 12) | ((at(1) & 0x3F) << 6) | (at(2) & 0x3F)
                                   : ((at(0) & 0x07) << 18) | ((at(1) & 0x3F) << 12) | ((at(2) & 0x3F) << 6) | (at(3) & 0x3F);
  };
  const auto hidden = [](std::uint32_t code) {
    return code < 0x20 || code == 0x7F || (code >= 0x80 && code < 0xA0) || code == 0xAD || (code >= 0x200B && code <= 0x200F) ||
           (code >= 0x202A && code <= 0x202E) || (code >= 0x2060 && code <= 0x2069) || code == 0xFEFF;
  };
  // Part of an emoji: a pictograph (skin tones among them), or the
  // variation selector that makes one of a symbol.
  const auto pictured = [](std::uint32_t code) {
    return code >= 0x1F000 || (code >= 0x2600 && code <= 0x27BF) || code == 0xFE0F;
  };
  const auto characters = std::ranges::to<std::vector>(std::views::transform(std::views::chunk_by(name, [](char, char next) { return (static_cast<unsigned char>(next) & 0xC0) == 0x80; }), [](auto&& each) { return std::string_view(each.begin(), each.end()); }));
  const auto codes = std::ranges::to<std::vector>(std::views::transform(characters, code_of));
  // A zero-width joiner between two emoji makes them one (a family, a
  // rainbow flag): kept there, and stripped anywhere else, as before.
  const auto kept = [&](std::size_t i) {
    if (codes[i] == 0x200D)
      return i > 0 && i + 1 < codes.size() && pictured(codes[i - 1]) && pictured(codes[i + 1]);
    return !hidden(codes[i]);
  };
  return std::ranges::to<std::string>(std::views::join(std::views::transform(std::views::filter(std::views::iota(std::size_t{0}, characters.size()), kept), [&](std::size_t i) { return characters[i]; })));
}
// What someone is called in a chat, before telling them apart: their name
// there, shown plainly, or their ID's local part.
[[nodiscard]] inline std::string local_part(std::string_view who) {
  return proto::local_part_of(state_before(protocol_of(who)), who);
}
[[nodiscard]] inline std::string called(const member& one) {
  std::string name = shown_plainly(one.name);
  return name.empty() ? local_part(one.id) : name;
}
// What a chat's members are called, made once for each version of its
// member list (members_revision): each one's name by their ID, and how many
// go by each name, folded. Asked for every message shown, it walked the
// whole list for the sender and again, folding every name, to see who else
// shared it -- in a room of thousands, millions of strings at each switch,
// on the UI's thread.
struct member_names {
  bool built = false;
  std::uint64_t revision = 0;
  std::unordered_map<std::string, std::string> called;
  std::unordered_map<std::string, std::size_t> folded_count;
  // Where each member is in the chat's list, by their ID: one found
  // without a walk of it -- a sender's picture, a forward's.
  std::unordered_map<std::string, std::size_t> at;
};
[[nodiscard]] inline const member_names& names_of(const conversation& in) {
  // The UI's thread alone asks; one entry for each chat shown.
  static std::map<conversation_id, member_names> kept;
  member_names& names = kept[in.id];
  if (!names.built || names.revision != in.members_revision) {
    names.built = true;
    names.revision = in.members_revision;
    names.called.clear();
    names.folded_count.clear();
    names.at.clear();
    for (const member& one : in.members) {
      names.at.insert_or_assign(one.id, static_cast<std::size_t>(&one - in.members.data()));
      std::string name = called(one);
      ++names.folded_count[mux::logic::folded(name)];
      names.called.insert_or_assign(one.id, std::move(name));
    }
  }
  return names;
}
[[nodiscard]] inline std::string called(const conversation& in, std::string_view who) {
  const member_names& names = names_of(in);
  const auto found = names.called.find(std::string(who));
  return found != names.called.end() ? found->second : local_part(who);
}
// What a sender is called in a chat -- with their whole ID after it where
// someone else there is called the same, in any case: a display name, or a
// local part on another server, is anyone's to take, and "Alice" written by
// someone else looked like Alice's (as Element tells them apart).
[[nodiscard]] inline std::string sender_name(const conversation& in, std::string_view sender) {
  const member_names& names = names_of(in);
  const auto found = names.called.find(std::string(sender));
  const bool member = found != names.called.end();
  const std::string name = member ? found->second : local_part(sender);
  // How many others go by the same name: all of them, less the sender where
  // the sender is one of them.
  const auto same = names.folded_count.find(mux::logic::folded(name));
  const std::size_t others = same == names.folded_count.end() ? 0 : same->second - (member ? 1 : 0);
  return others > 0 ? std::format("{} ({})", name, sender) : name;
}

// A time of day, as the clock on the wall says it.
[[nodiscard]] inline std::string clock_of(std::chrono::sys_time<std::chrono::milliseconds> at) {
  const std::time_t t =
      std::chrono::system_clock::to_time_t(std::chrono::time_point_cast<std::chrono::system_clock::duration>(at));
  const std::tm* local = std::localtime(&t);
  return local ? std::format("{:02}:{:02}", local->tm_hour, local->tm_min) : std::string();
}

}  // namespace mux::ui
