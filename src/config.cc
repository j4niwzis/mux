// SPDX-License-Identifier: AGPL-3.0-only
// mux.config: the accounts mux keeps between runs, in
// $XDG_CONFIG_HOME/mux/accounts.json (~/.config/mux/accounts.json when that
// is not set). The passwords are in it, so the file is made readable by its
// owner and no one else before anything is written into it, and replaced
// whole, so a crash halfway through a save leaves the old file.
export module mux.config;

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
using theme_t = splice::variant<theme::classic, theme::day, theme::tinted, theme::night>;
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
using accent_t = splice::variant<accent::theme_own, accent::blue, accent::green, accent::pink, accent::orange,
                              accent::purple, accent::red, accent::grey, accent::gold>;
namespace renderer {
struct opengl {
  friend bool operator==(opengl, opengl) = default;
};
struct software {
  friend bool operator==(software, software) = default;
};
}  // namespace renderer
using renderer_t = splice::variant<renderer::opengl, renderer::software>;
namespace proxy_kind {
struct socks5 {
  friend bool operator==(socks5, socks5) = default;
};
struct http {
  friend bool operator==(http, http) = default;
};
}  // namespace proxy_kind
using proxy_kind_t = splice::variant<proxy_kind::socks5, proxy_kind::http>;

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
using notify_backend_t = splice::variant<notify_backend::native, notify_backend::built_in>;
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
using notify_mode_t = splice::variant<notify_mode::by_default, notify_mode::all, notify_mode::mentions, notify_mode::off>;
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
using notify_flag_t = splice::variant<notify_flag::desktop, notify_flag::show_name, notify_flag::show_text, notify_flag::sound>;

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
[[nodiscard]] std::string word_of(const splice::variant<Ts...>& one) {
  return std::string(splice::visit([](auto each) { return word_of(each); }, one));
}
// What a user reads for a proxy's kind.
[[nodiscard]] constexpr std::string_view label_of(proxy_kind::socks5) { return "SOCKS5"; }
[[nodiscard]] constexpr std::string_view label_of(proxy_kind::http) { return "HTTP"; }
[[nodiscard]] inline std::string_view label_of(const proxy_kind_t& one) {
  return splice::visit([](auto each) { return label_of(each); }, one);
}

// A proxy, as a named profile of the program's list -- as Gajim keeps them
// -- that accounts choose by its name: SOCKS5 or HTTP CONNECT.
struct proxy_settings {
  std::string name;
  std::string kind = "socks5";  // "socks5" or "http"
  std::string host;
  std::int64_t port = 1080;
  std::optional<std::string> username;
  std::optional<std::string> password;
  // Who XMPP's SRV records are asked of, through it: a nameserver's address;
  // "off" for none (the domain itself on 5222, resolved by the proxy);
  // unset, the system's own nameserver, where the proxy can reach it.
  std::optional<std::string> srv_resolver;
  friend bool operator==(const proxy_settings&, const proxy_settings&) = default;
};

// Which kinds of room events show, each as chosen: nothing said is as the
// level under says -- a chat's its account's, an account's every one's.
struct room_event_kinds {
  std::optional<bool> joins, invites, names, avatars, room_name, topic, room_avatar, address, pins, permissions,
      access, encryption, other, unreadable, reactions, unreactions;
  friend bool operator==(const room_event_kinds&, const room_event_kinds&) = default;
};
consteval auto json_schema(knot::type<room_event_kinds>) { return knot::schema<room_event_kinds>(); }

// The mentions and reactions not yet seen, kept between runs, by chat.
struct kept_mark {
  std::string event;
  std::string target;
  std::int64_t at = 0;  // milliseconds since the epoch
  friend bool operator==(const kept_mark&, const kept_mark&) = default;
};
struct chat_marks {
  std::string account;
  std::string conversation;
  std::vector<kept_mark> mentions;
  std::vector<kept_mark> reactions;
  std::optional<std::vector<std::string>> seen;  // marks seen or gone to, by their event
  friend bool operator==(const chat_marks&, const chat_marks&) = default;
};
struct marks_file {
  std::vector<chat_marks> chats;
  friend bool operator==(const marks_file&, const marks_file&) = default;
};
consteval auto json_schema(knot::type<kept_mark>) { return knot::schema<kept_mark>(); }
consteval auto json_schema(knot::type<chat_marks>) { return knot::schema<chat_marks>(); }
consteval auto json_schema(knot::type<marks_file>) { return knot::schema<marks_file>(); }

