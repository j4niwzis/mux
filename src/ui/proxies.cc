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
import skiff.nodes.text;
import skiff.widgets.button;
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
template <class Actions>
struct settings_home : nodes::Stack {
  // Its children, in the order they are shown: the header, then the lines,
  // one under another -- walked as they are declared.
  struct parts_t {
    page_header<ask<Actions, &Actions::close_settings>, ask<Actions, &Actions::close_settings>> header;
    row_item<ask<Actions, &Actions::open_accounts>> accounts;
    row_item<ask<Actions, &Actions::settings_animations>> animations;
    row_item<ask<Actions, &Actions::settings_appearance>> appearance;
    row_item<ask<Actions, &Actions::open_packs>> packs;
    row_item<ask<Actions, &Actions::settings_rendering>> rendering;
    row_item<ask<Actions, &Actions::settings_notifications>> notifications;
    row_item<ask<Actions, &Actions::settings_storage>> storage;
    row_item<ask<Actions, &Actions::settings_files>> files;
    row_item<ask<Actions, &Actions::settings_proxies>> proxies;
  } parts;

  explicit settings_home(Actions* a)
      : parts{.header = {"Settings", {a}, {a}, false, true},
              .accounts = {"Accounts", {a}, icon::person{}},
              .animations = {"Animations", {a}, icon::motion{}},
              .appearance = {"Appearance", {a}, icon::eye{}},
              .packs = {"Emojis & Stickers", {a}, icon::smile{}},
              .rendering = {"Rendering", {a}, icon::sliders{}},
              .notifications = {"Notifications", {a}, icon::bell{}},
              .storage = {"Storage", {a}, icon::clip{}},
              .files = {"Files", {a}, icon::send{}},
              .proxies = {"Proxies", {a}, icon::gear{}}} {
    fState.apply({.fill = true});
  }

  void show_motion(std::string_view) {}
  void show_receipts(bool) {}
};



template <class Actions>
struct animations_page : nodes::Stack {
  using header_t = page_header<ask<Actions, &Actions::settings_home>, ask<Actions, &Actions::close_settings>>;
  using choice = row_item<choose_motion<Actions>>;
  struct parts_t {
    header_t header;
    nodes::Text note{"How much the window moves. Reduced keeps the small movements, such as a section unfolding, "
                     "and shows panels at once.",
                     13.0f, dim_colour};
    choice full;
    choice reduced;
    choice none;
  } parts;

  explicit animations_page(Actions* a)
      : parts{.header = header_t("Animations", {a}, {a}, true, true),
              .full = choice("Full", {a, kMotions[0]}, icon::none{}, false),
              .reduced = choice("Reduced", {a, kMotions[1]}, icon::none{}, false),
              .none = choice("None", {a, kMotions[2]}, icon::none{}, false)} {
    parts.note.apply({.fillX = true, .margin = {4.0f, 20.0f, 12.0f, 20.0f}});
    fState.apply({.fill = true});
    parts.note.setWrapped(true);
  }

  void show_receipts(bool) {}
  void show_motion(std::string_view level) {
    auto& [header, note, full, reduced, none] = parts;
    full.set_chosen(level == kMotions[0]);
    reduced.set_chosen(level == kMotions[1]);
    none.set_chosen(level == kMotions[2]);
  }
};

// A proxy profile opened from the list in Settings.
template <class Actions>
struct edit_proxy {
  Actions* actions = nullptr;
  int index = 0;
  void operator()() const { actions->edit_proxy(index); }
};
// A kind of proxy chosen on a profile's page.
template <class Actions>
struct choose_proxy_kind {
  Actions* actions = nullptr;
  config::proxy_kind_t kind;
  void operator()() const { actions->proxy_kind(kind); }
};

// Settings' Proxies page, as Gajim's Manage Proxies: the profiles, and a way
// to add one.
template <class Actions>
struct proxies_page : nodes::Stack {
  using header_t = page_header<ask<Actions, &Actions::settings_home>, ask<Actions, &Actions::close_settings>>;
  using add_row = row_item<ask<Actions, &Actions::add_proxy>>;
  struct parts_t {
    header_t header;
    std::vector<row_item<edit_proxy<Actions>>> profiles;
    add_row add;
    nodes::Text empty{"No proxies yet. Accounts connect directly.", 13.0f, dim_colour};
  } parts;

