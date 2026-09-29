// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:proxies -- Settings: the home page, animations, proxies.
export module mux.ui:proxies;

import std;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes;
import skiff.widgets;
import mux.core;
import mux.config;
export import :drawer;

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
    row_item<ask<Actions, &Actions::settings_rendering>> rendering;
    row_item<ask<Actions, &Actions::settings_storage>> storage;
    row_item<ask<Actions, &Actions::settings_files>> files;
    row_item<ask<Actions, &Actions::settings_proxies>> proxies;
  } parts;

  explicit settings_home(Actions* a)
      : parts{.header = {"Settings", {a}, {a}, false, true},
              .accounts = {"Accounts", {a}, icon::person{}},
              .animations = {"Animations", {a}, icon::motion{}},
              .appearance = {"Appearance", {a}, icon::eye{}},
              .rendering = {"Rendering", {a}, icon::sliders{}},
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
  page_header<ask<Actions, &Actions::settings_home>, ask<Actions, &Actions::close_settings>> header;
  nodes::Text note{"How much the window moves. Reduced keeps the small movements, such as a section unfolding, "
                   "and shows panels at once.",
                   13.0f, dim_colour};
  row_item<choose_motion<Actions>> full;
  row_item<choose_motion<Actions>> reduced;
  row_item<choose_motion<Actions>> none;

  explicit animations_page(Actions* a)
      : header("Animations", {a}, {a}, true, true),
        full("Full", {a, kMotions[0]}, icon::none{}, false),
        reduced("Reduced", {a, kMotions[1]}, icon::none{}, false),
        none("None", {a, kMotions[2]}, icon::none{}, false) {
    note.apply({.fillX = true, .margin = {4.0f, 20.0f, 12.0f, 20.0f}});
    fState.apply({.fill = true});
    note.setWrapped(true);
  }

  void forEachChild(auto&& f) {
    f(header);
    f(note);
    f(full);
    f(reduced);
    f(none);
  }
  void show_receipts(bool) {}
  void show_motion(std::string_view level) {
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
  page_header<ask<Actions, &Actions::settings_home>, ask<Actions, &Actions::close_settings>> header;
  std::vector<row_item<edit_proxy<Actions>>> profiles;
  row_item<ask<Actions, &Actions::add_proxy>> add;
  nodes::Text empty{"No proxies yet. Accounts connect directly.", 13.0f, dim_colour};

  // With a way back to the settings' list where it was opened from there.
  proxies_page(Actions* a, const std::vector<config::proxy_settings>& all, bool with_back)
      : header("Proxies", {a}, {a}, with_back, true), add("Add proxy", {a}, icon::plus{}) {
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
  void forEachChild(auto&& f) {
    f(header);
    f(profiles);
    f(add);
    f(empty);
  }
};

// SOCKS5 | HTTP: two segments in a frame, the chosen one lit by a plate
// that slides from one to the other.
template <class Actions>
struct kind_switch : nodes::Stack {
  // The highlight that slides from one to the other: under them, out of
  // their flow, shifted as far as the slide has come.
  nodes::Box<> highlight{accent_colour};
  segment<choose_proxy_kind<Actions>> socks;
  segment<choose_proxy_kind<Actions>> http;
  skiff::paint::Tween slide{0.0f, 180.0f, skiff::paint::movement::subtle{}};

  explicit kind_switch(Actions* a)
      : socks("SOCKS5", {a, config::proxy_kind::socks5{}}), http("HTTP", {a, config::proxy_kind::http{}}) {
    this->setHorizontal();
    this->setGap(1.0f);
    fState.apply({.autoSize = scene::axes::kBoth, .padding = {1.0f, 1.0f, 1.0f, 1.0f}, .background = chosen_colour});
    highlight.apply({.place = scene::anchor::kTopLeft, .width = 92.0f, .height = 28.0f});
  }
  void show(const config::proxy_kind_t& kind, bool at_once) {
    const float to = std::visit(overloaded{[](config::proxy_kind::socks5) { return 0.0f; },
                                           [](config::proxy_kind::http) { return 1.0f; }},
                                kind);
    if (at_once)
      slide.jump(to);
    else
      slide.setTarget(to);
    this->markDamaged();
  }
  void forEachChild(auto&& f) {
    f(highlight);
    f(socks);
    f(http);
  }
  [[nodiscard]] bool settling() const { return slide.moving(); }
  void update(double now_ms) {
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
  page_header<ask<Actions, &Actions::settings_proxies>, ask<Actions, &Actions::close_settings>> header;
  field name{"Name", "Home, Tor, Work…"};
  kind_switch<Actions> kinds;
  field host{"Host", "proxy.example.com"};
  field port{"Port", "1080"};
  field username{"User name", "none"};
  field password{"Password", "none"};
  nodes::Text message{"", 13.0f, dim_colour};
  button_row<widgets::Button<ask<Actions, &Actions::save_proxy_profile>>,
             widgets::Button<ask<Actions, &Actions::delete_proxy_profile>>>
      buttons;

  proxy_editor(Actions* a, const std::optional<config::proxy_settings>& from, int at)
      : index(at), header(from ? from->name : std::string("New proxy"), {a}, {a}, true, true), kinds(a),
        buttons(widgets::Button<ask<Actions, &Actions::save_proxy_profile>>("Save", {a}),
                widgets::Button<ask<Actions, &Actions::delete_proxy_profile>>("Delete", {a})) {
    fState.apply({.fill = true});
    this->setGap(8.0f);
    const auto inset = scene::Margin::horizontal(16.0f);
    for (field* one : {&name, &host, &port, &username, &password})
      one->apply({.margin = inset});
    kinds.apply({.margin = {4.0f, 0.0f, 4.0f, 16.0f}});
    message.setWrapped(true);
    message.apply({.fillX = true, .margin = inset});
    buttons.apply({.margin = inset});
    auto& [save, remove] = buttons.buttons;
    save.setPrimary(true);
    save.apply({.width = 110.0f, .height = 34.0f});
    remove.apply({.width = 110.0f, .height = 34.0f});
    remove.setVisible(from.has_value());
    password.box.setMasked(true);
    if (from) {
      name.box.setText(from->name);
      host.box.setText(from->host);
      port.box.setText(std::to_string(from->port));
      username.box.setText(from->username.value_or(""));
      password.box.setText(from->password.value_or(""));
    }
    kind = from ? config::proxy_kind_of(from->kind) : config::proxy_kind_t{config::proxy_kind::socks5{}};
    kinds.show(kind, true);
  }

  void set_kind(const config::proxy_kind_t& to) {
    kind = to;
    kinds.show(kind, false);
  }

  // The profile as typed, or what is wrong with it.
  [[nodiscard]] std::expected<config::proxy_settings, std::string> proxy() const {
    config::proxy_settings out{.name = name.box.text(), .kind = config::word_of(kind), .host = host.box.text()};
    if (out.name.empty())
      return std::unexpected("Name the proxy");
    if (out.host.empty())
      return std::unexpected("Type the proxy's host");
    const std::string& text = port.box.text();
    std::int64_t number = 0;
    const auto [last, failed] = std::from_chars(text.data(), text.data() + text.size(), number);
    if (text.empty() || failed != std::errc{} || last != text.data() + text.size() || number < 1 || number > 65535)
      return std::unexpected("A port is a number from 1 to 65535");
    out.port = number;
    out.username = typed_or_nothing(username.box.text());
    out.password = typed_or_nothing(password.box.text());
    return out;
  }

  void say(std::string text, bool error) {
    message.setText(std::move(text));
    message.setColour(error ? error_colour : dim_colour);
  }
  void show_motion(std::string_view) {}
  void show_receipts(bool) {}

  void forEachChild(auto&& f) {
    f(header);
    f(name);
    f(kinds);
    f(host);
    f(port);
    f(username);
    f(password);
    f(message);
    f(buttons);
  }
};

}  // namespace mux::ui
