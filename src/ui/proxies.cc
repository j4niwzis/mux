// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:proxies -- Settings: the home page, animations, proxies.
export module mux.ui:proxies;

import std;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes.box;
import skiff.nodes.flow;
import skiff.nodes.icon;
import skiff.nodes.text;
import skiff.widgets.button;
import skiff.model;
import skiff.compose;
import skiff.widgets.model;
import skiff.bind;
import mux.core;
import mux.config;
import :base;
import :icons;
import :avatars;
import :controls;
import :forms;
import :drawer;

export namespace mux::ui {

// ---- the settings -------------------------------------------------------------------

// Settings, as Telegram Desktop shows them: a box over the window, a list of
// sections, and each section a page of the same box.
template <class Event>
auto settings_link(const palette& colours, std::string label, icon_t icon, Event event) {
  return skiff::compose::onClick(std::move(event), skiff::compose::row(
      skiff::compose::hbox(16.0f, {.fillX = true, .height = 46.0f, .padding = {0.0f, 20.0f, 0.0f, 20.0f},
                                  .hoverBackground = colours.chosen, .focusBackground = colours.chosen}),
      skiff::compose::styled({.width = 28.0f, .height = 36.0f, .alignSelf = scene::align::kMiddle}, nodes::Icon(shape_of(icon), colours.dim)),
      skiff::compose::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle}, elided(nodes::Text(label, 15.0f, colours.text)))), label);
}
inline auto settings_home(const palette& colours) {
  return skiff::compose::column(
      skiff::compose::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      skiff::compose::styled({.depth = 1.0f, .background = colours.sidebar},
          page_header<sends<request::close_settings>, sends<request::close_settings>>(colours, "Settings", {}, {}, false, true)),
      settings_link(colours, "Accounts", icon::person{}, request::open_accounts{}),
      settings_link(colours, "Animations", icon::motion{}, request::settings_animations{}),
      settings_link(colours, "Appearance", icon::eye{}, request::settings_appearance{}),
      settings_link(colours, "Emojis & Stickers", icon::smile{}, request::open_packs{}),
      settings_link(colours, "Rendering", icon::sliders{}, request::settings_rendering{}),
      settings_link(colours, "Notifications", icon::bell{}, request::settings_notifications{}),
      settings_link(colours, "Storage", icon::clip{}, request::settings_storage{}),
      settings_link(colours, "Files", icon::send{}, request::settings_files{}),
      settings_link(colours, "Proxies", icon::gear{}, request::settings_proxies{}));
}
using settings_home_t = decltype(settings_home(std::declval<const palette&>()));

inline auto motion_settings_view(const palette& colours) {
  using field = skiff::model::Field<&config::look_settings::motion>;
  return skiff::compose::column(
      skiff::compose::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      wrapped(skiff::compose::styled({.fillX = true, .margin = {4.0f, 20.0f, 12.0f, 20.0f}},
          note_text(colours, "How much the window moves. Reduced keeps the small movements, such as a section unfolding, and shows panels at once."))),
      skiff::compose::bound<field>(widgets::ChoiceRowField<config::motion_t>(colours.widgets, "Full", config::motion::full{})),
      skiff::compose::bound<field>(widgets::ChoiceRowField<config::motion_t>(colours.widgets, "Reduced", config::motion::reduced{})),
      skiff::compose::bound<field>(widgets::ChoiceRowField<config::motion_t>(colours.widgets, "None", config::motion::none{})));
}
inline auto animations_page(const palette& colours) {
  return skiff::compose::column(
      skiff::compose::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      skiff::compose::styled({.depth = 1.0f, .background = colours.sidebar},
          page_header<sends<request::settings_home>, sends<request::close_settings>>(colours, "Animations", {}, {}, true, true)),
      motion_settings_view(colours));
}
using animations_page_t = decltype(animations_page(std::declval<const palette&>()));

// A proxy profile opened from the list in Settings.
template <class Actions>
struct edit_proxy {
  using Answer = ::mux::ui::request::edit_proxy;
  int index = 0;
  ::mux::ui::request::edit_proxy operator()() { return ::mux::ui::request::edit_proxy{index}; }
};
// A kind of proxy chosen on a profile's page.
template <class Actions>
struct choose_proxy_kind {
  using Answer = ::mux::ui::request::proxy_kind;
  config::proxy_kind_t kind;
  ::mux::ui::request::proxy_kind operator()() { return ::mux::ui::request::proxy_kind{kind}; }
};

