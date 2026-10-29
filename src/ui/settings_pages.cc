// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:settings_pages -- Settings: the home page. (Proxies are the proxies plugin's: src/plugins/proxies.)
module;
#include "skiff/named.h"
export module mux.ui:settings_pages;

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
import mux.ui.base;
import mux.composition;
import mux.plugins.ui;
import :drawer;

export namespace mux::ui {

// ---- the settings -------------------------------------------------------------------

// Settings, as Telegram Desktop shows them: a box over the window, a list of
// sections, and each section a page of the same box. The plugins' links
// after the client's own (contract::settings_links).
template <class... Features>
auto settings_links_of(plugin_list<Features...>, const palette& colours) {
  return skiff::compose::column(skiff::compose::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      extend(contract::settings_links{}, type_tag<Features>{}, colours)...);
}
inline auto settings_home(const palette& colours) {
  return SKIFF_NAMED(settings_home, skiff::compose::column(
      skiff::compose::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      skiff::compose::styled({.depth = 1.0f, .background = colours.sidebar},
          page_header<sends<request::close_settings>, sends<request::close_settings>>(colours, "Settings", {}, {}, false, true)),
      settings_link(colours, "Accounts", icon::person{}, request::open_accounts{}),
      settings_link(colours, "Appearance", icon::eye{}, request::settings_appearance{}),
      settings_links_of(composition::providers<contract::settings_links, enabled_plugins>{}, colours)));
}
using settings_home_t = decltype(settings_home(std::declval<const palette&>()));


}  // namespace mux::ui
