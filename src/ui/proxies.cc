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

// The editor owns a local draft. Controls write fields of this model;
// requests carry the parsed profile instead of asking the app to read widgets.
struct proxy_draft {
  int index = -1;
  std::string name;
  config::proxy_kind_t kind = config::proxy_kind::socks5{};
  std::string host;
  std::string port;
  std::string username;
  std::string password;
  std::string resolver;
};
struct proxy_notice { std::string text; };
struct save_proxy_draft {};
struct delete_proxy_draft {};

inline std::expected<config::proxy_settings, std::string> proxy_profile(const proxy_draft& draft) {
  if (draft.name.empty()) return std::unexpected("Name the proxy");
  if (draft.host.empty()) return std::unexpected("Type the proxy's host");
  std::int64_t number = 0;
  const auto [last, failed] = std::from_chars(draft.port.data(), draft.port.data() + draft.port.size(), number);
  if (draft.port.empty() || failed != std::errc{} || last != draft.port.data() + draft.port.size() || number < 1 || number > 65535)
    return std::unexpected("A port is a number from 1 to 65535");
  return config::proxy_settings{.name = draft.name, .kind = draft.kind, .host = draft.host, .port = number,
      .username = typed_or_nothing(draft.username), .password = typed_or_nothing(draft.password),
      .srv_resolver = typed_or_nothing(draft.resolver)};
}
struct proxy_draft_events {
  auto on(save_proxy_draft, const proxy_draft& draft) const {
    return skiff::model::Up{request::save_proxy_profile{proxy_profile(draft), draft.index}};
  }
  auto on(delete_proxy_draft, const proxy_draft& draft) const {
    return skiff::model::Up{request::delete_proxy_profile{draft.index}};
  }
};
template <auto Member>
auto proxy_text_field(const palette& colours, std::string label, std::string placeholder, bool masked = false) {
  return skiff::compose::column(
      skiff::compose::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY, .margin = scene::Margin::horizontal(16.0f)}),
      nodes::Text(std::move(label), 13.0f, colours.dim),
      skiff::compose::bound<skiff::model::Field<Member>>(skiff::compose::styled(
          {.fillX = true, .height = 36.0f}, widgets::TextField<std::string>(colours.widgets, std::move(placeholder), masked))));
}
inline auto proxy_editor(const palette& colours, const std::optional<config::proxy_settings>& from, int index) {
  proxy_draft draft{.index = index};
  if (from) draft = {.index = index, .name = from->name, .kind = from->kind, .host = from->host,
      .port = std::to_string(from->port), .username = from->username.value_or(""),
      .password = from->password.value_or(""), .resolver = from->srv_resolver.value_or("")};
  return skiff::compose::local<proxy_draft>(proxy_draft_events{}, skiff::compose::column(
      skiff::compose::vbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      page_header<sends<request::settings_proxies>, sends<request::close_settings>>(colours,
          from ? from->name : "New proxy", {}, {}, true, true),
      proxy_text_field<&proxy_draft::name>(colours, "Name", "Home, Tor, Work…"),
      skiff::compose::bound<skiff::model::Field<&proxy_draft::kind>>(skiff::compose::styled(
          {.fillX = true, .height = 30.0f, .margin = scene::Margin::horizontal(16.0f)},
          widgets::ChoiceTabs<config::proxy_kind_t>({{"SOCKS5", config::proxy_kind::socks5{}}, {"HTTP", config::proxy_kind::http{}}}))),
      proxy_text_field<&proxy_draft::host>(colours, "Host", "proxy.example.com"),
      proxy_text_field<&proxy_draft::port>(colours, "Port", "1080"),
      proxy_text_field<&proxy_draft::username>(colours, "User name", "none"),
      proxy_text_field<&proxy_draft::password>(colours, "Password", "none", true),
      proxy_text_field<&proxy_draft::resolver>(colours, "XMPP SRV lookups: nameserver", "the system's; an IP address, or off"),
      skiff::compose::text_for<proxy_notice>([](const proxy_notice& notice) { return notice.text; },
          wrapped(skiff::compose::styled({.fillX = true, .margin = scene::Margin::horizontal(16.0f)},
                                        nodes::Text("", 13.0f, colours.error)))),
      skiff::compose::row(skiff::compose::hbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY,
          .margin = scene::Margin::horizontal(16.0f)}),
          skiff::compose::onClick(save_proxy_draft{}, skiff::compose::column(
              skiff::compose::vbox(0.0f, {.width = 110.0f, .height = 34.0f, .cornerRadius = 6.0f,
                  .background = colours.accent, .hoverBackground = colours.chosen}),
              nodes::Text("Save", 14.0f, colours.text)), "Save proxy"),
          skiff::compose::visible(from.has_value(), skiff::compose::onClick(delete_proxy_draft{}, skiff::compose::column(
              skiff::compose::vbox(0.0f, {.width = 110.0f, .height = 34.0f, .cornerRadius = 6.0f,
                  .background = colours.tile, .hoverBackground = colours.chosen}),
              nodes::Text("Delete", 14.0f, colours.text)), "Delete proxy")))), std::move(draft));
}
using proxy_editor_t = decltype(proxy_editor(std::declval<const palette&>(), std::nullopt, -1));

}  // namespace mux::ui