// What notifies, and how, as Telegram Desktop's settings have it: a
// notification on the desktop, with the sender's name and the message's
// text or not, and a sound -- the chime, or a file -- by one backend.
struct notification_settings {
  bool desktop = true;
  bool show_name = true;
  bool show_text = true;
  bool sound = true;
  std::string backend = "native";
  std::optional<std::string> sound_file;
  // Woken by UnifiedPush, through the desktop's distributor (its D-Bus
  // specification) -- off unless chosen: only then is the bus asked, and
  // the servers given a pusher. The connection token, kept across runs, and
  // the endpoint the distributor gave for it.
  std::optional<bool> unified_push;
  std::optional<std::string> push_token;
  std::optional<std::string> push_endpoint;
  friend bool operator==(const notification_settings&, const notification_settings&) = default;
};
consteval auto json_schema(knot::type<notification_settings>) { return knot::schema<notification_settings>(); }
[[nodiscard]] constexpr bool notification_settings::* flag_member(notify_flag::desktop) { return &notification_settings::desktop; }
[[nodiscard]] constexpr bool notification_settings::* flag_member(notify_flag::show_name) { return &notification_settings::show_name; }
[[nodiscard]] constexpr bool notification_settings::* flag_member(notify_flag::show_text) { return &notification_settings::show_text; }
[[nodiscard]] constexpr bool notification_settings::* flag_member(notify_flag::sound) { return &notification_settings::sound; }
[[nodiscard]] inline bool& flag_in(notification_settings& in, const notify_flag_t& flag) {
  return in.*splice::visit([](auto one) { return flag_member(one); }, flag);
}
// A chat's own choice of what notifies: everything, or what mentions the
// user -- muted chats are kept apart, as before.
struct chat_notify {
  std::string account;
  std::string conversation;
  std::string mode;
  friend bool operator==(const chat_notify&, const chat_notify&) = default;
};
consteval auto json_schema(knot::type<chat_notify>) { return knot::schema<chat_notify>(); }

// An XMPP account: a JID and how to reach its server.
// What every saved account keeps, whatever its protocol: on or off, and the
// client's settings of it. What a protocol keeps of its own -- its address,
// its password, its server -- is its kept type, mux::proto::<protocol>::kept
// (through mux.proto.kept).
struct account_shared {
  bool enabled = true;
  std::optional<bool> read_receipts;
  std::optional<bool> send_typing;  // others' typing is always shown
  std::optional<bool> room_events;
  std::optional<room_event_kinds> room_event_kinds;
  std::optional<bool> show_receipts;
  std::optional<bool> link_previews;
  std::optional<bool> previews_direct;  // link previews fetched from the site itself
  std::optional<std::string> wallpaper;  // its chats' background, as word_of(wallpaper_t) says it
  std::optional<std::string> bubbles;    // its chats' bubbles, as word_of(bubble_look) says them
  std::optional<std::string> panels;     // its panels' look
  std::optional<bool> home_hides_spaced;  // Home without what spaces hold, but direct messages
  std::optional<bool> home_hides_direct;  // and without direct messages too, where it is so
  std::optional<std::int64_t> jump_search;
  std::optional<bool> notify;
  std::optional<bool> notify_sound;
  std::optional<std::string> proxy;
  std::optional<std::string> colour;
  std::optional<bool> strip;
  friend bool operator==(const account_shared&, const account_shared&) = default;
};
consteval auto json_schema(knot::type<account_shared>) { return knot::schema<account_shared>(); }

// What each protocol keeps of its own: its kept type, found by ADL on its tag
// -- kept_type(state) -- for every protocol of the list.
template <class Tag>
using kept_of = typename decltype(kept_type(state_of<Tag>{}))::type;
template <class>
struct kept_list;
template <class... Tags>
struct kept_list<protocol_list<Tags...>> {
  using held = splice::variant<kept_of<Tags>...>;
  // As the file has it, chosen by its "protocol": an account of a protocol
  // this build has not kept as it was, and written back so -- not lost.
  using saved = knot::tagged<"protocol", kept_of<Tags>..., knot::value>;
};
using kept_t = kept_list<protocols>::held;
using kept_saved_t = kept_list<protocols>::saved;

// One saved account: what its protocol keeps of its own, and what every
// account keeps.
struct account_t {
  kept_t own;
  account_shared shared;
  friend bool operator==(const account_t&, const account_t&) = default;
};

