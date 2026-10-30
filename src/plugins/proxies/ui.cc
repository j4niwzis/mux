// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.proxies.ui -- Proxies, as Gajim's Manage Proxies: Settings'
// link to them; their page, the profiles and a way to add one; a profile's
// editor; and an account's Proxy page, which of them it connects through.
module;
#include "../../inline_in_release.h"
#include "skiff/named.h"
export module mux.plugin.proxies.ui;

import std;
import skia;
import skiff.scene;
import skiff.compose;
import skiff.model;
import skiff.nodes.text;
import mux.composition;
import mux.config;
import mux.ui.base;
import mux.plugin.proxies.types;

export namespace mux::plugins::proxies {
using namespace ::mux::ui;

// Settings' link to them (mux.ui:settings_pages).
MUX_INLINE_IN_RELEASE auto extend(contract::settings_links, type_tag<feature>, const palette& colours) {
  return settings_link(colours, "Proxies", icon::gear{}, open_list{});
}

// The profiles, and a way to add one.
MUX_INLINE_IN_RELEASE auto list_view(const palette& colours, const std::vector<config::proxy_settings>& profiles, bool with_back) {
  auto rows = std::views::iota(std::size_t{0}, profiles.size()) | std::views::transform([&](std::size_t index) {
        const auto& profile = profiles[index];
        return settings_link(colours, std::format("{} ({} {}:{})", profile.name, config::label_of(profile.kind), profile.host, profile.port),
                             icon::dot{proxy_colour(profile.name)}, edit{static_cast<int>(index)});
      }) | std::ranges::to<std::vector>();
  return SKIFF_NAMED(list_view, skiff::compose::column(
      skiff::compose::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      skiff::compose::styled({.depth = 1.0f, .background = colours.sidebar},
          page_header<sends<request::settings_home>, sends<request::close_settings>>(colours, "Proxies", {}, {}, with_back, true)),
      skiff::compose::many(skiff::compose::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), std::move(rows)),
      settings_link(colours, "Add proxy", icon::plus{}, add{}),
      skiff::compose::visible(profiles.empty(), wrapped(skiff::compose::styled({.fillX = true, .margin = {8.0f, 20.0f, 0.0f, 20.0f}},
          note_text(colours, "No proxies yet. Accounts connect directly."))))));
}
MUX_INLINE_IN_RELEASE auto extend(contract::settings_pages, const ui_needs& n, const list_page& page) {
  return list_view(*n.colours, page.all, page.with_back);
}

// The editor owns a local draft. Controls write fields of this model;
// requests carry the parsed profile instead of asking the app to read widgets.
struct draft {
  int index = -1;
  std::string name;
  config::proxy_kind_t kind = config::proxy_kind::socks5{};
  std::string host;
  std::string port;
  std::string username;
  std::string password;
  std::string resolver;
};
struct save_draft {};
struct delete_draft {};

