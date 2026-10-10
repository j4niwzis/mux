// SPDX-License-Identifier: AGPL-3.0-only
// mux.config:config_choices -- The choices a setting is one of: theme, accent, renderer, a proxy's kind, notifications' backend, mode and flags -- each a type, read once from its word.
export module mux.config:config_choices;

import std;
import splice;
import knot;
import mux.vault;
import mux.proto.kept;

export namespace mux::config {

// A theme, and what draws the window: kept as words in the file, read into
// these once, and used as these everywhere else.
namespace theme {
// Telegram Desktop's four: its base palette, day-blue, night, night-green.
struct classic {
  static constexpr std::string_view json_value = "classic";
  static constexpr bool light = true;
  friend bool operator==(classic, classic) = default;
};
struct day {
  static constexpr std::string_view json_value = "day";
  static constexpr bool light = true;
  friend bool operator==(day, day) = default;
};
struct tinted {
  static constexpr std::string_view json_value = "tinted";
  static constexpr bool light = false;
  friend bool operator==(tinted, tinted) = default;
};
struct night {
  static constexpr std::string_view json_value = "night";
  static constexpr bool light = false;
  friend bool operator==(night, night) = default;
};
}  // namespace theme
using theme_t = spl::variant<theme::classic, theme::day, theme::tinted, theme::night>;
// Empty fields inherit the nearest room, space, account or client value.
struct style_settings {
  std::optional<theme_t> theme;
  std::string theme_file;
  std::string font;
  std::string monospace;
  friend bool operator==(const style_settings&, const style_settings&) = default;
};
consteval auto json_schema(knot::type<style_settings>) { return knot::schema<style_settings>(); }
struct custom_theme {
  std::string name;
  std::optional<std::uint32_t> background;
  std::optional<std::uint32_t> sidebar;
  std::optional<std::uint32_t> chosen;
  std::optional<std::uint32_t> text;
  std::optional<std::uint32_t> dim;
  std::optional<std::uint32_t> accent;
  std::optional<std::uint32_t> error;
  std::optional<std::uint32_t> selected, selected_text, band, section, tile, sent_time;
  std::optional<std::uint32_t> bubble;
  std::optional<std::uint32_t> out_bubble;
  std::optional<std::uint32_t> chat;
  std::optional<std::uint32_t> chat_top;
  std::optional<std::uint32_t> pattern;
  std::optional<std::uint32_t> on_accent;
};
consteval auto json_schema(knot::type<custom_theme>) { return knot::schema<custom_theme>(); }
[[nodiscard]] inline style_settings filled_from(style_settings below, const style_settings& above) {
  const bool own_theme = below.theme.has_value();
  if (!below.theme) below.theme = above.theme;
  if (below.theme_file.empty() && !own_theme) below.theme_file = above.theme_file;
  if (below.font.empty()) below.font = above.font;
  if (below.monospace.empty()) below.monospace = above.monospace;
  return below;
}
// The accent a theme is drawn with: its own, or one of Telegram's circles --
// each a shade of its own in each theme.
namespace accent {
struct theme_own {
  static constexpr std::string_view json_value = "theme";
  friend bool operator==(theme_own, theme_own) = default;
};
struct blue {
  static constexpr std::string_view json_value = "blue";
  friend bool operator==(blue, blue) = default;
};
struct green {
  static constexpr std::string_view json_value = "green";
  friend bool operator==(green, green) = default;
};
struct pink {
  static constexpr std::string_view json_value = "pink";
  friend bool operator==(pink, pink) = default;
};
struct orange {
  static constexpr std::string_view json_value = "orange";
  friend bool operator==(orange, orange) = default;
};
struct purple {
  static constexpr std::string_view json_value = "purple";
  friend bool operator==(purple, purple) = default;
};
struct red {
  static constexpr std::string_view json_value = "red";
  friend bool operator==(red, red) = default;
};
struct grey {
  static constexpr std::string_view json_value = "grey";
  friend bool operator==(grey, grey) = default;
};
struct gold {
  static constexpr std::string_view json_value = "gold";
  friend bool operator==(gold, gold) = default;
};
}  // namespace accent
using accent_t = spl::variant<accent::theme_own, accent::blue, accent::green, accent::pink, accent::orange,
                              accent::purple, accent::red, accent::grey, accent::gold>;
// How much the window moves: all of it, the small movements only, nothing.
namespace motion {
struct full {
  static constexpr std::string_view json_value = "full";
  friend bool operator==(full, full) = default;
};
struct reduced {
  static constexpr std::string_view json_value = "reduced";
  friend bool operator==(reduced, reduced) = default;
};
struct none {
  static constexpr std::string_view json_value = "none";
  friend bool operator==(none, none) = default;
};
}  // namespace motion
using motion_t = spl::variant<motion::full, motion::reduced, motion::none>;
namespace renderer {
struct opengl {
  static constexpr std::string_view json_value = "opengl";
  friend bool operator==(opengl, opengl) = default;
};
struct software {
  static constexpr std::string_view json_value = "software";
  friend bool operator==(software, software) = default;
};
}  // namespace renderer
using renderer_t = spl::variant<renderer::opengl, renderer::software>;
namespace proxy_kind {
struct socks5 {
  static constexpr std::string_view json_value = "socks5";
  friend bool operator==(socks5, socks5) = default;
};
struct http {
  static constexpr std::string_view json_value = "http";
  friend bool operator==(http, http) = default;
};
}  // namespace proxy_kind
using proxy_kind_t = spl::variant<proxy_kind::socks5, proxy_kind::http>;

// The words of the file, read by knot into these: each choice's own, the
// words mux used before for some, and any other word -- read, and taken as
// the default. What the program works with is the choice itself.
namespace said {
struct light {  // mux's own light theme, before: Day
  static constexpr std::string_view json_value = "light";
  friend bool operator==(light, light) = default;
};
struct dark {  // and its dark: Night
  static constexpr std::string_view json_value = "dark";
  friend bool operator==(dark, dark) = default;
};
struct cyan {  // an accent called so before: Blue
  static constexpr std::string_view json_value = "cyan";
  friend bool operator==(cyan, cyan) = default;
};
}  // namespace said
using theme_said_t = spl::variant<theme::classic, theme::day, theme::tinted, theme::night, said::light, said::dark, std::string>;
using accent_said_t = spl::variant<accent::theme_own, accent::blue, accent::green, accent::pink, accent::orange, accent::purple,
                                   accent::red, accent::grey, accent::gold, said::cyan, std::string>;
using motion_said_t = spl::variant<motion::full, motion::reduced, motion::none, std::string>;
using renderer_said_t = spl::variant<renderer::opengl, renderer::software, std::string>;
// Each read: the choice it says, the default where it says none of them,
// or nothing; an old word, what it meant.
[[nodiscard]] inline theme_t theme_of(const std::optional<theme_said_t>& said) {
  if (!said)
    return theme::tinted{};
  return spl::visit(spl::overloaded{[](const std::string&) -> theme_t { return theme::tinted{}; },
                                    [](said::light) -> theme_t { return theme::day{}; },
                                    [](said::dark) -> theme_t { return theme::night{}; },
                                    [](auto one) -> theme_t { return one; }},
                    *said);
}
[[nodiscard]] inline accent_t accent_of(const std::optional<accent_said_t>& said) {
  if (!said)
    return accent::theme_own{};
  return spl::visit(spl::overloaded{[](const std::string&) -> accent_t { return accent::theme_own{}; },
                                    [](said::cyan) -> accent_t { return accent::blue{}; },
                                    [](auto one) -> accent_t { return one; }},
                    *said);
}
[[nodiscard]] inline motion_t motion_of(const std::optional<motion_said_t>& said) {
  if (!said)
    return motion::full{};
  return spl::visit(spl::overloaded{[](const std::string&) -> motion_t { return motion::full{}; },
                                    [](auto one) -> motion_t { return one; }},
                    *said);
}
[[nodiscard]] inline renderer_t renderer_of(const std::optional<renderer_said_t>& said) {
  if (!said)
    return renderer::opengl{};
  return spl::visit(spl::overloaded{[](const std::string&) -> renderer_t { return renderer::opengl{}; },
                                    [](auto one) -> renderer_t { return one; }},
                    *said);
}
// An accent said as a word somewhere other than the file's own field -- an
// account's colour, a chat's strip -- read so until those are typed too.
[[nodiscard]] inline accent_t accent_of(const std::optional<std::string>& word) {
  static const std::unordered_map<std::string_view, accent_t> known = {
      {"blue", accent::blue{}},     {"cyan", accent::blue{}},   {"green", accent::green{}},
      {"pink", accent::pink{}},     {"orange", accent::orange{}}, {"purple", accent::purple{}},
      {"red", accent::red{}},       {"grey", accent::grey{}},   {"gold", accent::gold{}}};
  if (!word)
    return accent::theme_own{};
  const auto found = known.find(*word);
  return found == known.end() ? accent_t{accent::theme_own{}} : found->second;
}
// And each written as it is.
template <class Said, class... Ts>
[[nodiscard]] Said said_of(const spl::variant<Ts...>& one) {
  return spl::visit([](auto each) { return Said{each}; }, one);
}
// How a notification is shown: by the desktop's own service
// (org.freedesktop.Notifications, over D-Bus), or by mux, as tdesktop's
// own: a small window in a corner of the screen.
namespace notify_backend {
struct native {
  static constexpr std::string_view json_value = "native";
  friend bool operator==(native, native) = default;
};
struct built_in {
  static constexpr std::string_view json_value = "built-in";
  friend bool operator==(built_in, built_in) = default;
};
}  // namespace notify_backend
using notify_backend_t = spl::variant<notify_backend::native, notify_backend::built_in>;
// A chat's own choice of what notifies, as Telegram's and Element's: as
// its account says, everything, only what mentions the user, nothing.
namespace notify_mode {
struct by_default {
  static constexpr std::string_view json_value = "default";
  friend bool operator==(by_default, by_default) = default;
};
struct all {
  static constexpr std::string_view json_value = "all";
  friend bool operator==(all, all) = default;
};
struct mentions {
  static constexpr std::string_view json_value = "mentions";
  friend bool operator==(mentions, mentions) = default;
};
struct off {
  static constexpr std::string_view json_value = "off";
  friend bool operator==(off, off) = default;
};
}  // namespace notify_mode
using notify_mode_t = spl::variant<notify_mode::by_default, notify_mode::all, notify_mode::mentions, notify_mode::off>;
// The switches of the notifications page, each a member of its settings.
namespace notify_flag {
struct desktop {};
struct sound {};
}  // namespace notify_flag
using notify_flag_t = spl::variant<notify_flag::desktop, notify_flag::sound>;

[[nodiscard]] constexpr std::string_view word_of(theme::classic) { return "classic"; }
[[nodiscard]] constexpr std::string_view word_of(theme::day) { return "day"; }
[[nodiscard]] constexpr std::string_view word_of(theme::tinted) { return "tinted"; }
[[nodiscard]] constexpr std::string_view word_of(theme::night) { return "night"; }
[[nodiscard]] constexpr std::string_view word_of(accent::theme_own) { return "theme"; }
[[nodiscard]] constexpr std::string_view word_of(accent::blue) { return "blue"; }
[[nodiscard]] constexpr std::string_view word_of(accent::green) { return "green"; }
[[nodiscard]] constexpr std::string_view word_of(accent::pink) { return "pink"; }
[[nodiscard]] constexpr std::string_view word_of(accent::orange) { return "orange"; }
[[nodiscard]] constexpr std::string_view word_of(accent::purple) { return "purple"; }
[[nodiscard]] constexpr std::string_view word_of(accent::red) { return "red"; }
[[nodiscard]] constexpr std::string_view word_of(accent::grey) { return "grey"; }
[[nodiscard]] constexpr std::string_view word_of(accent::gold) { return "gold"; }
[[nodiscard]] constexpr std::string_view word_of(motion::full) { return "full"; }
[[nodiscard]] constexpr std::string_view word_of(motion::reduced) { return "reduced"; }
[[nodiscard]] constexpr std::string_view word_of(motion::none) { return "none"; }
[[nodiscard]] constexpr std::string_view word_of(renderer::opengl) { return "opengl"; }
[[nodiscard]] constexpr std::string_view word_of(renderer::software) { return "software"; }
[[nodiscard]] constexpr std::string_view word_of(proxy_kind::socks5) { return "socks5"; }
[[nodiscard]] constexpr std::string_view word_of(proxy_kind::http) { return "http"; }
[[nodiscard]] constexpr std::string_view word_of(notify_backend::native) { return "native"; }
[[nodiscard]] constexpr std::string_view word_of(notify_backend::built_in) { return "built-in"; }
[[nodiscard]] constexpr std::string_view word_of(notify_mode::by_default) { return "default"; }
[[nodiscard]] constexpr std::string_view word_of(notify_mode::all) { return "all"; }
[[nodiscard]] constexpr std::string_view word_of(notify_mode::mentions) { return "mentions"; }
[[nodiscard]] constexpr std::string_view word_of(notify_mode::off) { return "off"; }
template <class... Ts>
[[nodiscard]] std::string word_of(const spl::variant<Ts...>& one) {
  return std::string(spl::visit([](auto each) { return word_of(each); }, one));
}
// What a user reads for a proxy's kind.
[[nodiscard]] constexpr std::string_view label_of(proxy_kind::socks5) { return "SOCKS5"; }
[[nodiscard]] constexpr std::string_view label_of(proxy_kind::http) { return "HTTP"; }
[[nodiscard]] inline std::string_view label_of(const proxy_kind_t& one) {
  return spl::visit([](auto each) { return label_of(each); }, one);
}

}  // namespace mux::config
