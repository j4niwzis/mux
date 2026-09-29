// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.words: Names read into variants: a matrix: URI's parts, the motion setting.
export module mux.app.words;

import std;
import skiff.paint;

export namespace mux::app {

// The parts of a matrix: URI (MSC2312), by the name that leads each.
// Each says the sigil of the ID it names, and whether it names an event.
namespace uri_part {
struct user {  // u
  static constexpr std::optional<char> sigil = '@';
  static constexpr bool names_event = false;
};
struct alias {  // r
  static constexpr std::optional<char> sigil = '#';
  static constexpr bool names_event = false;
};
struct room_id {  // roomid
  static constexpr std::optional<char> sigil = '!';
  static constexpr bool names_event = false;
};
struct event {  // e
  static constexpr std::optional<char> sigil = std::nullopt;
  static constexpr bool names_event = true;
};
struct other {
  static constexpr std::optional<char> sigil = std::nullopt;
  static constexpr bool names_event = false;
};
}  // namespace uri_part
using uri_part_t = std::variant<uri_part::user, uri_part::alias, uri_part::room_id, uri_part::event, uri_part::other>;
uri_part_t uri_part_of(std::string_view name) {
  static const std::unordered_map<std::string_view, uri_part_t> known = {
      {"u", uri_part::user{}}, {"r", uri_part::alias{}}, {"roomid", uri_part::room_id{}}, {"e", uri_part::event{}}};
  const auto found = known.find(name);
  return found == known.end() ? uri_part_t{uri_part::other{}} : found->second;
}

// How much moves, as the accounts file says: "none", "reduced" or "full"
// (and full where it says nothing, or what it says is none of these).
skiff::paint::Motion motion_of(const std::optional<std::string>& said) {
  if (!said)
    return skiff::paint::motion::full{};
  static const std::unordered_map<std::string_view, skiff::paint::Motion> known = {
      {"full", skiff::paint::motion::full{}},
      {"reduced", skiff::paint::motion::reduced{}},
      {"none", skiff::paint::motion::none{}}};
  if (const auto found = known.find(*said); found != known.end())
    return found->second;
  std::println(std::cerr, "[mux] motion is \"none\", \"reduced\" or \"full\", not \"{}\": full it is", *said);
  return skiff::paint::motion::full{};
}

}  // namespace mux::app
