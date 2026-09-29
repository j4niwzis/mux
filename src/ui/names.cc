// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:names -- How chats and people are named and told of: presence, names, times.
export module mux.ui:names;

import std;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes;
import skiff.widgets;
import mux.core;
import mux.config;
export import :themes;

export namespace mux::ui {

// What a presence says, in a word or two.
[[nodiscard]] inline std::string presence_text(const availability_t& state) {
  return std::visit(overloaded{[](const availability::online&) { return std::string("online"); },
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
    return std::visit(overloaded{[](const protocol::xmpp&) { return std::string("offline"); },
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
  return std::visit(overloaded{[&](const protocol::xmpp&) { return one.id.id; },
                               [&](const protocol::matrix&) {
                                 const auto other = std::ranges::find_if(
                                     one.members, [&](const member& each) { return each.id != one.id.account.address; });
                                 return other == one.members.end() ? std::string() : other->id;
                               }},
                    one.id.account.speaks);
}

[[nodiscard]] inline bool is_group(const conversation& one) {
  return std::visit(overloaded{[](const conversation_kind::direct&) { return false; }, [](const auto&) { return true; }},
                    one.kind);
}

[[nodiscard]] inline std::string display_name(const conversation& one) {
  return one.name.empty() ? one.id.id : one.name;
}

// The name someone goes by in a conversation: as a member of it, or their
// address's local part.
[[nodiscard]] inline std::string sender_name(const conversation& in, std::string_view sender) {
  for (const member& one : in.members)
    if (one.id == sender && !one.name.empty())
      return one.name;
  if (sender.starts_with('@'))
    sender.remove_prefix(1);
  return std::string(sender.substr(0, sender.find_first_of("@:")));
}

// A time of day, as the clock on the wall says it.
[[nodiscard]] inline std::string clock_of(std::chrono::sys_time<std::chrono::milliseconds> at) {
  const std::time_t t =
      std::chrono::system_clock::to_time_t(std::chrono::time_point_cast<std::chrono::system_clock::duration>(at));
  const std::tm* local = std::localtime(&t);
  return local ? std::format("{:02}:{:02}", local->tm_hour, local->tm_min) : std::string();
}

}  // namespace mux::ui
