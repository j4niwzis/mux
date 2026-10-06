// SPDX-License-Identifier: AGPL-3.0-only
// mux.config:config_looks -- Backgrounds, the space bars, and the bubbles' and panels' looks.
export module mux.config:config_looks;

import std;
import splice;
import knot;
import mux.vault;
import mux.proto.kept;
import :config_choices;
import :config_settings;

export namespace mux::config {
// What a chat's background is: the theme's own -- its gradient and
// Telegram's pattern -- a plain colour, or a picture of the user's (kept in
// mux's data, by its path there). Kept as a word: "theme", "plain", or the
// picture's path; read once into the variant.
namespace wallpaper {
struct theme {
  friend bool operator==(theme, theme) = default;
};
struct plain {
  friend bool operator==(plain, plain) = default;
};
struct picture {
  std::string path;
  friend bool operator==(const picture&, const picture&) = default;
};
}  // namespace wallpaper
using wallpaper_t = spl::variant<wallpaper::theme, wallpaper::plain, wallpaper::picture>;
[[nodiscard]] inline std::string word_of(const wallpaper_t& one) {
  return spl::visit(spl::overloaded{[](wallpaper::theme) { return std::string("theme"); },
                                          [](wallpaper::plain) { return std::string("plain"); },
                                          [](const wallpaper::picture& at) { return at.path; }},
                       one);
}
[[nodiscard]] inline wallpaper_t wallpaper_of(std::string_view word) {
  static constexpr std::array<std::pair<std::string_view, bool>, 2> kWords{{{"theme", true}, {"plain", false}}};
  for (const auto& [name, theme] : kWords)
    if (word == name)
      return theme ? wallpaper_t{wallpaper::theme{}} : wallpaper_t{wallpaper::plain{}};
  return wallpaper::picture{std::string(word)};
}
// What a bar of spaces holds: Home (every chat), Direct messages, or a
// Matrix space, by its room. Kept as a word: "home", "direct", or the
// room's id (which starts with '!', so none is taken for the others).
namespace space_item {
struct home {
  friend bool operator==(home, home) = default;
};
struct direct {
  friend bool operator==(direct, direct) = default;
};
struct space {
  std::string room;
  friend bool operator==(const space&, const space&) = default;
};
}  // namespace space_item
using space_item_t = spl::variant<space_item::home, space_item::direct, space_item::space>;
[[nodiscard]] inline std::string word_of(const space_item_t& one) {
  return spl::visit(spl::overloaded{[](space_item::home) { return std::string("home"); },
                                          [](space_item::direct) { return std::string("direct"); },
                                          [](const space_item::space& s) { return s.room; }},
                       one);
}
[[nodiscard]] inline space_item_t space_item_of(std::string_view word) {
  if (word == "home")
    return space_item::home{};
  if (word == "direct")
    return space_item::direct{};
  return space_item::space{std::string(word)};
}
// Where it is put: the bar down the side, the one along the top -- or
// hidden. An item may be in both bars.
namespace space_bar {
struct side {
  friend bool operator==(side, side) = default;
};
struct top {
  friend bool operator==(top, top) = default;
};
struct hidden {
  friend bool operator==(hidden, hidden) = default;
};
}  // namespace space_bar
using space_bar_t = spl::variant<space_bar::side, space_bar::top, space_bar::hidden>;
[[nodiscard]] inline std::string_view word_of(const space_bar_t& one) {
  return spl::visit(spl::overloaded{[](space_bar::side) { return std::string_view("side"); },
                                          [](space_bar::top) { return std::string_view("top"); },
                                          [](space_bar::hidden) { return std::string_view("hidden"); }},
                       one);
}
[[nodiscard]] inline space_bar_t space_bar_of(std::string_view word) {
  if (word == "top")
    return space_bar::top{};
  if (word == "hidden")
    return space_bar::hidden{};
  return space_bar::side{};
}
// An item put in a bar, for an account: in the file as words, in the
// program as types. The order of those of a bar is the bar's order.
struct space_place {
  std::string account;
  std::string item;
  std::string bar;
  friend bool operator==(const space_place&, const space_place&) = default;
};
consteval auto json_schema(knot::type<space_place>) { return knot::schema<space_place>(); }
struct space_placed {
  std::string account;
  space_item_t item;
  space_bar_t bar;
  friend bool operator==(const space_placed&, const space_placed&) = default;
};

// How message bubbles look: solid, as they always were; translucent (their
// colour at an opacity); frosted (what is behind blurred, tinted with their
// colour at an opacity); or glass (translucent, a light edge round it). Kept
// as a word, "frosted:70" -- the kind and the opacity in percent.
namespace bubbles {
struct solid {
  friend bool operator==(solid, solid) = default;
};
struct translucent {
  friend bool operator==(translucent, translucent) = default;
};
struct frosted {
  friend bool operator==(frosted, frosted) = default;
};
struct glass {
  friend bool operator==(glass, glass) = default;
};
}  // namespace bubbles
using bubbles_t = spl::variant<bubbles::solid, bubbles::translucent, bubbles::frosted, bubbles::glass>;
// What is in a chat besides the bubbles, each at an opacity of its own
// where one is chosen -- else the bubbles'.
struct element_opacity {
  std::optional<int> service;    // a line of something done: an invite, a join
  std::optional<int> images;     // pictures, albums
  std::optional<int> avatars;    // the faces beside the bubbles
  std::optional<int> reactions;  // the reactions' chips
  friend bool operator==(const element_opacity&, const element_opacity&) = default;
};
// The elements by their names in a look's word, for reading and writing it.
inline constexpr std::array<std::pair<std::string_view, std::optional<int> element_opacity::*>, 4> kElementNames{
    {{"service", &element_opacity::service},
     {"images", &element_opacity::images},
     {"avatars", &element_opacity::avatars},
     {"reactions", &element_opacity::reactions}}};
// What in a chat is drawn frosted besides the bubbles, each with a blur of
// its own where one is chosen -- else the bubbles'.
struct element_blur {
  std::optional<double> service;    // a line of something done
  std::optional<double> reactions;  // the reactions' chips
  friend bool operator==(const element_blur&, const element_blur&) = default;
};
inline constexpr std::array<std::pair<std::string_view, std::optional<double> element_blur::*>, 2> kElementBlurNames{
    {{"service", &element_blur::service}, {"reactions", &element_blur::reactions}}};
struct bubble_look {
  bubbles_t kind = bubbles::solid{};
  int opacity = 70;  // percent, where the kind has one
  element_opacity elements;
  // How much Frosted blurs, in percent of the most; none, as the window's.
  std::optional<double> blur;
  element_blur blurs;
  friend bool operator==(const bubble_look&, const bubble_look&) = default;
};
// A look as a level has it, what it leaves unsaid -- an element's opacity,
// the blur, an element's blur -- taken from the level over it: a chat's look
// over its account's over every chat's, each saying only what it changes.
[[nodiscard]] inline bubble_look filled_from(bubble_look below, const bubble_look& above) {
  for (const auto& [name, member] : kElementNames)
    if (!(below.elements.*member))
      below.elements.*member = above.elements.*member;
  if (!below.blur)
    below.blur = above.blur;
  for (const auto& [name, member] : kElementBlurNames)
    if (!(below.blurs.*member))
      below.blurs.*member = above.blurs.*member;
  return below;
}
// What a look is chosen for: the messages' bubbles, or the panels round
// them (over the background behind the whole window).
namespace look_part {
struct bubbles {
  friend bool operator==(bubbles, bubbles) = default;
};
struct panels {
  friend bool operator==(panels, panels) = default;
};
}  // namespace look_part
using look_part_t = spl::variant<look_part::bubbles, look_part::panels>;
[[nodiscard]] inline std::string word_of(const bubble_look& one) {
  const std::string_view kind = spl::visit(spl::overloaded{[](bubbles::solid) { return std::string_view("solid"); },
                                                                 [](bubbles::translucent) { return std::string_view("translucent"); },
                                                                 [](bubbles::frosted) { return std::string_view("frosted"); },
                                                                 [](bubbles::glass) { return std::string_view("glass"); }},
                                              one.kind);
  std::string out = std::format("{}:{}", kind, one.opacity);
  for (const auto& [name, member] : kElementNames)
    if (const auto& own = one.elements.*member)
      out += std::format(";{}={}", name, *own);
  // ";blur=55.2;service.blur=40": the look's blur, and its elements'.
  if (one.blur)
    out += std::format(";blur={}", *one.blur);
  for (const auto& [name, member] : kElementBlurNames)
    if (const auto& own = one.blurs.*member)
      out += std::format(";{}.blur={}", name, *own);
  return out;
}
[[nodiscard]] inline bubble_look bubble_look_of(std::string_view word) {
  static constexpr std::array<std::pair<std::string_view, int>, 4> kKinds{
      {{"solid", 0}, {"translucent", 1}, {"frosted", 2}, {"glass", 3}}};
  bubble_look out;
  const std::string_view name = word.substr(0, word.find(':'));
  for (const auto& [each, index] : kKinds)
    if (name == each) {
      static const std::array<bubbles_t, 4> kinds{bubbles::solid{}, bubbles::translucent{}, bubbles::frosted{},
                                                      bubbles::glass{}};
      out.kind = kinds[static_cast<std::size_t>(index)];
    }
  const std::string_view head = word.substr(0, word.find(';'));
  if (const auto colon = head.find(':'); colon != std::string_view::npos) {
    int percent = out.opacity;
    std::from_chars(head.data() + colon + 1, head.data() + head.size(), percent);
    out.opacity = std::clamp(percent, 10, 100);
  }
  // ";images=80;avatars=100": the elements chosen apart from the bubbles.
  for (std::size_t at = word.find(';'); at != std::string_view::npos;) {
    const std::size_t next = word.find(';', at + 1);
    const std::string_view pair = word.substr(at + 1, next == std::string_view::npos ? std::string_view::npos : next - at - 1);
    const std::size_t equals = pair.find('=');
    for (const auto& [name, member] : kElementNames)
      if (equals != std::string_view::npos && pair.substr(0, equals) == name) {
        int percent = 100;
        std::from_chars(pair.data() + equals + 1, pair.data() + pair.size(), percent);
        out.elements.*member = std::clamp(percent, 0, 100);
      }
    // A blur, the look's or an element's: a percent, with its fraction.
    const auto blur_at = [&] {
      double percent = 10.0;
      std::from_chars(pair.data() + equals + 1, pair.data() + pair.size(), percent);
      return std::clamp(percent, 0.0, 100.0);
    };
    if (equals != std::string_view::npos && pair.substr(0, equals) == "blur")
      out.blur = blur_at();
    for (const auto& [name, member] : kElementBlurNames)
      if (equals != std::string_view::npos && pair.substr(0, equals) == std::format("{}.blur", name))
        out.blurs.*member = blur_at();
    at = next;
  }
  return out;
}

// What is asked of a background at a level: as the level over it says, the
// theme's, plain, or a picture to choose.
namespace wallpaper_pick {
struct inherit {};
struct theme {};
struct plain {};
struct picture {};
}  // namespace wallpaper_pick
using wallpaper_pick_t =
    spl::variant<wallpaper_pick::inherit, wallpaper_pick::theme, wallpaper_pick::plain, wallpaper_pick::picture>;

}  // namespace mux::config
