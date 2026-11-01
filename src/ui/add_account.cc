// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:add_account -- Adding an account.
module;
#include "../inline_in_release.h"
#include "skiff/named.h"
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
import mux.composition;
import mux.plugins.ui;
import :account_forms;

export namespace mux::ui {

// ---- adding an account ------------------------------------------------------------
// What the pane shows, as the program says: the protocol whose form is up.
// The program keeps it: a press asks it (add_account_of) and it says the
// new state.
struct add_account_shown {
  protocol_t speaks{};
  friend bool operator==(const add_account_shown&, const add_account_shown&) = default;
};

// XMPP or Matrix at the top, each named as its protocol names itself, the
// one whose form is up lit: in a thin frame.
template <class... Tags>
auto protocol_switch(const palette& colours, const protocol_t& speaks, protocol_list<Tags...>) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(protocol_switch, c::row(c::hbox(1.0f, {.autoSize = scene::axes::kBoth, .padding = {1.0f, 1.0f, 1.0f, 1.0f}, .background = colours.chosen}),
                segment(colours, std::string(protocol_name(config::kept_of<Tags>{})), sends<request::add_account_of>{{protocol_t{Tags{}}}},
                        speaks.index() == protocol_t{Tags{}}.index())...));
}
using protocol_switch_t = decltype(protocol_switch(std::declval<const palette&>(), protocol_t{}, protocols{}));

// What the plugins put in the pane (contract::account_adding), in their
// order.
template <class... Features>
auto adding_parts_of(plugin_list<Features...>, const palette& colours) {
  return skiff::compose::column(skiff::compose::vbox(12.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                                extend(contract::account_adding{}, type_tag<Features>{}, colours)...);
}

// The protocol's form, in a box of its own -- the form is one of the
// protocols' (a variant), the box what comes in, made again as the
// protocol changes; the form itself as the program's facts say.
MUX_INLINE_IN_RELEASE auto adding_form(const ui_needs& n) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(adding_form, c::appearing(true, c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), account_form_view(n)), 0.0f));
}
using adding_form_t = decltype(adding_form(std::declval<const ui_needs&>()));

// Adding an account, beside the list of them: the switch, what its protocol
// says, what the plugins put there, and that protocol's form under them -- made anew
// where the protocol changes, coming in as it does.
MUX_INLINE_IN_RELEASE auto add_account_view(const ui_needs& n) {
  namespace c = skiff::compose;
  const palette& colours = *n.colours;
  // As tall as what it holds: the detail pane scrolls it, and a column as
  // tall as its pane's content, filled, was 0 high -- the form in it, grown
  // into what that left, nothing, and everything after the switch outside.
  return SKIFF_NAMED(add_account_view, c::column(c::vbox(12.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      c::projected<add_account_shown>([](const add_account_shown& now) { return now.speaks; },
          c::mount<protocol_switch_t, protocol_t>(
              [at = &colours](const protocol_t& speaks) { return std::tuple{protocol_switch(*at, speaks, protocols{})}; },
              {.autoSize = scene::axes::kBoth})),
      c::text_for<add_account_shown>([](const add_account_shown& now) { return adding_note(now.speaks); },
                                     c::styled({.fillX = true}, wrapped(nodes::Text("", 13.0f, colours.dim)))),
      adding_parts_of(composition::providers<contract::account_adding, enabled_plugins>{}, colours),
      c::projected<add_account_shown>([](const add_account_shown& now) { return now.speaks; },
          c::mount<adding_form_t, protocol_t>([at = &n](const protocol_t&) { return std::tuple{adding_form(*at)}; },
                                              {.fillX = true, .autoSize = scene::axes::kY}))));
}
using add_account_view_t = decltype(add_account_view(std::declval<const ui_needs&>()));

}  // namespace mux::ui

