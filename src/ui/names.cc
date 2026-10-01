// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:names -- How chats and people are named and told of: presence, names, times.
export module mux.ui:names;

import std;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
import mux.core;
import mux.config;
import :base;

export namespace mux::ui {

// What a presence says, in a word or two.
[[nodiscard]] inline std::string presence_text(const availability_t& state) {
  return splice::visit(splice::overloaded{[](const availability::online&) { return std::string("online"); },
                               [](const availability::chat&) { return std::string("online"); },
                               [](const availability::away&) { return std::string("away"); },
                               [](const availability::extended_away&) { return std::string("away for a while"); },
                               [](const availability::do_not_disturb&) { return std::string("busy"); },
                               [](const availability::offline&) { return std::string("offline"); }},
                    state);
}
// A contact's presence, in a word or two. One never heard of is offline on
// XMPP, whose roster says who is there; on Matrix it is nothing, as servers
// may keep presence off, and "offline" would then be said of everyone.
[[nodiscard]] inline std::string presence_of(const model& now, const account_id& account, const std::string& contact) {
  const auto unknown = [&] {
    return splice::visit(splice::overloaded{[](const protocol::xmpp&) { return std::string("offline"); },
                                 [](const protocol::matrix&) { return std::string(); }},
                      account.speaks);
  };
  const auto found = now.accounts().find(account);
  if (found == now.accounts().end())
    return unknown();
  const auto kept = found->second.presences.find(contact);
  return kept == found->second.presences.end() ? unknown() : presence_text(kept->second.state);
}
// Whom a direct chat is with: on XMPP, its address; on Matrix it is a room,
// so the member who is not the account itself.
[[nodiscard]] inline std::string contact_of(const conversation& one) {
  return splice::visit(splice::overloaded{[&](const protocol::xmpp&) { return one.id.id; },
                               [&](const protocol::matrix&) {
                                 const auto other = std::ranges::find_if(
                                     one.members, [&](const member& each) { return each.id != one.id.account.address; });
                                 return other == one.members.end() ? std::string() : other->id;
                               }},
                    one.id.account.speaks);
}

[[nodiscard]] inline bool is_group(const conversation& one) {
  return splice::visit(splice::overloaded{[](const conversation_kind::direct&) { return false; }, [](const auto&) { return true; }},
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
  const auto hidden = [](std::string_view character) {
    const auto at = [&](std::size_t i) { return static_cast<std::uint32_t>(static_cast<unsigned char>(character[i])); };
    const std::uint32_t code =
        character.size() == 1   ? at(0)
        : character.size() == 2 ? ((at(0) & 0x1F) << 6) | (at(1) & 0x3F)
        : character.size() == 3 ? ((at(0) & 0x0F) << 12) | ((at(1) & 0x3F) << 6) | (at(2) & 0x3F)
                                : ((at(0) & 0x07) << 18) | ((at(1) & 0x3F) << 12) | ((at(2) & 0x3F) << 6) | (at(3) & 0x3F);
    return code < 0x20 || code == 0x7F || (code >= 0x80 && code < 0xA0) || code == 0xAD || (code >= 0x200B && code <= 0x200F) ||
           (code >= 0x202A && code <= 0x202E) || (code >= 0x2060 && code <= 0x2069) || code == 0xFEFF;
  };
  return name | std::views::chunk_by([](char, char next) { return (static_cast<unsigned char>(next) & 0xC0) == 0x80; }) |
         std::views::transform([](auto&& each) { return std::string_view(each.begin(), each.end()); }) |
         std::views::filter([&](std::string_view each) { return !hidden(each); }) | std::views::join |
         std::ranges::to<std::string>();
}
// What someone is called in a chat, before telling them apart: their name
// there, shown plainly, or their ID's local part.
[[nodiscard]] inline std::string local_part(std::string_view who) {
  if (who.starts_with('@'))
    who.remove_prefix(1);
  return std::string(who.substr(0, who.find_first_of("@:")));
}
[[nodiscard]] inline std::string called(const member& one) {
  std::string name = shown_plainly(one.name);
  return name.empty() ? local_part(one.id) : name;
}
[[nodiscard]] inline std::string called(const conversation& in, std::string_view who) {
  const auto found = std::ranges::find(in.members, who, &member::id);
  return found != in.members.end() ? called(*found) : local_part(who);
}
// What a sender is called in a chat -- with their whole ID after it where
// someone else there is called the same, in any case: a display name, or a
// local part on another server, is anyone's to take, and "Alice" written by
// someone else looked like Alice's (as Element tells them apart).
[[nodiscard]] inline std::string sender_name(const conversation& in, std::string_view sender) {
  const std::string name = called(in, sender);
  const auto folded = [](std::string_view text) {
    return text | std::views::transform([](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }) |
           std::ranges::to<std::string>();
  };
  const std::string mine = folded(name);
  const bool shared = std::ranges::any_of(in.members, [&](const member& one) {
    return one.id != sender && folded(called(one)) == mine;
  });
  return shared ? std::format("{} ({})", name, sender) : name;
}

// A time of day, as the clock on the wall says it.
[[nodiscard]] inline std::string clock_of(std::chrono::sys_time<std::chrono::milliseconds> at) {
  const std::time_t t =
      std::chrono::system_clock::to_time_t(std::chrono::time_point_cast<std::chrono::system_clock::duration>(at));
  const std::tm* local = std::localtime(&t);
  return local ? std::format("{:02}:{:02}", local->tm_hour, local->tm_min) : std::string();
}

}  // namespace mux::ui