// Settings' Proxies page, as Gajim's Manage Proxies: the profiles, and a way
// to add one.
inline auto proxies_page(const palette& colours, const std::vector<config::proxy_settings>& profiles, bool with_back) {
  auto rows = std::views::iota(std::size_t{0}, profiles.size()) |
      std::views::transform([&](std::size_t index) {
        const auto& profile = profiles[index];
        return settings_link(colours, std::format("{} ({} {}:{})", profile.name, config::label_of(profile.kind), profile.host, profile.port),
                             icon::dot{proxy_colour(profile.name)}, request::edit_proxy{static_cast<int>(index)});
      }) | std::ranges::to<std::vector>();
  return skiff::compose::column(
      skiff::compose::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      skiff::compose::styled({.depth = 1.0f, .background = colours.sidebar},
          page_header<sends<request::settings_home>, sends<request::close_settings>>(colours, "Proxies", {}, {}, with_back, true)),
      skiff::compose::many(skiff::compose::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), std::move(rows)),
      settings_link(colours, "Add proxy", icon::plus{}, request::add_proxy{}),
      skiff::compose::visible(profiles.empty(), wrapped(skiff::compose::styled({.fillX = true, .margin = {8.0f, 20.0f, 0.0f, 20.0f}},
          note_text(colours, "No proxies yet. Accounts connect directly.")))));
}
using proxies_page_t = decltype(proxies_page(std::declval<const palette&>(), std::declval<const std::vector<config::proxy_settings>&>(), true));

// SOCKS5 | HTTP: two segments in a frame, the chosen one lit by a plate
// that slides from one to the other.
template <class Actions> struct kind_switch : skiff::compose::Stacked {
  // The highlight that slides from one to the other: under them, out of
  // their flow, shifted as far as the slide has come.
  using kind_segment = segment<choose_proxy_kind<Actions>>;
  struct parts_t {
    nodes::Box<> highlight;
    kind_segment socks;
    kind_segment http;
  } parts;
  skiff::paint::Tween slide{0.0f, 180.0f, skiff::paint::movement::subtle{}};

  kind_switch(const palette &colours)
      : Stacked(skiff::compose::hbox(1.0f, {.autoSize = scene::axes::kBoth,
                                            .padding = {1.0f, 1.0f, 1.0f, 1.0f},
                                            .background = colours.chosen})),
        parts{.highlight =
                  skiff::compose::styled({.place = scene::anchor::kTopLeft,
                                          .width = 92.0f,
                                          .height = 28.0f},
                                         nodes::Box<>(colours.accent)),
              .socks = kind_segment(colours, "SOCKS5",
                                    {config::proxy_kind::socks5{}}),
              .http =
                  kind_segment(colours, "HTTP", {config::proxy_kind::http{}})} {
  }
  void show(const config::proxy_kind_t& kind, bool at_once) {
    const float to = spl::visit(spl::overloaded{[](config::proxy_kind::socks5) { return 0.0f; },
                                           [](config::proxy_kind::http) { return 1.0f; }},
                                kind);
    if (at_once)
      slide.jump(to);
    else
      slide.setTarget(to);
    this->markDamaged();
  }
  [[nodiscard]] bool settling() const { return slide.moving(); }
  void update(double now_ms) {
    auto& [highlight, socks, http] = parts;
    if (slide.step(now_ms))
      highlight.apply({.shiftX = (http.bounds().fLeft - socks.bounds().fLeft) * slide.value()});
  }
};

