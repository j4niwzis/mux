// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui.proto.matrix:form -- Matrix's account form.
module;
#include "skiff/named.h"
export module mux.ui.proto.matrix:form;

import std;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.compose;
import skiff.model;
import skiff.nodes.flow;
import skiff.nodes.text;
import skiff.widgets.button;
import skiff.widgets.model;
import mux.core;
import mux.config;
import mux.proto.kept;
import mux.proto.matrix;
import mux.proto.matrix.requests;
import mux.ui.base;

export namespace mux::proto::matrix {

// What the add-account pane says of the protocol.
[[nodiscard]] constexpr std::string_view form_note(const account_draft&) {
  return "A user ID like @user:example.org, on a homeserver such as Synapse.";
}

// A Matrix account's settings, over the draft typed into them (kept by the
// form, sent whole as it is submitted): its user ID; signed in with a
// password typed here, or in the browser on the server's own page (OAuth
// 2.0); the homeserver (found through the server's .well-known when left
// empty) and what this device is called. A new account -- never one being
// edited -- is registered on the server first: where the server's page does
// not do it, with a registration token and the user's word that they agree
// to the server's terms.
inline auto account_form_of(const account_draft& seed, const ui::palette& colours, bool editing) {
  namespace c = skiff::compose;
  using namespace ::mux::ui;
  using d = account_draft;
  const auto registers_here = [](const d& now) { return now.creating && !now.in_browser; };
  return SKIFF_NAMED(account_form_of, c::local<d>(
      submitted_by_enter<d>(c::column(
          c::vbox(12.0f, {.fillX = true, .autoSize = scene::axes::kY}),
          model_field<&d::user_id>(colours, "User ID", "@user:example.org"),
          choice_field<&d::in_browser, bool>(colours, "Sign in",
                                             {{"With a password", false}, {"In the browser, on the server's page", true}}),
          c::shown_for<d>([](const d& now) { return !now.in_browser; }, model_field<&d::password>(colours, "Password", "Password", true)),
          model_field<&d::homeserver>(colours, "Homeserver", "found through the server's .well-known"),
          model_field<&d::device_name>(colours, "Device name", "mux"),
          c::visible(!editing, choice_field<&d::creating, bool>(colours, "Account",
                                                                {{"Sign in to an account", false}, {"Create a new account", true}})),
          c::shown_for<d>(registers_here,
                          model_field<&d::registration_token>(colours, "Registration token", "where the server registers by invitation")),
          c::shown_for<d>(registers_here, choice_field<&d::agrees, bool>(colours, "The server's terms",
                                                                         {{"Not agreed to", false}, {"I agree to the server's terms", true}})),
          account_form_end<d>(colours, editing))),
      seed));
}

}  // namespace mux::proto::matrix