  // With a way back to the settings' list where it was opened from there.
  proxies_page(Actions* a, const std::vector<config::proxy_settings>& all, bool with_back)
      : parts{.header = header_t("Proxies", {a}, {a}, with_back, true), .add = add_row("Add proxy", {a}, icon::plus{})} {
    auto& [header, profiles, add, empty] = parts;
    empty.setWrapped(true);
    empty.apply({.fillX = true, .margin = {8.0f, 20.0f, 0.0f, 20.0f}});
    fState.apply({.fill = true});
    for (std::size_t i = 0; i < all.size(); ++i)
      profiles.emplace_back(std::format("{} ({} {}:{})", all[i].name, config::label_of(config::proxy_kind_of(all[i].kind)),
                                        all[i].host, all[i].port),
                            edit_proxy<Actions>{a, static_cast<int>(i)}, icon::dot{proxy_colour(all[i].name)});
    empty.setVisible(all.empty());
  }
  void show_motion(std::string_view) {}
  void show_receipts(bool) {}
};

// SOCKS5 | HTTP: two segments in a frame, the chosen one lit by a plate
// that slides from one to the other.
template <class Actions>
struct kind_switch : nodes::Stack {
  // The highlight that slides from one to the other: under them, out of
  // their flow, shifted as far as the slide has come.
  using kind_segment = segment<choose_proxy_kind<Actions>>;
  struct parts_t {
    nodes::Box<> highlight{accent_colour};
    kind_segment socks;
    kind_segment http;
  } parts;
  skiff::paint::Tween slide{0.0f, 180.0f, skiff::paint::movement::subtle{}};

  explicit kind_switch(Actions* a)
      : parts{.socks = kind_segment("SOCKS5", {a, config::proxy_kind::socks5{}}),
              .http = kind_segment("HTTP", {a, config::proxy_kind::http{}})} {
    this->setHorizontal();
    this->setGap(1.0f);
    fState.apply({.autoSize = scene::axes::kBoth, .padding = {1.0f, 1.0f, 1.0f, 1.0f}, .background = chosen_colour});
    parts.highlight.apply({.place = scene::anchor::kTopLeft, .width = 92.0f, .height = 28.0f});
  }
  void show(const config::proxy_kind_t& kind, bool at_once) {
    const float to = splice::visit(splice::overloaded{[](config::proxy_kind::socks5) { return 0.0f; },
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
template <class Actions>
struct proxy_editor : nodes::Stack {
  int index = -1;  // in the list; -1 for a new one
  config::proxy_kind_t kind = config::proxy_kind::socks5{};
  using header_t = page_header<ask<Actions, &Actions::settings_proxies>, ask<Actions, &Actions::close_settings>>;
  using save_button = widgets::Button<ask<Actions, &Actions::save_proxy_profile>>;
  using delete_button = widgets::Button<ask<Actions, &Actions::delete_proxy_profile>>;
  struct parts_t {
    header_t header;
    field name{"Name", "Home, Tor, Work…"};
    kind_switch<Actions> kinds;
    field host{"Host", "proxy.example.com"};
    field port{"Port", "1080"};
    field username{"User name", "none"};
    field password{"Password", "none"};
    // XMPP's SRV records, through it: asked of whom.
    field resolver{"XMPP SRV lookups: nameserver", "the system's; an IP address, or off"};
    nodes::Text message{"", 13.0f, dim_colour};
    button_row<save_button, delete_button> buttons;
  } parts;

  proxy_editor(Actions* a, const std::optional<config::proxy_settings>& from, int at)
      : index(at),
        parts{.header = header_t(from ? from->name : std::string("New proxy"), {a}, {a}, true, true),
              .kinds = kind_switch<Actions>(a),
              .buttons = button_row<save_button, delete_button>(save_button("Save", {a}), delete_button("Delete", {a}))} {
    auto& [header, name, kinds, host, port, username, password, resolver, message, buttons] = parts;
    fState.apply({.fill = true});
    this->setGap(8.0f);
    const auto inset = scene::Margin::horizontal(16.0f);
    for (field* one : {&name, &host, &port, &username, &password, &resolver})
      one->apply({.margin = inset});
    kinds.apply({.margin = {4.0f, 0.0f, 4.0f, 16.0f}});
    message.setWrapped(true);
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
    kind = from ? config::proxy_kind_of(from->kind) : config::proxy_kind_t{config::proxy_kind::socks5{}};
    kinds.show(kind, true);
  }

  void set_kind(const config::proxy_kind_t& to) {
    kind = to;
    parts.kinds.show(kind, false);
  }

  // The profile as typed, or what is wrong with it.
  [[nodiscard]] std::expected<config::proxy_settings, std::string> proxy() const {
    const auto& [header, name, kinds, host, port, username, password, resolver, message, buttons] = parts;
    config::proxy_settings out{.name = name.parts.box.text(), .kind = config::word_of(kind), .host = host.parts.box.text()};
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
    parts.message.setColour(error ? error_colour : dim_colour);
  }
  void show_motion(std::string_view) {}
  void show_receipts(bool) {}
};

}  // namespace mux::ui
