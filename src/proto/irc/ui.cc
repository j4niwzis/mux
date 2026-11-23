// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui.proto.irc -- What IRC shows of its own in the client's UI: its
// account form, found by ADL where the panels are made.
module;
#include "skiff/named.h"
export module mux.ui.proto.irc;

import std;
import splice;
import skiff.paint;
import skiff.scene;
import skiff.compose;
import skiff.nodes.flow;
import skiff.nodes.image;
import skiff.nodes.text;
import skiff.nodes.scroll;
import skiff.widgets.dropdown;
import skiff.widgets.button;
import skiff.widgets.motion;
import skiff.widgets.sliderbar;
import skiff.widgets.textarea;
import skiff.widgets.textbox;
import skiff.widgets.model;
import skiff.model;
import mux.core;
import mux.config;
import mux.proto.kept;
import mux.ui.base;
import mux.proto.irc;

namespace mux::proto::irc::form_detail {
using namespace ::mux::ui;

// What "Advanced" folds out: SASL's account name where it is not the nick,
// the server's own password (a bouncer's), the user and real names.
inline auto advanced_fields(const palette& colours) {
  namespace c = skiff::compose;
  using d = account_draft;
  return SKIFF_NAMED(irc_advanced_fields, c::column(c::vbox(8.0f) | c::fill_x | c::content_y | c::with({.padding = {0.0f, 0.0f, 12.0f, 0.0f}}),
                   model_field<&d::login>(colours, "SASL account", "the nick"),
                   model_field<&d::server_password>(colours, "Server password (PASS)", "none", true),
                   model_field<&d::username>(colours, "User name", "the nick"),
                   model_field<&d::realname>(colours, "Real name", "the nick")));
}

}  // namespace mux::proto::irc::form_detail

export namespace mux::proto::irc {

// What the add-account pane says of the protocol.
[[nodiscard]] constexpr std::string_view form_note(const account_draft&) {
  return "An address like ircs://nick@irc.libera.chat -- TLS from the start; irc:// for a server without it.";
}
// An IRC account's settings, over the draft typed into them: its address,
// the password SASL logs in with (none: no SASL), the channels to be in, and
// "Advanced" folding out the rest.
inline auto account_form_of(const account_draft& seed, const ui::palette& colours, bool editing) {
  namespace c = skiff::compose;
  using namespace ::mux::ui;
  using d = account_draft;
  return SKIFF_NAMED(irc_account_form_of, c::local<d>(
      submitted_by_enter<d>(c::column(
          c::vbox(12.0f) | c::fill_x | c::content_y,
          model_field<&d::address>(colours, "Address", "ircs://nick@irc.libera.chat"),
          model_field<&d::password>(colours, "Password (SASL)", "none", true),
          model_field<&d::channels>(colours, "Channels", "#mux #cairn"),
          check_field<&d::advanced>(colours, "Advanced"),
          c::projected<d>([](const d& now) { return now.advanced; }, widgets::collapsible(form_detail::advanced_fields(colours))),
          account_form_end<d>(colours, editing))),
      seed));
}

// What an IRC profile holds: no banner; here, or away.
inline ui::profile_policy profile_policy_of(const state&) {
  return {false, {{"Online", availability::online{}}, {"Away", availability::away{}}}};
}

}  // namespace mux::proto::irc