inline std::expected<config::proxy_settings, std::string> profile_of(const draft& now) {
  if (now.name.empty()) return std::unexpected("Name the proxy");
  if (now.host.empty()) return std::unexpected("Type the proxy's host");
  std::int64_t number = 0;
  const auto [last, failed] = std::from_chars(now.port.data(), now.port.data() + now.port.size(), number);
  if (now.port.empty() || failed != std::errc{} || last != now.port.data() + now.port.size() || number < 1 || number > 65535)
    return std::unexpected("A port is a number from 1 to 65535");
  return config::proxy_settings{.name = now.name, .kind = now.kind, .host = now.host, .port = number,
      .username = typed_or_nothing(now.username), .password = typed_or_nothing(now.password),
      .srv_resolver = typed_or_nothing(now.resolver)};
}
struct draft_events {
  auto on(save_draft, const draft& now) const { return skiff::model::Up{save{profile_of(now), now.index}}; }
  auto on(delete_draft, const draft& now) const { return skiff::model::Up{remove{now.index}}; }
};
MUX_INLINE_IN_RELEASE auto editor_view(const palette& colours, const std::optional<config::proxy_settings>& from, int index) {
  draft made{.index = index};
  if (from) made = {.index = index, .name = from->name, .kind = from->kind, .host = from->host,
      .port = std::to_string(from->port), .username = from->username.value_or(""),
      .password = from->password.value_or(""), .resolver = from->srv_resolver.value_or("")};
  return SKIFF_NAMED(editor_view, skiff::compose::local<draft>(draft_events{}, skiff::compose::column(
      skiff::compose::vbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      page_header<sends<open_list>, sends<request::close_settings>>(colours, from ? from->name : "New proxy", {}, {}, true, true),
      model_field<&draft::name>(colours, "Name", "Home, Tor, Work…", false, scene::Margin::horizontal(16.0f)),
      skiff::compose::bound<skiff::model::Field<&draft::kind>>(skiff::compose::styled(
          {.fillX = true, .height = 30.0f, .margin = scene::Margin::horizontal(16.0f)},
          themed_tabs<config::proxy_kind_t>(colours, {{"SOCKS5", config::proxy_kind::socks5{}}, {"HTTP", config::proxy_kind::http{}}}))),
      model_field<&draft::host>(colours, "Host", "proxy.example.com", false, scene::Margin::horizontal(16.0f)),
      model_field<&draft::port>(colours, "Port", "1080", false, scene::Margin::horizontal(16.0f)),
      model_field<&draft::username>(colours, "User name", "none", false, scene::Margin::horizontal(16.0f)),
      model_field<&draft::password>(colours, "Password", "none", true, scene::Margin::horizontal(16.0f)),
      model_field<&draft::resolver>(colours, "XMPP SRV lookups: nameserver", "the system's; an IP address, or off", false, scene::Margin::horizontal(16.0f)),
      skiff::compose::text_for<shown>([](const shown& now) { return now.error; },
          wrapped(skiff::compose::styled({.fillX = true, .margin = scene::Margin::horizontal(16.0f)},
                                        nodes::Text("", 13.0f, colours.error)))),
      skiff::compose::row(skiff::compose::hbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY,
          .margin = scene::Margin::horizontal(16.0f)}),
          // The program's buttons, their labels in their middle.
          skiff::compose::styled({.width = 110.0f, .height = 34.0f},
              skiff::compose::primary(button_for<sends<save_draft>>(colours.widgets, "Save", {}))),
          skiff::compose::visible(from.has_value(), skiff::compose::styled({.width = 110.0f, .height = 34.0f},
              button_for<sends<delete_draft>>(colours.widgets, "Delete", {}))))), std::move(made)));
}
MUX_INLINE_IN_RELEASE auto extend(contract::settings_pages, const ui_needs& n, const editor_page& page) {
  return editor_view(*n.colours, page.from, page.index);
}

// An account's Proxy page, as Gajim's: which of the profiles it connects
// through -- none, or one -- and the way to Manage proxies.
constexpr std::string_view page_title(account_page) { return "Proxy"; }
inline icon_t page_icon(account_page) { return icon::gear{}; }
struct rows_key {
  std::vector<std::string> names;
  std::vector<std::string> labels;
  std::string current;
  friend bool operator==(const rows_key&, const rows_key&) = default;
};
MUX_INLINE_IN_RELEASE auto choice_rows(const palette& colours, const std::string& account, const rows_key& key) {
  namespace c = skiff::compose;
  // No proxy first -- an empty place where the dots are, so the names line up.
  auto rows = std::views::iota(-1, static_cast<int>(key.names.size())) | std::views::transform([&](int at) {
    if (at < 0)
      return row_item(colours, "No proxy", sends<choose>{choose{account, -1}}, icon::dot{skia::colorSetARGB(0, 0, 0, 0)}, key.current.empty());
    const std::string& name = key.names[static_cast<std::size_t>(at)];
    return row_item(colours, key.labels[static_cast<std::size_t>(at)], sends<choose>{choose{account, at}}, icon::dot{proxy_colour(name)},
                    key.current == name);
  }) | std::ranges::to<std::vector>();
  return SKIFF_NAMED(choice_rows, c::many(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), std::move(rows)));
}
using choice_rows_t = decltype(choice_rows(std::declval<const palette&>(), std::string(), rows_key{}));
MUX_INLINE_IN_RELEASE auto page_view(account_page, const palette& colours, const account_page_facts& facts) {
  namespace c = skiff::compose;
  const std::string address = facts.by.address;
  return SKIFF_NAMED(page_view, c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      c::styled({.margin = {0.0f, 0.0f, 4.0f, 0.0f}}, section_title(colours, "PROXY")),
      c::projected<shown>([address](const shown& now) {
        const auto found = now.through.find(address);
        return rows_key{now.names, now.labels, found == now.through.end() ? std::string() : found->second};
      }, c::mount<choice_rows_t, rows_key>([at = &colours, address](const rows_key& key) { return std::tuple{choice_rows(*at, address, key)}; },
                                           {.fillX = true, .autoSize = scene::axes::kY})),
      c::styled({.margin = {8.0f, 0.0f, 0.0f, 0.0f}}, row_item(colours, "Manage proxies…", sends<manage>{}, icon::gear{}))));
}

}  // namespace mux::plugins::proxies
