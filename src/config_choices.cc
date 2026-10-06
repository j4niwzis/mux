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
  static constexpr bool light = true;
  friend bool operator==(classic, classic) = default;
};
struct day {
  static constexpr bool light = true;
  friend bool operator==(day, day) = default;
};
struct tinted {
  static constexpr bool light = false;
  friend bool operator==(tinted, tinted) = default;
};
struct night {
  static constexpr bool light = false;
  friend bool operator==(night, night) = default;
};
}  // namespace theme
using theme_t = spl::variant<theme::classic, theme::day, theme::tinted, theme::night>;
// The accent a theme is drawn with: its own, or one of Telegram's circles --
// each a shade of its own in each theme.
namespace accent {
struct theme_own {
  friend bool operator==(theme_own, theme_own) = default;
};
struct blue {
  friend bool operator==(blue, blue) = default;
};
struct green {
  friend bool operator==(green, green) = default;
};
struct pink {
  friend bool operator==(pink, pink) = default;
};
struct orange {
  friend bool operator==(orange, orange) = default;
};
struct purple {
  friend bool operator==(purple, purple) = default;
};
struct red {
  friend bool operator==(red, red) = default;
};
struct grey {
  friend bool operator==(grey, grey) = default;
};
struct gold {
  friend bool operator==(gold, gold) = default;
};
}  // namespace accent
using accent_t = spl::variant<accent::theme_own, accent::blue, accent::green, accent::pink, accent::orange,
                              accent::purple, accent::red, accent::grey, accent::gold>;
namespace renderer {
struct opengl {
  friend bool operator==(opengl, opengl) = default;
};
struct software {
  friend bool operator==(software, software) = default;
};
}  // namespace renderer
using renderer_t = spl::variant<renderer::opengl, renderer::software>;
namespace proxy_kind {
struct socks5 {
  friend bool operator==(socks5, socks5) = default;
};
struct http {
  friend bool operator==(http, http) = default;
};
}  // namespace proxy_kind
using proxy_kind_t = spl::variant<proxy_kind::socks5, proxy_kind::http>;

// The words of the file, and what they mean: anything else is the default.
// A word of the file looked up in a table of the ones known; the default
// where it is none of them, or not there.
template <class Variant>
[[nodiscard]] Variant word_of(const std::unordered_map<std::string_view, Variant>& known,
                              std::optional<std::string_view> word, Variant otherwise) {
  if (!word)
    return otherwise;
  const auto found = known.find(*word);
  return found == known.end() ? otherwise : found->second;
}
[[nodiscard]] inline theme_t theme_of(const std::optional<std::string>& word) {
  static const std::unordered_map<std::string_view, theme_t> known = {
      {"classic", theme::classic{}},
      {"day", theme::day{}},
      {"light", theme::day{}},  // mux's own light, before
      {"night", theme::night{}},
      {"dark", theme::night{}},  // mux's own dark, before
      {"tinted", theme::tinted{}}};
  return word_of<theme_t>(known, word, theme::tinted{});
}
[[nodiscard]] inline accent_t accent_of(const std::optional<std::string>& word) {
  static const std::unordered_map<std::string_view, accent_t> known = {
      {"blue", accent::blue{}},     {"cyan", accent::blue{}},   {"green", accent::green{}},
      {"pink", accent::pink{}},     {"orange", accent::orange{}}, {"purple", accent::purple{}},
      {"red", accent::red{}},       {"grey", accent::grey{}},   {"gold", accent::gold{}}};
  return word_of<accent_t>(known, word, accent::theme_own{});
}
[[nodiscard]] inline renderer_t renderer_of(const std::optional<std::string>& word) {
  static const std::unordered_map<std::string_view, renderer_t> known = {{"software", renderer::software{}},
                                                                         {"opengl", renderer::opengl{}}};
  return word_of<renderer_t>(known, word, renderer::opengl{});
}
// How a notification is shown: by the desktop's own service
// (org.freedesktop.Notifications, over D-Bus), or by mux, as tdesktop's
// own: a small window in a corner of the screen.
namespace notify_backend {
struct native {
  friend bool operator==(native, native) = default;
};
struct built_in {
  friend bool operator==(built_in, built_in) = default;
};
}  // namespace notify_backend
using notify_backend_t = spl::variant<notify_backend::native, notify_backend::built_in>;
[[nodiscard]] inline notify_backend_t notify_backend_of(const std::optional<std::string>& word) {
  static const std::unordered_map<std::string_view, notify_backend_t> known = {
      {"native", notify_backend::native{}}, {"built-in", notify_backend::built_in{}}};
  return word_of<notify_backend_t>(known, word, notify_backend::native{});
}
// A chat's own choice of what notifies, as Telegram's and Element's: as
// its account says, everything, only what mentions the user, nothing.
namespace notify_mode {
struct by_default {
  friend bool operator==(by_default, by_default) = default;
};
struct all {
  friend bool operator==(all, all) = default;
};
struct mentions {
  friend bool operator==(mentions, mentions) = default;
};
struct off {
  friend bool operator==(off, off) = default;
};
}  // namespace notify_mode
using notify_mode_t = spl::variant<notify_mode::by_default, notify_mode::all, notify_mode::mentions, notify_mode::off>;
[[nodiscard]] inline notify_mode_t notify_mode_of(const std::optional<std::string>& word) {
  static const std::unordered_map<std::string_view, notify_mode_t> known = {{"all", notify_mode::all{}},
                                                                            {"mentions", notify_mode::mentions{}}};
  return word_of<notify_mode_t>(known, word, notify_mode::by_default{});
}
// The switches of the notifications page, each a member of its settings.
namespace notify_flag {
struct desktop {};
struct show_name {};
struct show_text {};
struct sound {};
}  // namespace notify_flag
using notify_flag_t = spl::variant<notify_flag::desktop, notify_flag::show_name, notify_flag::show_text, notify_flag::sound>;

[[nodiscard]] inline proxy_kind_t proxy_kind_of(std::string_view word) {
  static const std::unordered_map<std::string_view, proxy_kind_t> known = {{"http", proxy_kind::http{}},
                                                                           {"socks5", proxy_kind::socks5{}}};
  return word_of<proxy_kind_t>(known, word, proxy_kind::socks5{});
}
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