// One proxy profile's page: its name, SOCKS5 or HTTP, where, and who to be
// there; saved or deleted with its buttons. Declared: a column of these,
// nothing placed by hand.
template <class Actions> struct proxy_editor : skiff::compose::Stacked {
  int index = -1;  // in the list; -1 for a new one
  // The colours what it says is said in.
  const palette* colours_ = nullptr;
  config::proxy_kind_t kind = config::proxy_kind::socks5{};
  using header_t = page_header_t<sends<::mux::ui::request::settings_proxies>, sends<::mux::ui::request::close_settings>>;
  using save_button = button_for<sends<::mux::ui::request::save_proxy_profile>>;
  using delete_button = button_for<sends<::mux::ui::request::delete_proxy_profile>>;
  struct parts_t {
    header_t header;
    field name;
    kind_switch<Actions> kinds;
    field host;
    field port;
    field username;
    field password;
    // XMPP's SRV records, through it: asked of whom.
    field resolver;
    nodes::Text message;
    button_row<save_button, delete_button> buttons;
  } parts;

  proxy_editor(const ui_needs<Actions>& n, const std::optional<config::proxy_settings>& from, int at)
      : proxy_editor(*n.colours, from, at) {}
  proxy_editor(const palette &colours,
               const std::optional<config::proxy_settings> &from, int at)
      : Stacked(skiff::compose::vbox(8.0f, {.fill = true})), index(at),
        colours_(&colours),
        parts{.header = page_header<sends<::mux::ui::request::settings_proxies>, sends<::mux::ui::request::close_settings>>(colours,
                                 from ? from->name : std::string("New proxy"),
                                 {}, {}, true, true),
              .name = field(colours, "Name", "Home, Tor, Work…"),
              .kinds =
                  skiff::compose::styled({.margin = {4.0f, 0.0f, 4.0f, 16.0f}},
                                         kind_switch<Actions>(colours)),
              .host = field(colours, "Host", "proxy.example.com"),
              .port = field(colours, "Port", "1080"),
              .username = field(colours, "User name", "none"),
              .password = field(colours, "Password", "none"),
              .resolver = field(colours, "XMPP SRV lookups: nameserver",
                                "the system's; an IP address, or off"),
              .message = wrapped(nodes::Text("", 13.0f, colours.dim)),
              .buttons = button_row<save_button, delete_button>(
                  save_button(colours.widgets, "Save", {}),
                  delete_button(colours.widgets, "Delete", {}))} {
    auto& [header, name, kinds, host, port, username, password, resolver, message, buttons] = parts;
    const auto inset = scene::Margin::horizontal(16.0f);
    for (field* one : {&name, &host, &port, &username, &password, &resolver})
      one->apply({.margin = inset});

    message.apply({.fillX = true, .margin = inset});
    buttons.apply({.margin = inset});
    auto& [save, remove] = buttons.parts.buttons;
    save.setPrimary(true);
    save.apply({.width = 110.0f, .height = 34.0f});
    remove.apply({.width = 110.0f, .height = 34.0f});
    remove.setVisible(from.has_value());
    password.parts.box.setMasked(true);
    if (from) {
      name.parts.box.setText(from->name);
      host.parts.box.setText(from->host);
      port.parts.box.setText(std::to_string(from->port));
      username.parts.box.setText(from->username.value_or(""));
      password.parts.box.setText(from->password.value_or(""));
      resolver.parts.box.setText(from->srv_resolver.value_or(""));
    }
    kind = from ? from->kind : config::proxy_kind_t{config::proxy_kind::socks5{}};
    kinds.show(kind, true);
  }

  void set_kind(const config::proxy_kind_t& to) {
    kind = to;
    parts.kinds.show(kind, false);
  }

  // The profile as typed, or what is wrong with it.
  [[nodiscard]] std::expected<config::proxy_settings, std::string> proxy() const {
    const auto& [header, name, kinds, host, port, username, password, resolver, message, buttons] = parts;
    config::proxy_settings out{.name = name.parts.box.text(), .kind = kind, .host = host.parts.box.text()};
    if (out.name.empty())
      return std::unexpected("Name the proxy");
    if (out.host.empty())
      return std::unexpected("Type the proxy's host");
    const std::string& text = port.parts.box.text();
    std::int64_t number = 0;
    const auto [last, failed] = std::from_chars(text.data(), text.data() + text.size(), number);
    if (text.empty() || failed != std::errc{} || last != text.data() + text.size() || number < 1 || number > 65535)
      return std::unexpected("A port is a number from 1 to 65535");
    out.port = number;
    out.username = typed_or_nothing(username.parts.box.text());
    out.password = typed_or_nothing(password.parts.box.text());
    out.srv_resolver = typed_or_nothing(resolver.parts.box.text());
    return out;
  }

  void say(std::string text, bool error) {
    parts.message.setText(std::move(text));
    parts.message.setColour(error ? colours_->error : colours_->dim);
  }
  void show_receipts(bool) {}
};

}  // namespace mux::ui
