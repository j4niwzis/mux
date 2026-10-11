// SPDX-License-Identifier: AGPL-3.0-only
// mux.proto.xmpp.state -- What an XMPP account is now, as XMPP's extension
// points decide by it: the stream, and what the server said it has (its
// disco#info, XEP-0030) -- read once, where it comes in, into what it means.
export module mux.proto.xmpp.state;

import std;
import splice;
import mux.proto.identity;

export namespace mux::proto::xmpp {

// XEP-0004 field types are interpreted once when the server's form arrives.
namespace muc_kind {
struct text_single {}; struct text_private {}; struct text_multi {};
struct jid_single {}; struct jid_multi {}; struct boolean {};
struct list_single {}; struct list_multi {}; struct fixed {}; struct hidden {};
struct unsupported {};
}
using muc_kind_t = spl::variant<muc_kind::text_single, muc_kind::text_private,
    muc_kind::text_multi, muc_kind::jid_single, muc_kind::jid_multi,
    muc_kind::boolean, muc_kind::list_single, muc_kind::list_multi,
    muc_kind::fixed, muc_kind::hidden, muc_kind::unsupported>;
inline muc_kind_t muc_kind_of(std::string_view name) {
  static const std::map<std::string_view, muc_kind_t> known{
      {"text-single", muc_kind::text_single{}}, {"text-private", muc_kind::text_private{}},
      {"text-multi", muc_kind::text_multi{}}, {"jid-single", muc_kind::jid_single{}},
      {"jid-multi", muc_kind::jid_multi{}}, {"boolean", muc_kind::boolean{}},
      {"list-single", muc_kind::list_single{}}, {"list-multi", muc_kind::list_multi{}},
      {"fixed", muc_kind::fixed{}}, {"hidden", muc_kind::hidden{}}};
  const auto found = known.find(name);
  return found == known.end() ? muc_kind_t{muc_kind::unsupported{}} : found->second;
}
inline std::optional<bool> muc_boolean_of(std::string_view value) {
  static constexpr std::array<std::pair<std::string_view, bool>, 4> known{{
      {"0", false}, {"false", false}, {"1", true}, {"true", true}}};
  const auto found = std::ranges::find(known, value, &decltype(known)::value_type::first);
  return found == known.end() ? std::nullopt : std::optional(found->second);
}
struct muc_field {
  std::string var, label, description;
  muc_kind_t kind = muc_kind::text_single{};
  std::vector<std::string> values;
  std::vector<std::pair<std::string, std::string>> options;
  bool required = false;
};
struct muc_field_rules { bool editable = true, multiple = false, choices = false, jid = false, boolean = false; };
inline muc_field_rules rules_of(const muc_kind_t& kind) {
  return spl::visit(spl::overloaded{
      [](muc_kind::text_single) { return muc_field_rules{}; },
      [](muc_kind::text_private) { return muc_field_rules{}; },
      [](muc_kind::text_multi) { return muc_field_rules{.multiple = true}; },
      [](muc_kind::jid_single) { return muc_field_rules{.jid = true}; },
      [](muc_kind::jid_multi) { return muc_field_rules{.multiple = true, .jid = true}; },
      [](muc_kind::boolean) { return muc_field_rules{.boolean = true}; },
      [](muc_kind::list_single) { return muc_field_rules{.choices = true}; },
      [](muc_kind::list_multi) { return muc_field_rules{.multiple = true, .choices = true}; },
      [](muc_kind::fixed) { return muc_field_rules{.editable = false}; },
      [](muc_kind::hidden) { return muc_field_rules{.editable = false}; },
      [](muc_kind::unsupported) { return muc_field_rules{.editable = false}; }}, kind);
}
inline std::optional<std::string> validate_muc_value(const muc_field& field, const auto& values) {
  const auto rules = rules_of(field.kind);
  if (!rules.editable) return {};
  const bool empty = std::ranges::empty(values) || std::ranges::all_of(values, [](const auto& v) { return v.empty(); });
  if (field.required && empty) return "A value is required.";
  if (rules.boolean && (std::ranges::distance(values) != 1 || !muc_boolean_of(*std::ranges::begin(values)))) return "Choose on or off.";
  if (!rules.multiple && std::ranges::distance(values) > 1) return "Only one value is allowed.";
  if (rules.choices && !std::ranges::all_of(values, [&](const auto& value) {
        return std::ranges::any_of(field.options, [&](const auto& option) { return option.second == value; });
      })) return "Choose an advertised value.";
  return {};
}

struct muc_person { std::string jid, nick, affiliation, role; };
struct muc_affiliation { std::string jid, affiliation; };
struct muc_room {
  std::string nick, affiliation, role;
  bool joined = false, configuring = false;
  std::string title;
  std::vector<std::string> instructions;
  std::vector<muc_field> fields;
  std::vector<muc_person> people;
  std::vector<muc_affiliation> affiliations;
};

struct state {
  bool online = false;  // the stream is up
  // The server's message archive (XEP-0313, urn:xmpp:mam:2); unset where it
  // has not been asked yet.
  std::optional<bool> archive;
  bool upload = false;
  friend bool operator==(const state&, const state&) = default;
};
constexpr room_part_list<muc_room> room_parts(const state&) { return {}; }

// The protocol is known by its state type itself (id<state>, in protocol_t);
// every overload of it takes its state.

}  // namespace mux::proto::xmpp
