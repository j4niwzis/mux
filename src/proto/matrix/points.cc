// SPDX-License-Identifier: AGPL-3.0-only
// mux.proto.matrix -- What Matrix says when the client asks (mux.proto):
// its overloads, on its tag, found by ADL.
export module mux.proto.matrix;

import std;
import splice;
import mux.core;
import mux.proto;
import mux.proto.matrix.links;
import mux.proto.matrix.requests;
import chevron.escape;

export namespace mux::proto::matrix {
constexpr bool offers(const state&, feature::server_previews) { return true; }
// A DM is a room: its settings a room's.
constexpr bool direct_rooms(const state&) { return true; }


inline std::string directory_server(const state&, std::string_view address) {
  return std::string(address.substr(address.find(':') + 1));
}
constexpr bool offers(const state&, feature::people_directory) { return true; }
constexpr bool offers(const state&, feature::room_directory) { return true; }
constexpr bool offers(const state&, feature::room_creation) { return true; }
constexpr bool offers(const state&, feature::sticker_packs) { return true; }
constexpr bool offers(const state&, feature::developer_tools) { return true; }
constexpr bool offers(const state&, feature::shared_mentions) { return true; }
constexpr bool offers(const state&, feature::history_context) { return true; }
constexpr bool offers(const state&, feature::identity_verification) { return true; }

// matrix.to, for a person, a room and a message in it.
inline std::optional<std::string> share_link(const state&, std::string_view address) {
  return "https://matrix.to/#/" + std::string(address);
}
inline std::optional<std::string> person_link(const state&, std::string_view user) { return "https://matrix.to/#/" + std::string(user); }
inline std::optional<std::string> room_link(const state&, const conversation& chat) { return room_link_to(chat); }
// Only an event the server named ($...): one still being sent has no link.
inline std::optional<std::string> message_link(const state&, const conversation& chat, std::string_view event) {
  if (!event.starts_with('$'))
    return std::nullopt;
  return message_link_to(chat, event);
}
// Its own dialogs, each by what it shows, as the program shows it: the
// developer tools, as Element's -- some JSON to read and copy, a room's
// state, an event of any type sent -- and a step of interactive auth done
// in the browser.
constexpr dialog_list<tool::uia_step> dialogs(const state&) { return {}; }
// Its own account pages: its sessions. (Its profile is the profiles
// plugin's page; its encryption's,
// cross-signing and the key backup, are the encryption plugin's.)
namespace settings {
struct sessions {};
}  // namespace settings
constexpr account_page_list<settings::sessions> account_pages(const state&) { return {}; }
// What it keeps of an account while they are open: its sessions as the
// server listed them (none yet: not heard), this one's among them; whether
// this session has the cross-signing keys and the room keys are backed up;
// the session being renamed; the sessions being signed out, and what the
// server refused -- whether it wants the password for them.
struct account_security {
  bool cross_signing = false, backup = false;
};
struct account_state {
  std::optional<std::vector<session_info>> sessions;
  std::string current;
  std::optional<account_security> security;
  std::optional<std::string> renaming;
  std::vector<std::string> signing;
  bool signing_out = false;
  std::string refused;
  bool needs_password = false;
};
constexpr account_part_list<account_state> account_parts(const state&) { return {}; }

// A direct chat is a room: whom it is with, the member who is not the account.
inline std::string direct_contact(const state&, const conversation& one) {
  const auto other = std::ranges::find_if(one.members, [&](const member& each) { return each.id != one.id.account.address; });
  return other == one.members.end() ? std::string() : other->id;
}
// Its media: mxc://, the content repository's.
constexpr bool owns_media(const state&, std::string_view uri) { return uri.starts_with("mxc://"); }
// A room's !id is no name for anyone; an alias and a user ID are.
constexpr bool id_reads_as_name(const state&, std::string_view id) { return !id.starts_with('!'); }
// An ID's local part: after its sigil (@, #, !), before its server.
inline std::string local_part(const state&, std::string_view address) {
  if (address.starts_with('@') || address.starts_with('#') || address.starts_with('!'))
    address.remove_prefix(1);
  return std::string(address.substr(0, address.find(':')));
}
// A room's rules and levels, as Matrix keeps them in a chat's part: none
// kept, the defaults.
[[nodiscard]] inline const room_rules& rules_of(const room_part_t& part) {
  static const room_rules none{};
  return spl::visit(spl::overloaded{[](const room_rules& kept) -> const room_rules& { return kept; },
                                          [](const auto&) -> const room_rules& { return none; }},
                       part);
}
[[nodiscard]] inline const room_rules& rules_of(const conversation& in) { return rules_of(in.theirs); }
// And to change: the part made Matrix's where it was none.
[[nodiscard]] inline room_rules& rules_in(room_part_t& part) {
  return spl::visit(spl::overloaded{[](room_rules& kept) -> room_rules& { return kept; },
                                          [&](auto&) -> room_rules& { return part.template emplace<room_rules>(); }},
                       part);
}
// A room upgraded: the one it continues in, by its tombstone; and the one it
// continues, by its creation.
inline std::optional<std::string> successor_of(const state&, const conversation& in) { return rules_of(in).replaced_by; }
inline std::optional<std::string> predecessor_of(const state&, const conversation& in) { return rules_of(in).predecessor; }
// What the account may do in a room, as its power levels allow: write
// in it, let in those who knock; and to someone in it, only above them.
[[nodiscard]] inline std::int64_t level_of(const room_rules& rules, std::string_view who) {
  const auto level = rules.powers.find(std::string(who));
  return level == rules.powers.end() ? rules.power_default : level->second;
}
[[nodiscard]] inline std::int64_t level_of(const conversation& in, std::string_view who) { return level_of(rules_of(in), who); }
// Whether what a room's Manage facts say of one lets one do a thing.
[[nodiscard]] inline bool may(const auto& facts, const power_need_t& need) {
  return facts.mine >= rules_of(facts.theirs).needs.of(need);
}
inline part::chat_rights chat_rights(const state&, const conversation& in) {
  const std::int64_t mine = level_of(in, in.id.account.address);
  const auto asked = rules_of(in).needs.events.find("m.room.message");
  const std::int64_t needs = asked != rules_of(in).needs.events.end() ? asked->second : rules_of(in).needs.events_default;
  // Its packs (MSC2545's im.ponies.room_emotes), as a state event's level asks.
  const auto packs = rules_of(in).needs.events.find("im.ponies.room_emotes");
  const std::int64_t packs_need = packs != rules_of(in).needs.events.end() ? packs->second : rules_of(in).needs.state_default;
  return {mine >= needs, mine >= rules_of(in).needs.invite, mine >= packs_need};
}
inline part::person_rights person_rights(const state&, const conversation& in, std::string_view who) {
  const std::int64_t mine = level_of(in, in.id.account.address), theirs = level_of(in, who);
  const bool above = who != in.id.account.address && mine > theirs;
  return {above && mine >= rules_of(in).needs.of(power_need::kick{}), above && mine >= rules_of(in).needs.of(power_need::ban{})};
}
// What is known of someone's identity: on their card, and beside them in an
// encrypted room's members.
inline std::vector<part::badge> person_badges(const state&, const conversation* in, const model& known, const account_id& by,
                                              std::string_view who) {
  if (in && !in->encrypted)
    return {};
  const auto trust = known.trust_of(by, std::string(who));
  if (!trust)
    return {};
  return {spl::visit(spl::overloaded{[](trust::verified) { return part::badge{"Verified", part::tone::accent{}}; },
                                           [](trust::unverified) { return part::badge{"Not verified"}; },
                                           [](trust::changed) { return part::badge{"Identity reset", part::tone::danger{}}; }},
                        *trust)};
}
// A sender's role, as the room's power levels give it: 100 and 50.
inline std::string sender_role(const state&, const conversation& in, std::string_view who) {
  const std::int64_t power = level_of(in, who);
  return power >= 100 ? std::string("admin") : power >= 50 ? std::string("mod") : std::string();
}
// What is known of the other person's identity, in a direct encrypted chat.
[[nodiscard]] inline std::optional<trust_t> other_trust(const state& now, const conversation& one, const model& known) {
  const bool direct = spl::visit(
      spl::overloaded{[](const conversation_kind::direct&) { return true; }, [](const auto&) { return false; }}, one.kind);
  if (!one.encrypted || !direct)
    return std::nullopt;
  return known.trust_of(one.id.account, direct_contact(now, one));
}
// Encrypted, said after the chat's status, as Element's shield on its
// header: and in a direct chat, what is known of the other's identity.
inline std::vector<part::badge> header_badges(const state& now, const conversation& one, const model& known) {
  if (!one.encrypted || !one.typing.empty())
    return {};
  const auto trust = other_trust(now, one, known);
  const std::string after = !trust ? std::string()
                                   : spl::visit(spl::overloaded{[](trust::verified) { return std::string(" \u00b7 Verified"); },
                                                                      [](trust::unverified) { return std::string(" \u00b7 Not verified"); },
                                                                      [](trust::changed) { return std::string(" \u00b7 Identity reset"); }},
                                                   *trust);
  return {part::badge{"\U0001F512 Encrypted" + after}};
}
// Element's warning over the composer, in a direct encrypted chat: the
// other not verified -- messages are still encrypted to them -- or their
// identity reset, to be verified again or withdrawn on their card.
using banner = part::banner_of<request::verify_them>;
inline std::vector<banner> composer_banners(const state& now, const conversation& one, const model& known) {
  const auto trust = other_trust(now, one, known);
  if (!trust)
    return {};
  const std::string& name = one.name.empty() ? one.id.id : one.name;
  const request::verify_them them{one.id.account, direct_contact(now, one)};
  return spl::visit(
      spl::overloaded{
          [](trust::verified) { return std::vector<banner>{}; },
          [&](trust::unverified) {
            return std::vector<banner>{{std::format("Verify {} to confirm their identity.", name),
                                        part::tone::accent{}, "Verify", them}};
          },
          [&](trust::changed) {
            return std::vector<banner>{{std::format("{}'s identity changed. Verify them again.", name),
                                        part::tone::danger{}, "Verify again", them}};
          }},
      *trust);
}
constexpr bool can_pin(const state&, std::string_view event) { return event.starts_with('$'); }

// Annotation targets are events, including state and reaction events. Local
// transaction IDs cannot be related to until acknowledged by the server.
inline bool may_react(const state&, const conversation& chat, const message& event) {
  const auto& rules = rules_of(chat);
  const auto asked = rules.needs.events.find("m.reaction");
  const auto required = asked == rules.needs.events.end() ? rules.needs.events_default : asked->second;
  return event.id.starts_with('$') && !event.redacted && level_of(chat, chat.id.account.address) >= required;
}

// As the room's power levels allow it: one's own where one may send a
// redaction; another's where one may also redact.
inline bool may_delete(const state&, const conversation& chat, bool outgoing) {
  const auto mine = rules_of(chat).powers.find(chat.id.account.address);
  const std::int64_t level = mine != rules_of(chat).powers.end() ? mine->second : rules_of(chat).power_default;
  const auto redaction = rules_of(chat).needs.events.find("m.room.redaction");
  const std::int64_t send = redaction != rules_of(chat).needs.events.end() ? redaction->second : rules_of(chat).needs.events_default;
  return level >= send && (outgoing || level >= rules_of(chat).needs.redact);
}


// Element's slash commands: the command's word read once into a type
// (slash_word_of, a table), what follows it into what it does (slash_of,
// an overload for each). A word not known, or one with nothing it needs
// after it: not a command -- the line sent as it is.
namespace slash_word {
struct me {};
struct shrug {};
struct tableflip {};
struct unflip {};
struct lenny {};
struct plain {};
struct html {};
struct rainbow {};
struct rainbowme {};
struct spoiler {};
struct join {};
struct leave {};
struct invite {};
struct kick {};
struct ban {};
struct unban {};
struct topic {};
struct roomname {};
struct myroomnick {};
struct nick {};
struct op {};
struct deop {};
}  // namespace slash_word
using slash_word_t = spl::variant<slash_word::me, slash_word::shrug, slash_word::tableflip, slash_word::unflip, slash_word::lenny,
                                  slash_word::plain, slash_word::html, slash_word::rainbow, slash_word::rainbowme, slash_word::spoiler,
                                  slash_word::join, slash_word::leave, slash_word::invite, slash_word::kick, slash_word::ban,
                                  slash_word::unban, slash_word::topic, slash_word::roomname, slash_word::myroomnick, slash_word::op,
                                  slash_word::deop, slash_word::nick>;
[[nodiscard]] inline std::optional<slash_word_t> slash_word_of(std::string_view word) {
  static const std::unordered_map<std::string_view, slash_word_t> known{
      {"me", slash_word::me{}},           {"shrug", slash_word::shrug{}},       {"tableflip", slash_word::tableflip{}},
      {"unflip", slash_word::unflip{}},   {"lenny", slash_word::lenny{}},       {"plain", slash_word::plain{}},
      {"html", slash_word::html{}},       {"rainbow", slash_word::rainbow{}},   {"rainbowme", slash_word::rainbowme{}},
      {"spoiler", slash_word::spoiler{}}, {"join", slash_word::join{}},         {"j", slash_word::join{}},
      {"leave", slash_word::leave{}},     {"part", slash_word::leave{}},        {"invite", slash_word::invite{}},
      {"kick", slash_word::kick{}},       {"remove", slash_word::kick{}},       {"ban", slash_word::ban{}},
      {"unban", slash_word::unban{}},     {"topic", slash_word::topic{}},       {"roomname", slash_word::roomname{}},
      {"myroomnick", slash_word::myroomnick{}}, {"op", slash_word::op{}},       {"deop", slash_word::deop{}},
      {"nick", slash_word::nick{}},
  };
  const auto found = known.find(word);
  return found == known.end() ? std::nullopt : std::optional(found->second);
}
// The first word of what follows, and the rest after it.
[[nodiscard]] inline std::pair<std::string_view, std::string_view> slash_split(std::string_view rest) {
  const auto space = rest.find(' ');
  if (space == std::string_view::npos)
    return {rest, {}};
  std::string_view after = rest.substr(space + 1);
  while (after.starts_with(' '))
    after.remove_prefix(1);
  return {rest.substr(0, space), after};
}
// What follows, sent as a text with a face before it (or after, Element's
// /lenny), or the face alone.
[[nodiscard]] inline std::optional<slash_t> slash_face(std::string_view face, std::string_view rest) {
  return slash::said{rest.empty() ? std::string(face) : std::format("{} {}", face, rest)};
}
// Each code point of it in a hue of its own, round the colour wheel along
// the text, as Element's /rainbow: <font data-mx-color> each.
[[nodiscard]] inline std::string rainbow_html(std::string_view text) {
  std::vector<std::string_view> points;
  for (std::size_t at = 0; at < text.size();) {
    const auto lead = static_cast<unsigned char>(text[at]);
    const std::size_t length = lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
    points.push_back(text.substr(at, std::min(length, text.size() - at)));
    at += length;
  }
  std::string out;
  for (std::size_t i = 0; i < points.size(); ++i) {
    // Its hue, and the colour of it at full saturation and brightness.
    const double hue = 360.0 * static_cast<double>(i) / static_cast<double>(std::max<std::size_t>(1, points.size()));
    const double x = 1.0 - std::abs(std::fmod(hue / 60.0, 2.0) - 1.0);
    const auto sector = static_cast<int>(hue / 60.0) % 6;
    const std::array<std::array<double, 3>, 6> rgb{{{1, x, 0}, {x, 1, 0}, {0, 1, x}, {0, x, 1}, {x, 0, 1}, {1, 0, x}}};
    const auto byte = [](double v) { return static_cast<unsigned>(std::lround(v * 255.0)); };
    const auto& c = rgb[static_cast<std::size_t>(sector)];
    out += std::format(R"(<font data-mx-color="#{:02x}{:02x}{:02x}">{}</font>)", byte(c[0]), byte(c[1]), byte(c[2]),
                       chevron::escaped(points[i]));
  }
  return out;
}
[[nodiscard]] inline std::optional<slash_t> slash_of(slash_word::me, std::string_view rest) {
  if (rest.empty())
    return std::nullopt;
  return slash::said{std::string(rest), std::nullopt, true};
}
[[nodiscard]] inline std::optional<slash_t> slash_of(slash_word::shrug, std::string_view rest) { return slash_face("\u00AF\\_(\u30C4)_/\u00AF", rest); }
[[nodiscard]] inline std::optional<slash_t> slash_of(slash_word::tableflip, std::string_view rest) {
  return slash_face("(\u256F\u00B0\u25A1\u00B0\uFF09\u256F\uFE35 \u253B\u2501\u253B", rest);
}
[[nodiscard]] inline std::optional<slash_t> slash_of(slash_word::unflip, std::string_view rest) {
  return slash_face("\u252C\u2500\u2500\u252C \u30CE( \u309C-\u309C\u30CE)", rest);
}
[[nodiscard]] inline std::optional<slash_t> slash_of(slash_word::lenny, std::string_view rest) {
  return slash_face("( \u0361\u00B0 \u035C\u0296 \u0361\u00B0)", rest);
}
[[nodiscard]] inline std::optional<slash_t> slash_of(slash_word::plain, std::string_view rest) {
  if (rest.empty())
    return std::nullopt;
  return slash::said{std::string(rest)};
}
[[nodiscard]] inline std::optional<slash_t> slash_of(slash_word::html, std::string_view rest) {
  if (rest.empty())
    return std::nullopt;
  return slash::said{std::string(rest), std::string(rest)};
}
[[nodiscard]] inline std::optional<slash_t> slash_of(slash_word::rainbow, std::string_view rest) {
  if (rest.empty())
    return std::nullopt;
  return slash::said{std::string(rest), rainbow_html(rest)};
}
[[nodiscard]] inline std::optional<slash_t> slash_of(slash_word::rainbowme, std::string_view rest) {
  if (rest.empty())
    return std::nullopt;
  return slash::said{std::string(rest), rainbow_html(rest), true};
}
[[nodiscard]] inline std::optional<slash_t> slash_of(slash_word::spoiler, std::string_view rest) {
  if (rest.empty())
    return std::nullopt;
  return slash::said{std::string(rest), std::format("<span data-mx-spoiler>{}</span>", chevron::escaped(rest))};
}
[[nodiscard]] inline std::optional<slash_t> slash_of(slash_word::join, std::string_view rest) {
  const auto [room, after] = slash_split(rest);
  if (room.empty())
    return std::nullopt;
  return slash::join{std::string(room)};
}
[[nodiscard]] inline std::optional<slash_t> slash_of(slash_word::leave, std::string_view) { return slash::leave{}; }
// Someone, by their ID: what is done to them in the room.
// What is done to them, with the reason given after their ID where the
// action takes one (/kick, /ban).
[[nodiscard]] inline mux::room_action_t acted(std::type_identity<mux::room_action::invite>, std::string user, std::string_view) {
  return mux::room_action::invite{std::move(user)};
}
[[nodiscard]] inline mux::room_action_t acted(std::type_identity<mux::room_action::unban>, std::string user, std::string_view) {
  return mux::room_action::unban{std::move(user)};
}
[[nodiscard]] inline mux::room_action_t acted(std::type_identity<mux::room_action::kick>, std::string user, std::string_view reason) {
  return mux::room_action::kick{std::move(user), reason.empty() ? std::nullopt : std::optional<std::string>(reason)};
}
[[nodiscard]] inline mux::room_action_t acted(std::type_identity<mux::room_action::ban>, std::string user, std::string_view reason) {
  return mux::room_action::ban{std::move(user), reason.empty() ? std::nullopt : std::optional<std::string>(reason)};
}
template <class Action>
[[nodiscard]] std::optional<slash_t> slash_to_someone(std::string_view rest) {
  const auto [user, reason] = slash_split(rest);
  if (!user.starts_with('@'))
    return std::nullopt;
  return slash::act{acted(std::type_identity<Action>{}, std::string(user), reason)};
}
[[nodiscard]] inline std::optional<slash_t> slash_of(slash_word::invite, std::string_view rest) { return slash_to_someone<mux::room_action::invite>(rest); }
[[nodiscard]] inline std::optional<slash_t> slash_of(slash_word::kick, std::string_view rest) { return slash_to_someone<mux::room_action::kick>(rest); }
[[nodiscard]] inline std::optional<slash_t> slash_of(slash_word::ban, std::string_view rest) { return slash_to_someone<mux::room_action::ban>(rest); }
[[nodiscard]] inline std::optional<slash_t> slash_of(slash_word::unban, std::string_view rest) { return slash_to_someone<mux::room_action::unban>(rest); }
[[nodiscard]] inline std::optional<slash_t> slash_of(slash_word::topic, std::string_view rest) {
  if (rest.empty())
    return std::nullopt;
  return slash::act{mux::room_action::retopic{std::string(rest)}};
}
[[nodiscard]] inline std::optional<slash_t> slash_of(slash_word::roomname, std::string_view rest) {
  if (rest.empty())
    return std::nullopt;
  return slash::act{mux::room_action::rename{std::string(rest)}};
}
[[nodiscard]] inline std::optional<slash_t> slash_of(slash_word::myroomnick, std::string_view rest) {
  if (rest.empty())
    return std::nullopt;
  return slash::room_name_mine{std::string(rest)};
}
[[nodiscard]] inline std::optional<slash_t> slash_of(slash_word::nick, std::string_view rest) {
  if (rest.empty())
    return std::nullopt;
  return slash::name_mine{std::string(rest)};
}
[[nodiscard]] inline std::optional<slash_t> slash_of(slash_word::op, std::string_view rest) {
  const auto [user, level_text] = slash_split(rest);
  std::int64_t level = 50;
  if (!user.starts_with('@') ||
      (!level_text.empty() && std::from_chars(level_text.data(), level_text.data() + level_text.size(), level).ec != std::errc{}))
    return std::nullopt;
  return slash::power{std::string(user), level};
}
[[nodiscard]] inline std::optional<slash_t> slash_of(slash_word::deop, std::string_view rest) {
  const auto [user, after] = slash_split(rest);
  if (!user.starts_with('@'))
    return std::nullopt;
  return slash::power{std::string(user), 0};
}
// A line typed in a Matrix chat, as a slash command: what it asks done, or
// nothing -- the line, sent as it is (found by ADL, mux.proto's
// command_of).
[[nodiscard]] inline std::optional<request::slash> command_of(const state&, const conversation_id& to, std::string_view text) {
  if (!text.starts_with('/') || text.starts_with("//"))
    return std::nullopt;
  const auto [word, rest] = slash_split(text.substr(1));
  const auto known = slash_word_of(word);
  if (!known)
    return std::nullopt;
  auto what = spl::visit([&](auto one) { return slash_of(one, rest); }, *known);
  if (!what)
    return std::nullopt;
  return request::slash{to, std::move(*what)};
}

// Its own requests, done by program_asked (mux.app.proto.matrix): listed
// here, where its settings pages' manage_room is seen too -- what a debug
// build's program serves a request by.
constexpr request_list<request::verify_session, request::change_account,
                       request::refresh_sessions, request::verify_them,
                       request::change_room,
                       request::slash,
                       request::continue_uia,
                       request::cancel_uia, 
                       request::close_tool<tool::uia_step>>
requests_of(const state&) {
  return {};
}
}  // namespace mux::proto::matrix
