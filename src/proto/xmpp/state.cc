// SPDX-License-Identifier: AGPL-3.0-only
// mux.proto.xmpp.state -- What an XMPP account is now, as XMPP's extension
// points decide by it: the stream, and what the server said it has (its
// disco#info, XEP-0030) -- read once, where it comes in, into what it means.
export module mux.proto.xmpp.state;

import std;
import mux.proto.identity;

export namespace mux::proto::xmpp {

// Server-supplied XEP-0004 configuration, kept independently of tern's wire schema.
struct muc_field {
  std::string var, type, label, description;
  std::vector<std::string> values;
  std::vector<std::pair<std::string, std::string>> options;
  bool required = false;
};
inline std::optional<std::string> validate_muc_value(const muc_field& field, const std::vector<std::string>& values) {
  const bool empty = values.empty() || std::ranges::all_of(values, [](const auto& v) { return v.empty(); });
  if (field.required && empty) return "A value is required.";
  if (field.type == "boolean" && (values.size() != 1 || (values.front() != "0" && values.front() != "1" && values.front() != "true" && values.front() != "false"))) return "Use 0 or 1 for a boolean.";
  const bool multi = field.type == "list-multi" || field.type == "text-multi" || field.type == "jid-multi";
  if (!multi && values.size() > 1) return "Only one value is allowed.";
  if (field.type == "list-single" || field.type == "list-multi")
    for (const auto& value : values)
      if (!std::ranges::any_of(field.options, [&](const auto& option) { return option.second == value; })) return "Choose an advertised value.";
  return {};
}

struct muc_person { std::string jid, nick, affiliation, role; };
struct muc_affiliation { std::string jid, affiliation; };
struct muc_room {
  std::string nick, affiliation, role;
  bool joined = false, configuring = false;
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
