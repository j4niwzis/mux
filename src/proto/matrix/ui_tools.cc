// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui.proto.matrix:tools -- Matrix's own dialogs: a step of interactive
// auth done in the browser.
module;
#include "skiff/named.h"
export module mux.ui.proto.matrix:tools;

import std;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.compose;
import skiff.nodes.flow;
import skiff.nodes.text;
import skiff.nodes.scroll;
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
import mux.proto.matrix;
import mux.proto.matrix.requests;
import mux.ui.base;

// ---- interactive auth ----------------------------------------------------------
namespace mux::proto::matrix::tools_detail {
using namespace ::mux::ui;

// A step of interactive auth done in the browser, as Element's: what it is
// for, its page opened there again where it was closed, and Continue once
// it is done there -- or Cancel.
inline auto uia_view(const palette& colours, const tool::uia_step& facts) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(uia_view, c::column(c::vbox(10.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {20.0f, 22.0f, 20.0f, 22.0f}}),
      nodes::Text(facts.what, 17.0f, colours.text, true),
      c::styled({.fillX = true},
                wrapped(nodes::Text("Your server asks you to confirm this in your browser: the page is open there. Once you have done what it asks, "
                                    "press Continue.",
                                    14.0f, colours.dim))),
      c::styled({.width = 200.0f, .height = 32.0f},
                widgets::Button<sends<::mux::ui::request::open_url>>(colours.widgets, "Open the page again", {{facts.url}})),
      dialog_buttons(colours, "Continue", sends<request::cancel_uia>{}, sends<request::continue_uia>{}, 120.0f)));
}

}  // namespace mux::proto::matrix::tools_detail

// Each dialog's content, by what it shows -- found where the window makes
// its dialogs, by the facts' namespace.
export namespace mux::proto::matrix::tool {
inline auto dialog_content(const ui::ui_needs& n, const uia_step& facts) { return tools_detail::uia_view(*n.colours, facts); }
template <class Needs, class Facts>
  requires requires(const Needs& n, const Facts& f) { dialog_content(n, f); }
auto make_content(const Needs& n, const Facts& facts) {
  return dialog_content(n, facts);
}
template <class Facts>
  requires requires(const ui::ui_needs& n, const Facts& f) { dialog_content(n, f); }
ui::dialog_look content_look(std::type_identity<Facts>) {
  return {};
}
}  // namespace mux::proto::matrix::tool
