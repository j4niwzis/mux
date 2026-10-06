// SPDX-License-Identifier: AGPL-3.0-only
// mux.proto.matrix.client:names -- The names Matrix gives things, read into variants.
export module mux.proto.matrix.client:names;

import std;
import splice;
import knot;
import loom.api;
import loom.ev;
import loom.state;
import loom.names;
import loom.cs.joining;
import loom.cs.leaving;
import loom.cs.login;
import loom.cs.message_pagination;
import loom.cs.receipts;
import loom.cs.redaction;
import loom.cs.room_send;
import loom.cs.rooms;
import loom.cs.sync;
import loom.cs.typing;
import loom.cs.wellknown;
import mux.config;
import mux.core;
import mux.http;
import mux.net;
export import :base;

export namespace mux::proto::matrix::client {

// The names Matrix gives things, read into variants: loom's (loom.names).
namespace msgtype = loom::names::msgtype;
namespace event_type = loom::names::event_type;
namespace membership = loom::names::membership;
namespace relation = loom::names::relation;
namespace room_type = loom::names::room_type;
namespace body_format = loom::names::body_format;
namespace verification_kind = loom::names::verification_kind;
namespace state_type = loom::names::state_type;
namespace errcode = loom::names::errcode;
using loom::names::msgtype_t;
using loom::names::event_type_t;
using loom::names::membership_t;
using loom::names::relation_t;
using loom::names::room_type_t;
using loom::names::body_format_t;
using loom::names::verification_kind_t;
using loom::names::state_type_t;
using loom::names::errcode_t;
using loom::names::named;
using loom::names::msgtype_of;
using loom::names::verification_request_of;
using loom::names::verification_kind_of;
using loom::names::event_type_of;
using loom::names::membership_of;
using loom::names::relation_of;
using loom::names::room_type_of;
using loom::names::body_format_of;
using loom::names::state_type_of;
using loom::names::errcode_of;

// A room's join rule and history visibility, as their words say; and the
// words, as a room's state is written.
[[nodiscard]] inline join_rule_t join_rule_of(std::optional<std::string_view> name) {
  static const std::unordered_map<std::string_view, join_rule_t> known = {
      {"public", mux::proto::matrix::join_rule::open{}},
      {"invite", mux::proto::matrix::join_rule::invite{}},
      {"knock", mux::proto::matrix::join_rule::knock{}},
      {"restricted", mux::proto::matrix::join_rule::restricted{}},
      {"knock_restricted", mux::proto::matrix::join_rule::knock_restricted{}}};
  return named<join_rule_t, mux::proto::matrix::join_rule::other>(known, name);
}
[[nodiscard]] inline history_rule_t history_rule_of(std::optional<std::string_view> name) {
  static const std::unordered_map<std::string_view, history_rule_t> known = {
      {"shared", mux::proto::matrix::history_rule::shared{}},
      {"invited", mux::proto::matrix::history_rule::invited{}},
      {"joined", mux::proto::matrix::history_rule::joined{}},
      {"world_readable", mux::proto::matrix::history_rule::world_readable{}}};
  return named<history_rule_t, mux::proto::matrix::history_rule::shared>(known, name);
}
[[nodiscard]] constexpr std::string_view word_of(mux::proto::matrix::join_rule::open) { return "public"; }
[[nodiscard]] constexpr std::string_view word_of(mux::proto::matrix::join_rule::invite) { return "invite"; }
[[nodiscard]] constexpr std::string_view word_of(mux::proto::matrix::join_rule::knock) { return "knock"; }
[[nodiscard]] constexpr std::string_view word_of(mux::proto::matrix::join_rule::other) { return "invite"; }
[[nodiscard]] constexpr std::string_view word_of(const mux::proto::matrix::join_rule::restricted&) { return "restricted"; }
[[nodiscard]] constexpr std::string_view word_of(const mux::proto::matrix::join_rule::knock_restricted&) { return "knock_restricted"; }
// A room's join rule as its state says it, read from the event's types:
// the spaces a restricted one lets in, from its allow list.
[[nodiscard]] inline join_rule_t join_rule_from(const loom::ev::m_room_join_rules_content_t* content) {
  namespace rule = mux::proto::matrix::join_rule;
  using said = loom::ev::m_room_join_rules_content_t::join_rule_values;
  using condition = loom::ev::m_room_join_rules_content_t::allow_condition_t;
  if (content == nullptr)
    return rule::other{};
  const auto spaces = [&] {
    std::vector<std::string> out;
    for (const condition& one : content->allow.value_or(std::vector<condition>{}))
      spl::visit(spl::overloaded{[&](const condition::type_values::m_room_membership&) {
                                   if (one.room_id)
                                     out.push_back(*one.room_id);
                                 },
                                 [](const std::string&) {}},
                 one.type);
    return out;
  };
  return spl::visit(spl::overloaded{[](const said::public_&) -> join_rule_t { return rule::open{}; },
                                    [](const said::invite&) -> join_rule_t { return rule::invite{}; },
                                    [](const said::knock&) -> join_rule_t { return rule::knock{}; },
                                    [](const said::private_&) -> join_rule_t { return rule::other{}; },
                                    [&](const said::restricted&) -> join_rule_t { return rule::restricted{spaces()}; },
                                    [&](const said::knock_restricted&) -> join_rule_t { return rule::knock_restricted{spaces()}; },
                                    [](const std::string&) -> join_rule_t { return rule::other{}; }},
                    content->join_rule);
}
// The allow list a join rule writes: each space it names, its members let in.
[[nodiscard]] inline std::optional<std::vector<loom::ev::m_room_join_rules_content_t::allow_condition_t>> allow_of(
    const join_rule_t& rule) {
  using condition = loom::ev::m_room_join_rules_content_t::allow_condition_t;
  const auto of = [](const std::vector<std::string>& spaces) {
    return std::optional(std::ranges::to<std::vector<condition>>(std::views::transform(spaces, [](const std::string& space) {
      return condition{.type = condition::type_values::m_room_membership{}, .room_id = space};
    })));
  };
  return spl::visit(spl::overloaded{[&](const mux::proto::matrix::join_rule::restricted& one) { return of(one.spaces); },
                                    [&](const mux::proto::matrix::join_rule::knock_restricted& one) { return of(one.spaces); },
                                    [](const auto&) { return std::optional<std::vector<condition>>(); }},
                    rule);
}
[[nodiscard]] constexpr std::string_view word_of(mux::proto::matrix::history_rule::shared) { return "shared"; }
[[nodiscard]] constexpr std::string_view word_of(mux::proto::matrix::history_rule::invited) { return "invited"; }
[[nodiscard]] constexpr std::string_view word_of(mux::proto::matrix::history_rule::joined) { return "joined"; }
[[nodiscard]] constexpr std::string_view word_of(mux::proto::matrix::history_rule::world_readable) { return "world_readable"; }


}  // namespace mux::proto::matrix::client