// An account as the file has it: {"protocol": "matrix", "own": {...},
// "shared": {...}}.
struct saved_account {
  std::string protocol;
  kept_saved_t own;
  account_shared shared;
  friend bool operator==(const saved_account&, const saved_account&) = default;
};
consteval auto json_schema(knot::type<saved_account>) { return knot::schema<saved_account>(); }



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
using wallpaper_t = splice::variant<wallpaper::theme, wallpaper::plain, wallpaper::picture>;
[[nodiscard]] inline std::string word_of(const wallpaper_t& one) {
  return splice::visit(splice::overloaded{[](wallpaper::theme) { return std::string("theme"); },
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
using space_item_t = splice::variant<space_item::home, space_item::direct, space_item::space>;
[[nodiscard]] inline std::string word_of(const space_item_t& one) {
  return splice::visit(splice::overloaded{[](space_item::home) { return std::string("home"); },
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
using space_bar_t = splice::variant<space_bar::side, space_bar::top, space_bar::hidden>;
[[nodiscard]] inline std::string_view word_of(const space_bar_t& one) {
  return splice::visit(splice::overloaded{[](space_bar::side) { return std::string_view("side"); },
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
using bubbles_t = splice::variant<bubbles::solid, bubbles::translucent, bubbles::frosted, bubbles::glass>;
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
using look_part_t = splice::variant<look_part::bubbles, look_part::panels>;
[[nodiscard]] inline std::string word_of(const bubble_look& one) {
  const std::string_view kind = splice::visit(splice::overloaded{[](bubbles::solid) { return std::string_view("solid"); },
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
    splice::variant<wallpaper_pick::inherit, wallpaper_pick::theme, wallpaper_pick::plain, wallpaper_pick::picture>;

// A chat's own choice of whether what is done in it is shown.
struct room_events_choice {
  std::string account;       // the account's address
  std::string conversation;  // the chat's id in it
  std::optional<bool> show;  // all of them
  std::optional<room_event_kinds> kinds;  // each kind
  std::optional<bool> receipts;  // who has read up to where, as faces
  std::optional<bool> previews;  // a card for a message's first link
  std::optional<bool> previews_direct;  // fetched from the site itself, not the server
  std::optional<bool> typing;    // others told one is typing
  std::optional<std::int64_t> jump_search;  // events paged back looking for one; 0 no limit
  std::optional<std::string> wallpaper;  // its background, as word_of(wallpaper_t) says it
  std::optional<bool> forum;  // a space: shown as one chat, its rooms in it as topics
  std::optional<bool> hide_from_home;  // a space: its rooms not in Home
  std::optional<std::string> bubbles;    // its bubbles, as word_of(bubble_look) says them
  std::optional<std::string> panels;  // its panels' look, as word_of(bubble_look) says it
  friend bool operator==(const room_events_choice&, const room_events_choice&) = default;
};
consteval auto json_schema(knot::type<room_events_choice>) { return knot::schema<room_events_choice>(); }

// A chat listed in another account's list than its own -- a copy of its row
// there, or moved there -- the strip on its left as it chose: its colour,
// else its account's; shown, else as its account says.
struct chat_placement {
  std::string account;       // the chat's account, by its address
  std::string conversation;  // the chat's id in it
  std::string listed_in;     // the account whose list it is in, by its address
  bool moved = false;        // out of its own account's list
  std::optional<std::string> strip_colour;
  std::optional<bool> strip;
  friend bool operator==(const chat_placement&, const chat_placement&) = default;
};
consteval auto json_schema(knot::type<chat_placement>) { return knot::schema<chat_placement>(); }

// A chat muted: no notifications from it, its unread count in grey.
struct muted_chat {
  std::string account;       // the account's address
  std::string conversation;  // the chat's id in it
  friend bool operator==(const muted_chat&, const muted_chat&) = default;
};
// A sticker kept -- sent lately, or a favourite: what sending it again takes.
struct sticker_kept {
  std::string shortcode;
  std::string url;
  std::string body;
  std::optional<std::int64_t> w, h, size;
  std::optional<std::string> mimetype;
  friend bool operator==(const sticker_kept&, const sticker_kept&) = default;
  friend consteval auto json_schema(knot::type<sticker_kept>) { return knot::schema<sticker_kept>(); }
};

// The file: a list for each protocol, so each entry says what it is by where
// it is, and has only its own protocol's keys.
// How much is kept, in memory and on disk: what the Storage page sets.
struct cache_limits {
  std::int64_t messages_in_memory = 5000;
  std::int64_t messages_on_disk_mb = 512;
  std::int64_t pictures_in_memory_mb = 32;
  std::int64_t pictures_on_disk_mb = 512;
  // Deleted messages kept on disk, apart from the rest: none said is 256.
  std::optional<std::int64_t> deleted_on_disk_mb;
  friend bool operator==(const cache_limits&, const cache_limits&) = default;
};
consteval auto json_schema(knot::type<cache_limits>) { return knot::schema<cache_limits>(); }
// Which of them, as the page names them.
namespace limit {
struct messages_in_memory {
  friend bool operator==(messages_in_memory, messages_in_memory) = default;
};
struct messages_on_disk {
  friend bool operator==(messages_on_disk, messages_on_disk) = default;
};
struct pictures_in_memory {
  friend bool operator==(pictures_in_memory, pictures_in_memory) = default;
};
struct pictures_on_disk {
  friend bool operator==(pictures_on_disk, pictures_on_disk) = default;
};
struct deleted_on_disk {
  friend bool operator==(deleted_on_disk, deleted_on_disk) = default;
};
}  // namespace limit
using limit_t = splice::variant<limit::messages_in_memory, limit::messages_on_disk, limit::pictures_in_memory,
                             limit::pictures_on_disk, limit::deleted_on_disk>;
inline constexpr std::int64_t kDeletedOnDiskMb = 256;
[[nodiscard]] inline std::int64_t deleted_on_disk_of(const cache_limits& all) {
  return all.deleted_on_disk_mb.value_or(kDeletedOnDiskMb);
}
// A limit's number, and its bounds: what it can be halved or doubled to.
// Each limit's, by overloads; a limit_t's, by visiting them.
[[nodiscard]] inline std::int64_t& value_of(cache_limits& all, limit::messages_in_memory) {
  return all.messages_in_memory;
}
[[nodiscard]] inline std::int64_t& value_of(cache_limits& all, limit::messages_on_disk) {
  return all.messages_on_disk_mb;
}
[[nodiscard]] inline std::int64_t& value_of(cache_limits& all, limit::pictures_in_memory) {
  return all.pictures_in_memory_mb;
}
[[nodiscard]] inline std::int64_t& value_of(cache_limits& all, limit::pictures_on_disk) {
  return all.pictures_on_disk_mb;
}
[[nodiscard]] inline std::int64_t& value_of(cache_limits& all, limit::deleted_on_disk) {
  if (!all.deleted_on_disk_mb)
    all.deleted_on_disk_mb = kDeletedOnDiskMb;
  return *all.deleted_on_disk_mb;
}
[[nodiscard]] inline std::int64_t& value_of(cache_limits& all, const limit_t& which) {
  return splice::visit([&](auto one) -> std::int64_t& { return value_of(all, one); }, which);
}
// Messages are counted, pictures weighed in MiB.
[[nodiscard]] inline std::pair<std::int64_t, std::int64_t> bounds_of(limit::messages_in_memory) { return {250, 200000}; }
[[nodiscard]] inline std::pair<std::int64_t, std::int64_t> bounds_of(limit::messages_on_disk) { return {4, 65536}; }
[[nodiscard]] inline std::pair<std::int64_t, std::int64_t> bounds_of(limit::pictures_in_memory) { return {4, 65536}; }
[[nodiscard]] inline std::pair<std::int64_t, std::int64_t> bounds_of(limit::pictures_on_disk) { return {4, 65536}; }
[[nodiscard]] inline std::pair<std::int64_t, std::int64_t> bounds_of(limit::deleted_on_disk) { return {4, 65536}; }
[[nodiscard]] inline std::pair<std::int64_t, std::int64_t> bounds_of(const limit_t& which) {
  return splice::visit([](auto one) { return bounds_of(one); }, which);
}

// What is done to a picture dropped on the window before it is sent.
struct sending_settings {
  bool strip_metadata = true;  // its EXIF, XMP, text and the like cut out
  bool rename = true;          // named image.<its type>
  friend bool operator==(const sending_settings&, const sending_settings&) = default;
};
consteval auto json_schema(knot::type<sending_settings>) { return knot::schema<sending_settings>(); }

// What is kept of the history beyond what the servers keep.
struct history_settings {
  // A message deleted is shown where it was, all it said, marked; off, it
  // is gone from the chat. Either way it is kept on disk.
  bool show_deleted = false;
  // What is done in a room -- joins and leaves, a name or a picture changed,
  // events nothing here reads -- shown as lines of their own, or not. Kept
  // either way; an account's choice, and a room's own, come first.
  bool show_room_events = true;
  // Who has read up to where, as Element shows it: small faces under the
  // message each person read up to. Off unless chosen.
  bool show_receipts = false;
  // A card under a message for its first link, fetched through the
  // account's server. On unless turned off.
  bool link_previews = true;
  // Link previews fetched from the site itself, through the account's proxy,
  // instead of through its server: off -- the site then sees where
  // the request comes from.
  // Optional, as every field added to a kept file after it was first
  // written: knot requires the others, and a file saved before this one
  // came would no longer be read. Unset: off.
  std::optional<bool> previews_direct;
  // How many events a search for a message jumped to (a reply's, a link's)
  // pages back through before it gives up; 0 for no limit.
  std::int64_t jump_search = 5000;
  // And each kind of them, where chosen apart.
  std::optional<room_event_kinds> room_event_kinds;
  // Whether others are told one is typing (m.typing, XEP-0085's chat
  // states) -- never what: as every account's, until chosen there or in a
  // chat or its space.
  std::optional<bool> send_typing;  // unset: sent (and optional, as previews_direct)
  friend bool operator==(const history_settings&, const history_settings&) = default;
};
consteval auto json_schema(knot::type<history_settings>) { return knot::schema<history_settings>(); }

struct file {
  // The accounts, of every protocol, in order.
  std::optional<std::vector<saved_account>> accounts;
  // How much the window moves: "none", "reduced" (sections unfold, panels
  // just appear) or "full". Nothing said is full.
  std::optional<std::string> motion;
  // The account shown last, by its address: shown again at the next start.
  std::optional<std::string> last_account;
  // The emoji picked lately, newest first.
  std::optional<std::vector<std::string>> recent_emoji;
  // The stickers sent lately, newest first; and those made favourites.
  std::optional<std::vector<sticker_kept>> recent_stickers;
  std::optional<std::vector<sticker_kept>> favourite_stickers;
  std::optional<std::vector<muted_chat>> muted;
  // The chats listed in other accounts' lists than their own.
  std::optional<std::vector<chat_placement>> placements;
  // The chats that chose for themselves whether their room events show.
  std::optional<std::vector<room_events_choice>> room_events;
  // The proxy profiles accounts choose from.
  std::optional<std::vector<proxy_settings>> proxies;
  // The theme, "dark" or "light", and what draws the window, "opengl" or
  // "software". Nothing said is dark and OpenGL.
  std::optional<std::string> theme;
  std::optional<std::string> accent;
  // Every chat's background, as word_of(wallpaper_t) says it; none, the theme's.
  std::optional<std::string> wallpaper;
  // Every chat's bubbles, as word_of(bubble_look) says them; none, solid.
  std::optional<std::string> bubbles;
  std::optional<std::string> panels;  // its panels' look, as word_of(bubble_look) says it
  std::optional<bool> home_hides_spaced;  // Home without what spaces hold, but direct messages
  std::optional<bool> home_hides_direct;  // and without direct messages too, where it is so
  std::optional<std::string> renderer;
  // Only what changed repainted, into a frame kept between them.
  std::optional<bool> partial_redraw;
  // What each frame repainted, outlined: to see that only that is.
  std::optional<bool> flash_redraws;
  // Frames shown in step with the screen's refresh; off, as fast as drawn.
  std::optional<bool> vsync;
  // The window's opacity in percent (none, 100: opaque); and whether the
  // chat's background is behind the whole window, not only its messages.
  std::optional<int> window_opacity;
  std::optional<bool> wallpaper_behind;
  // Frosted popups and sheets blurring what is really under them, live.
  std::optional<bool> live_blur;
  // How much Frosted blurs what is behind, in percent (none, 30).
  // Frosted's blur, in percent of the most -- three times what it was
  // once (the old frost_blur's 100 is 33.3 here), any fraction of it.
  std::optional<double> frost;
  // The old setting, read where the new is not there yet: a third of it.
  std::optional<int> frost_blur;
  // Spaces in bars at all (none, yes); the bar along the top (none, yes);
  // and where each item is put, in order.
  std::optional<bool> spaces;
  std::optional<bool> top_bar;
  std::optional<std::vector<space_place>> space_places;
  // Frames a second, and the last frame's time, in the window's corner.
  std::optional<bool> show_fps;
  // The interface's scale, in percent of the display's own (none, 100): as
  // Telegram Desktop's "Interface scale".
  std::optional<int> interface_scale;
  std::optional<cache_limits> cache;
  std::optional<sending_settings> sending;
  std::optional<history_settings> history;
  std::optional<notification_settings> notifications;
  std::optional<std::vector<chat_notify>> chat_notify;
  friend bool operator==(const file&, const file&) = default;
};

consteval auto json_schema(knot::type<proxy_settings>) { return knot::schema<proxy_settings>(); }
consteval auto json_schema(knot::type<muted_chat>) { return knot::schema<muted_chat>(); }
consteval auto json_schema(knot::type<file>) { return knot::schema<file>(); }

// What an account is known by: what its protocol says -- a JID, a user ID.
[[nodiscard]] inline const std::string& address_of(const account_t& one) noexcept {
  return splice::visit([](const auto& each) -> const std::string& { return address_of(each); }, one.own);
}

[[nodiscard]] inline bool& enabled_of(account_t& one) noexcept {
  return one.shared.enabled;
}
[[nodiscard]] inline bool enabled_of(const account_t& one) noexcept {
  return one.shared.enabled;
}

// Whether an account sends read receipts, and the proxy it goes through.
[[nodiscard]] inline bool read_receipts_of(const account_t& one) {
  return one.shared.read_receipts.value_or(true);
}
// Whether an account sends room keys to verified sessions only: where its
// protocol keeps that (only_verified_in(kept), by ADL); none for another.
namespace only_verified_defaults {
template <class Kept>
[[nodiscard]] auto only_verified_in(Kept&) -> std::conditional_t<std::is_const_v<Kept>, const std::optional<bool>*, std::optional<bool>*> {
  return nullptr;
}
}  // namespace only_verified_defaults
[[nodiscard]] inline std::optional<bool>* only_verified_in(account_t& one) {
  return splice::visit([](auto& each) -> std::optional<bool>* {
    using only_verified_defaults::only_verified_in;
    return only_verified_in(each);
  }, one.own);
}
[[nodiscard]] inline bool only_verified_of(const account_t& one) {
  const std::optional<bool>* kept = splice::visit([](const auto& each) -> const std::optional<bool>* {
    using only_verified_defaults::only_verified_in;
    return only_verified_in(each);
  }, one.own);
  return kept && kept->value_or(false);
}
[[nodiscard]] inline std::optional<bool>& read_receipts_in(account_t& one) {
  return one.shared.read_receipts;
}
// An account's colour: its own choice, else one of the eight its address
// picks -- the same every time, and accounts apart mostly apart.
[[nodiscard]] inline accent_t default_colour_of(std::string_view address) {
  std::uint32_t hash = 2166136261u;
  for (const char c : address) {
    hash ^= static_cast<unsigned char>(c);
    hash *= 16777619u;
  }
  switch (hash % 8) {
    case 0: return accent::blue{};
    case 1: return accent::green{};
    case 2: return accent::pink{};
    case 3: return accent::orange{};
    case 4: return accent::purple{};
    case 5: return accent::red{};
    case 6: return accent::grey{};
    default: return accent::gold{};
  }
}
[[nodiscard]] inline accent_t colour_of(const account_t& one) {
  const std::optional<std::string>& word = one.shared.colour;
  return word ? accent_of(word) : default_colour_of(address_of(one));
}
[[nodiscard]] inline std::optional<std::string>& colour_in(account_t& one) {
  return one.shared.colour;
}
[[nodiscard]] inline bool strip_of(const account_t& one) {
  return one.shared.strip.value_or(true);
}
[[nodiscard]] inline std::optional<bool>& strip_in(account_t& one) {
  return one.shared.strip;
}
// Whether the account tells whom it talks to that the user is typing.
// What a passphrase is asked for, of the client's own: local data opened at
// the start; encrypted, a new one twice; another one, the one now first; or
// encryption turned off, the one now. A protocol's own purposes are its
// (passphrases(state)); proto::passphrase_for_t is them all.
namespace passphrase_for {
struct unlock {
  friend bool operator==(unlock, unlock) = default;
};
struct encrypt {
  friend bool operator==(encrypt, encrypt) = default;
};
struct change {
  friend bool operator==(change, change) = default;
};
struct decrypt {
  friend bool operator==(decrypt, decrypt) = default;
};
}  // namespace passphrase_for
// A new passphrase: not empty, long enough, the same twice -- or why not.
// Its key is made slowly (Argon2id, 64 MiB), but a short passphrase is still
// few guesses away for whoever has the files.
[[nodiscard]] inline std::optional<std::string> new_passphrase_refused(const std::string& fresh, const std::string& again) {
  if (fresh.empty())
    return "Type a passphrase.";
  if (std::ranges::distance(fresh | std::views::filter([](char c) { return (c & 0xC0) != 0x80; })) < 10)
    return "A passphrase of at least 10 characters.";
  if (fresh != again)
    return "The new passphrase is not the same twice.";
  return std::nullopt;
}

// Its own choice, if it made one; else as every account's.
[[nodiscard]] inline const std::optional<bool>& send_typing_of(const account_t& one) {
  return one.shared.send_typing;
}
[[nodiscard]] inline std::optional<bool>& send_typing_in(account_t& one) {
  return one.shared.send_typing;
}
// Whether the account's chats show their room events: its own choice, if
// it made one.
[[nodiscard]] inline const std::optional<std::int64_t>& jump_search_of(const account_t& one) {
  return one.shared.jump_search;
}
[[nodiscard]] inline std::optional<std::int64_t>& jump_search_in(account_t& one) {
  return one.shared.jump_search;
}
// An account's chats' background, as word_of(wallpaper_t) says it.
[[nodiscard]] inline const std::optional<std::string>& wallpaper_of(const account_t& one) {
  return one.shared.wallpaper;
}
[[nodiscard]] inline std::optional<std::string>& wallpaper_in(account_t& one) {
  return one.shared.wallpaper;
}
[[nodiscard]] inline const std::optional<std::string>& bubbles_of(const account_t& one) {
  return one.shared.bubbles;
}
[[nodiscard]] inline std::optional<std::string>& bubbles_in(account_t& one) {
  return one.shared.bubbles;
}
[[nodiscard]] inline const std::optional<std::string>& panels_of(const account_t& one) {
  return one.shared.panels;
}
[[nodiscard]] inline std::optional<std::string>& panels_in(account_t& one) {
  return one.shared.panels;
}
[[nodiscard]] inline const std::optional<bool>& home_hides_of(const account_t& one) {
  return one.shared.home_hides_spaced;
}
[[nodiscard]] inline const std::optional<bool>& home_direct_of(const account_t& one) {
  return one.shared.home_hides_direct;
}
[[nodiscard]] inline std::optional<bool>& home_direct_in(account_t& one) {
  return one.shared.home_hides_direct;
}
[[nodiscard]] inline std::optional<bool>& home_hides_in(account_t& one) {
  return one.shared.home_hides_spaced;
}
[[nodiscard]] inline const std::optional<bool>& link_previews_of(const account_t& one) {
  return one.shared.link_previews;
}
[[nodiscard]] inline std::optional<bool>& link_previews_in(account_t& one) {
  return one.shared.link_previews;
}
// Whether its chats' link previews come from the sites themselves.
[[nodiscard]] inline const std::optional<bool>& previews_direct_of(const account_t& one) {
  return one.shared.previews_direct;
}
[[nodiscard]] inline std::optional<bool>& previews_direct_in(account_t& one) {
  return one.shared.previews_direct;
}
[[nodiscard]] inline const std::optional<bool>& show_receipts_of(const account_t& one) {
  return one.shared.show_receipts;
}
[[nodiscard]] inline std::optional<bool>& show_receipts_in(account_t& one) {
  return one.shared.show_receipts;
}
[[nodiscard]] inline const std::optional<bool>& room_events_of(const account_t& one) {
  return one.shared.room_events;
}
[[nodiscard]] inline std::optional<bool>& room_events_in(account_t& one) {
  return one.shared.room_events;
}
[[nodiscard]] inline const std::optional<bool>& notify_of(const account_t& one) {
  return one.shared.notify;
}
[[nodiscard]] inline std::optional<bool>& notify_in(account_t& one) {
  return one.shared.notify;
}
[[nodiscard]] inline const std::optional<bool>& notify_sound_of(const account_t& one) {
  return one.shared.notify_sound;
}
[[nodiscard]] inline std::optional<bool>& notify_sound_in(account_t& one) {
  return one.shared.notify_sound;
}
[[nodiscard]] inline const std::optional<room_event_kinds>& room_event_kinds_of(const account_t& one) {
  return one.shared.room_event_kinds;
}
[[nodiscard]] inline std::optional<room_event_kinds>& room_event_kinds_in(account_t& one) {
  return one.shared.room_event_kinds;
}
[[nodiscard]] inline std::optional<std::string>& proxy_in(account_t& one) {
  return one.shared.proxy;
}
[[nodiscard]] inline const std::optional<std::string>& proxy_of(const account_t& one) {
  return one.shared.proxy;
}
// The profile of that name, where there is one.
[[nodiscard]] inline const proxy_settings* find_proxy(const std::vector<proxy_settings>& all,
                                                      const std::optional<std::string>& name) {
  if (!name)
    return nullptr;
  const auto found = std::ranges::find(all, *name, &proxy_settings::name);
  return found == all.end() ? nullptr : &*found;
}

// The name of an account's protocol, as the user reads it.
[[nodiscard]] inline std::string_view protocol_name(const account_t& one) noexcept {
  return splice::visit([](const auto& each) { return protocol_name(each); }, one.own);
}

constexpr bool is_matrix(std::string_view address) noexcept { return address.starts_with('@'); }

// An account from an address alone, as on the command line: the first
// protocol of the list that owns the address -- its owns_address(state, ...).
template <class... Tags>
[[nodiscard]] account_t account_from(protocol_list<Tags...>, std::string address, std::string password) {
  std::optional<account_t> made;
  (void)((owns_address(state_of<Tags>{}, address) ? (made = account_t{.own = kept_t{kept_from(state_of<Tags>{}, address, password)}}, true) : false) ||
         ...);
  return made.value_or(account_t{});
}
[[nodiscard]] inline account_t account_from(std::string address, std::string password) {
  return account_from(protocols{}, std::move(address), std::move(password));
}

// The accounts of a file as the program holds them, and back.
[[nodiscard]] inline std::optional<account_t> account_of(const saved_account& one) {
  return splice::visit(splice::overloaded{[](const knot::value&) { return std::optional<account_t>(); },
                                          [&](const auto& own) {
                                            return std::optional<account_t>(account_t{.own = kept_t{own}, .shared = one.shared});
                                          }},
                       one.own.data());
}
[[nodiscard]] inline saved_account saved_of(const account_t& one) {
  return splice::visit([&](const auto& own) {
    return saved_account{.protocol = std::string(protocol_word(own)), .own = kept_saved_t{own}, .shared = one.shared};
  }, one.own);
}
// What an old file kept flat in each account, as every account keeps it now.
// All the accounts of a file of a protocol this build has (an old file's
// lists among them, read into accounts as load() reads it).
[[nodiscard]] inline std::vector<account_t> accounts_of(const file& from) {
  std::vector<account_t> out;
  for (const auto& one : from.accounts.value_or(std::vector<saved_account>{}))
    if (auto held = account_of(one))
      out.push_back(std::move(*held));
  return out;
}
// The accounts of a protocol this build has not: kept to be written back.
[[nodiscard]] inline std::vector<saved_account> foreign_of(const file& from) {
  return from.accounts.value_or(std::vector<saved_account>{}) |
         std::views::filter([](const saved_account& one) { return !account_of(one).has_value(); }) |
         std::ranges::to<std::vector>();
}
[[nodiscard]] inline file file_of(std::span<const account_t> accounts, std::span<const saved_account> foreign = {}) {
  file out;
  out.accounts = accounts | std::views::transform([](const account_t& one) { return saved_of(one); }) |
                 std::ranges::to<std::vector>();
  out.accounts->append_range(foreign);
  return out;
}

// What is wrong with an account as typed: its protocol's check(kept), by ADL.
std::optional<std::string> check(const account_t& one) {
  return splice::visit([](const auto& each) { return check(each); }, one.own);
}

// Where what the program keeps between runs, and could make again, is put:
// $XDG_STATE_HOME/mux, or ~/.local/state/mux.
std::filesystem::path state_path(std::string_view name) {
  if (const char* xdg = std::getenv("XDG_STATE_HOME"); xdg && *xdg)
    return std::filesystem::path(xdg) / "mux" / name;
  if (const char* home = std::getenv("HOME"); home && *home)
    return std::filesystem::path(home) / ".local" / "state" / "mux" / name;
  return std::filesystem::path(std::format("mux-{}", name));
}

// A name -- an address, a room's id, a media source -- as a file's: kept as
// it is where it is plain (letters, digits, '@', '-', '_', and '.' but not
// first), every other byte as %XX -- '%' too, and a leading '.', so no "."
// or ".." and nothing hidden. One name to one file: before, every other
// character became '_', and "!a:b" and "!a_b" shared one.
[[nodiscard]] inline std::string file_name_of(std::string_view name) {
  const auto as_file = [name](std::size_t at) {
    const auto c = static_cast<unsigned char>(name[at]);
    const bool plain = std::isalnum(c) != 0 || c == '@' || c == '-' || c == '_' || (c == '.' && at > 0);
    constexpr std::string_view digits = "0123456789ABCDEF";
    return plain ? std::string(1, name[at]) : std::string{'%', digits[c >> 4], digits[c & 15u]};
  };
  std::string out = std::views::iota(std::size_t{0}, name.size()) | std::views::transform(as_file) | std::views::join |
                    std::ranges::to<std::string>();
  return out.empty() ? std::string("%") : out;
}
// The name as files were named before: to find what was kept under it.
[[nodiscard]] inline std::string old_file_name_of(std::string_view name) {
  return name | std::views::transform([](char c) {
           return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '@' || c == '.' || c == '-' ? c : '_';
         }) |
         std::ranges::to<std::string>();
}
// Where something is kept: what was kept where it was before moved there,
// where it is still there and nothing is where it goes now.
inline std::filesystem::path moved_from(std::filesystem::path now, const std::filesystem::path& before) {
  if (now == before)
    return now;
  std::error_code failed;
  if (!std::filesystem::exists(now, failed) && std::filesystem::exists(before, failed)) {
    std::filesystem::create_directories(now.parent_path(), failed);
    std::filesystem::rename(before, now, failed);
  }
  return now;
}

// Where what can be fetched again is kept: $XDG_CACHE_HOME/mux, or
// ~/.cache/mux.
std::filesystem::path cache_path(std::string_view name) {
  if (const char* xdg = std::getenv("XDG_CACHE_HOME"); xdg && *xdg)
    return std::filesystem::path(xdg) / "mux" / name;
  if (const char* home = std::getenv("HOME"); home && *home)
    return std::filesystem::path(home) / ".cache" / "mux" / name;
  return std::filesystem::path(std::format("mux-cache-{}", name));
}

std::filesystem::path default_path() {
  if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg)
    return std::filesystem::path(xdg) / "mux" / "accounts.json";
  if (const char* home = std::getenv("HOME"); home && *home)
    return std::filesystem::path(home) / ".config" / "mux" / "accounts.json";
  return std::filesystem::path("mux-accounts.json");
}

// The accounts kept at `where`: none when there is no file yet, and what is
// wrong with it when there is one that cannot be read.
// A protocol with no old list of its own: a list no file has.
namespace legacy_defaults {
template <class Entry>
struct none {
  std::optional<std::vector<Entry>> no_legacy_accounts;
};
template <class Entry>
consteval auto json_schema(knot::type<none<Entry>>) {
  return knot::schema<none<Entry>>();
}
template <class Entry>
constexpr type_tag<none<Entry>> legacy_accounts_type(const auto&, type_tag<Entry>) {
  return {};
}
template <class Entry>
const std::optional<std::vector<Entry>>& legacy_entries(const none<Entry>& all) {
  return all.no_legacy_accounts;
}
}  // namespace legacy_defaults
// An old file's lists, each its protocol's (legacy_accounts_type): its
// entries read once as the protocol's own and once as every account's, the
// two zipped into accounts as files have them now.
template <class... Tags>
[[nodiscard]] std::vector<saved_account> legacy_accounts_in(protocol_list<Tags...>, std::string_view text) {
  std::vector<saved_account> out;
  (
      [&] {
        using kept = kept_of<Tags>;
        using legacy_defaults::legacy_accounts_type;
        using legacy_defaults::legacy_entries;
        using own_list = typename decltype(legacy_accounts_type(kept{}, type_tag<kept>{}))::type;
        using shared_list = typename decltype(legacy_accounts_type(kept{}, type_tag<account_shared>{}))::type;
        const auto own = knot::try_read<own_list>(text);
        const auto shared = knot::try_read<shared_list>(text);
        if (!own || !shared || !legacy_entries(*own) || !legacy_entries(*shared))
          return;
        std::ranges::for_each(std::views::zip(*legacy_entries(*own), *legacy_entries(*shared)), [&](const auto& both) {
          const auto& [mine, every] = both;
          out.push_back(saved_account{std::string(protocol_word(mine)), kept_saved_t{mine}, every});
        });
      }(),
      ...);
  return out;
}

std::expected<file, std::string> load(const std::filesystem::path& where, mux::vault::vault& vault) {
  std::error_code failed;
  if (!std::filesystem::exists(where, failed))
    return file{};
  // Through the vault: sealed where local data is encrypted, and not read
  // with it locked or with another key.
  const auto opened = vault.read_file(where);
  if (!opened)
    return std::unexpected(std::format("cannot open {}{}", where.string(),
                                       vault.locked() ? ": local data is encrypted and locked" : ""));
  const std::string& text = *opened;
  auto read = knot::try_read<file>(text);
  if (!read)
    return std::unexpected(
        std::format("{} is not an accounts file: {} at {}", where.string(), read.error().message, read.error().offset));
  // An old file's lists, each its protocol's, read into accounts as they are
  // now, before the file's own.
  std::vector<saved_account> old = legacy_accounts_in(protocols{}, text);
  if (!old.empty()) {
    std::ranges::move(read->accounts.value_or(std::vector<saved_account>{}), std::back_inserter(old));
    read->accounts = std::move(old);
  }
  return std::move(*read);
}

// The accounts written to `where`: the directory made (its owner's alone),
// the new file made its owner's alone before the passwords go into it, and
// put in place of the old one in one rename.
std::expected<void, std::string> save(const std::filesystem::path& where, const file& accounts, mux::vault::vault& vault) {
  namespace fs = std::filesystem;
  std::error_code failed;
  if (where.has_parent_path() && !fs::exists(where.parent_path(), failed)) {
    fs::create_directories(where.parent_path(), failed);
    if (failed)
      return std::unexpected(std::format("cannot make {}: {}", where.parent_path().string(), failed.message()));
    fs::permissions(where.parent_path(), fs::perms::owner_all, fs::perm_options::replace, failed);
  }
  // Through the vault: made the owner's alone before the passwords go in,
  // sealed where local data is encrypted, put in place in one rename.
  std::string text;
  knot::write(text, accounts);
  text += '\n';
  if (!vault.write_file(where, text, true))
    return std::unexpected(std::format("cannot write {}", where.string()));
  return {};
}

}  // namespace mux::config
