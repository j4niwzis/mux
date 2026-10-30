// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.catalog.ui -- Settings' Plugins page: each plugin built in, in
// their order, with what it does, its licence, and whether it is mux's own
// or a third party's; pressed, its own page -- its repository, its licence
// and its README (mux.plugins.info). Linked from Settings' home.
module;
#include "../../inline_in_release.h"
#include "skiff/named.h"
export module mux.plugin.catalog.ui;

import std;
import skiff.compose;
import skiff.nodes.text;
import mux.core;
import mux.composition;
import mux.ui.base;
import mux.plugins.info;
import mux.plugin.catalog.types;

export namespace mux::plugins::catalog {

using namespace ::mux::ui;

MUX_INLINE_IN_RELEASE auto catalog_words(const palette& colours, std::string text, float size = 14.0f) {
  return wrapped(skiff::compose::styled({.fillX = true, .margin = {8.0f, 20.0f, 8.0f, 20.0f}},
                                        nodes::Text(std::move(text), size, colours.text)));
}
// Whose a plugin is.
[[nodiscard]] inline std::string_view origin_of(const plugin_info& one) { return one.bundled ? "mux" : "third party"; }

MUX_INLINE_IN_RELEASE auto list_view(const palette& colours) {
  auto rows = plugin_infos | std::views::enumerate | std::views::transform([&](const auto& each) {
                const auto& [at, one] = each;
                return settings_link(colours, std::format("{} · {} · {}", one.name, one.license, origin_of(one)), icon::gear{},
                                     open_plugin{static_cast<std::size_t>(at)});
              }) | std::ranges::to<std::vector>();
  return SKIFF_NAMED(catalog_list_view, skiff::compose::column(
      skiff::compose::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      page_header<sends<request::settings_home>, sends<request::close_settings>>(colours, "Plugins", {}, {}, true, true),
      catalog_words(colours, std::format("{} plugins built in. Third parties' are built in the same way, from their own folders "
                                         "(MUX_EXTRA_PLUGINS).", plugin_infos.size()), 13.0f),
      skiff::compose::many(skiff::compose::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), std::move(rows))));
}
MUX_INLINE_IN_RELEASE auto plugin_view(const palette& colours, std::size_t index) {
  const plugin_info missing{"unknown", "", "", "", "", true};
  const plugin_info& one = index < plugin_infos.size() ? plugin_infos[index] : missing;
  auto repository = settings_link(colours, "Repository", icon::gear{}, request::open_url{std::string(one.repository)});
  repository.setVisible(!one.repository.empty());
  return SKIFF_NAMED(catalog_plugin_view, skiff::compose::column(
      skiff::compose::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      page_header<sends<open_list>, sends<request::close_settings>>(colours, std::string(one.name), {}, {}, true, true),
      catalog_words(colours, std::string(one.summary)),
      catalog_words(colours, std::format("Licence: {}\nFrom: {}\nRepository: {}", one.license, origin_of(one),
                                         one.repository.empty() ? std::string_view("not given") : one.repository), 13.0f),
      std::move(repository),
      catalog_words(colours, one.readme.empty() ? std::string("No README.") : std::string(one.readme), 13.0f)));
}

// Settings' link to it.
MUX_INLINE_IN_RELEASE auto extend(contract::settings_links, type_tag<feature>, const palette& colours) {
  return settings_link(colours, "Plugins", icon::gear{}, open_list{});
}
// Its pages.
MUX_INLINE_IN_RELEASE auto extend(contract::settings_pages, const ui_needs& n, const list_page&) { return list_view(*n.colours); }
MUX_INLINE_IN_RELEASE auto extend(contract::settings_pages, const ui_needs& n, const plugin_page& page) { return plugin_view(*n.colours, page.index); }

}  // namespace mux::plugins::catalog
