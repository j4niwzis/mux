// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:add_account -- Adding an account.
export module mux.ui:add_account;

import std;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.compose;
import skiff.nodes.flow;
import skiff.nodes.text;
import mux.core;
import mux.config;
import mux.protocols;
import mux.ui.base;
import :account_forms;

export namespace mux::ui {

// ---- adding an account ------------------------------------------------------------
// What the pane shows, as the program says: the protocol whose form is up,
// the proxy profiles there are now (one made or dropped in the settings
// meanwhile is there), and the one the new account is to go through, where
// one is chosen. The program keeps it: a press asks it (add_account_of,
// choose_new_proxy) and it says the new state.
struct add_account_shown {
  protocol_t speaks{};
  std::vector<std::string> proxies;
  std::optional<std::string> proxy;
  friend bool operator==(const add_account_shown&, const add_account_shown&) = default;
};
// The proxy chosen again, where the profiles moved: what was chosen stays so
// while it is there.
[[nodiscard]] inline add_account_shown with_proxies(add_account_shown now, std::vector<std::string> proxies) {
  if (now.proxy && std::ranges::find(proxies, *now.proxy) == proxies.end())
    now.proxy.reset();
  now.proxies = std::move(proxies);
  return now;
}

// XMPP or Matrix at the top, each named as its protocol names itself, the
// one whose form is up lit: in a thin frame.
template <class... Tags>
auto protocol_switch(const palette& colours, const protocol_t& speaks, protocol_list<Tags...>) {
  namespace c = skiff::compose;
  return c::row(c::hbox(1.0f, {.autoSize = scene::axes::kBoth, .padding = {1.0f, 1.0f, 1.0f, 1.0f}, .background = colours.chosen}),
                segment(colours, std::string(protocol_name(config::kept_of<Tags>{})), sends<request::add_account_of>{{protocol_t{Tags{}}}},
                        speaks.index() == protocol_t{Tags{}}.index())...);
}
using protocol_switch_t = decltype(protocol_switch(std::declval<const palette&>(), protocol_t{}, protocols{}));

// The proxy the new account goes through: none, or one of the profiles --
// and, always there, a way to make one.
struct proxy_choice {
  std::vector<std::string> names;
  std::optional<std::string> chosen;
  friend bool operator==(const proxy_choice&, const proxy_choice&) = default;
};
inline auto proxy_row(const palette& colours, const proxy_choice& now) {
  namespace c = skiff::compose;
  const auto one = [&](std::optional<std::string> name) {
    return segment(colours, name.value_or("None"), sends<request::choose_new_proxy>{{name}}, name == now.chosen);
  };
  return c::row(c::hbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                c::styled({.alignSelf = scene::align::kMiddle}, nodes::Text("Proxy", 13.0f, colours.dim)),
                c::many(c::hbox(4.0f, {.autoSize = scene::axes::kBoth}),
                        std::views::iota(std::size_t{0}, now.names.size() + 1) | std::views::transform([&](std::size_t at) {
                          return one(at == 0 ? std::nullopt : std::optional(now.names[at - 1]));
                        }) | std::ranges::to<std::vector>()),
                segment(colours, "Add proxy…", sends<request::manage_proxies>{}, false));
}
using proxy_row_t = decltype(proxy_row(std::declval<const palette&>(), proxy_choice{}));

// The protocol's form, in a box of its own -- the form is one of the
// protocols' (a variant), the box what comes in, made again as the
// protocol changes; the form itself as the program's facts say.
inline auto adding_form(const ui_needs& n) {
  namespace c = skiff::compose;
  return c::appearing(true, c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), account_form_view(n)), 0.0f);
}
using adding_form_t = decltype(adding_form(std::declval<const ui_needs&>()));

// Adding an account, beside the list of them: the switch, what its protocol
// says, the proxy row, and that protocol's form under them -- made anew
// where the protocol changes, coming in as it does.
inline auto add_account_view(const ui_needs& n) {
  namespace c = skiff::compose;
  const palette& colours = *n.colours;
  return c::column(c::vbox(12.0f, {.fill = true}),
      c::projected<add_account_shown>([](const add_account_shown& now) { return now.speaks; },
          c::mount<protocol_switch_t, protocol_t>(
              [at = &colours](const protocol_t& speaks) { return std::tuple{protocol_switch(*at, speaks, protocols{})}; },
              {.autoSize = scene::axes::kBoth})),
      c::text_for<add_account_shown>([](const add_account_shown& now) { return adding_note(now.speaks); },
                                     c::styled({.fillX = true}, wrapped(nodes::Text("", 13.0f, colours.dim)))),
      c::projected<add_account_shown>([](const add_account_shown& now) { return proxy_choice{now.proxies, now.proxy}; },
          c::mount<proxy_row_t, proxy_choice>(
              [at = &colours](const proxy_choice& now) { return std::tuple{proxy_row(*at, now)}; },
              {.fillX = true, .autoSize = scene::axes::kY})),
      c::projected<add_account_shown>([](const add_account_shown& now) { return now.speaks; },
          c::mount<adding_form_t, protocol_t>([at = &n](const protocol_t&) { return std::tuple{adding_form(*at)}; },
                                              {.fillX = true, .grow = scene::axes::kY})));
}
using add_account_view_t = decltype(add_account_view(std::declval<const ui_needs&>()));

}  // namespace mux::ui

