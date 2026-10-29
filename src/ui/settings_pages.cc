// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:settings_pages -- Settings: the home page, animations. (Proxies are the proxies plugin's: src/plugins/proxies.)
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
      settings_link(colours, "Animations", icon::motion{}, request::settings_animations{}),
      settings_link(colours, "Appearance", icon::eye{}, request::settings_appearance{}),
      settings_link(colours, "Rendering", icon::sliders{}, request::settings_rendering{}),
      settings_link(colours, "Storage", icon::clip{}, request::settings_storage{}),
      settings_link(colours, "Files", icon::send{}, request::settings_files{}),
      settings_links_of(composition::providers<contract::settings_links, enabled_plugins>{}, colours)));
}
using settings_home_t = decltype(settings_home(std::declval<const palette&>()));

inline auto motion_settings_view(const palette& colours) {
  using field = skiff::model::Field<&config::look_settings::motion>;
  return SKIFF_NAMED(motion_settings_view, skiff::compose::column(
      skiff::compose::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      wrapped(skiff::compose::styled({.fillX = true, .margin = {4.0f, 20.0f, 12.0f, 20.0f}},
          note_text(colours, "How much the window moves. Reduced keeps the small movements, such as a section unfolding, and shows panels at once."))),
      skiff::compose::bound<field>(widgets::ChoiceRowField<config::motion_t>(colours.widgets, "Full", config::motion::full{})),
      skiff::compose::bound<field>(widgets::ChoiceRowField<config::motion_t>(colours.widgets, "Reduced", config::motion::reduced{})),
      skiff::compose::bound<field>(widgets::ChoiceRowField<config::motion_t>(colours.widgets, "None", config::motion::none{}))));
}
inline auto animations_page(const palette& colours) {
  return SKIFF_NAMED(animations_page, skiff::compose::column(
      skiff::compose::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      skiff::compose::styled({.depth = 1.0f, .background = colours.sidebar},
          page_header<sends<request::settings_home>, sends<request::close_settings>>(colours, "Animations", {}, {}, true, true)),
      motion_settings_view(colours)));
}
using animations_page_t = decltype(animations_page(std::declval<const palette&>()));

}  // namespace mux::ui
