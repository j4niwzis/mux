// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.files.ui -- Settings' Files page -- what is done to a picture
// dropped on the window before it is sent -- linked from Settings' home
// (contract::settings_links, settings_pages).
module;
#include "../../inline_in_release.h"
#include "skiff/named.h"
export module mux.plugin.files.ui;

import std;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes.flow;
import skiff.nodes.scroll;
import skiff.nodes.text;
import skiff.model;
import skiff.compose;
import skiff.widgets.model;
import skiff.bind;
import mux.core;
import mux.config;
import mux.composition;
import mux.ui.base;
import mux.plugin.files.types;

export namespace mux::plugins::files {

using namespace ::mux::ui;

// Settings' Files page: what is done to a picture dropped on the window
// before it is sent.
MUX_INLINE_IN_RELEASE auto files_settings_view(const palette& colours) {
  using sending = config::sending_settings;
  return SKIFF_NAMED(files_settings_view, skiff::compose::column(
      skiff::compose::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      spaced_title(colours, "PICTURES DROPPED ON THE WINDOW"),
      setting_switch<&sending::strip_metadata>(colours, "Remove metadata"),
      setting_switch<&sending::rename>(colours, "Name them image.<type>"),
      spaced_note(colours, "Metadata is where and when a picture was taken, with what, by whom: EXIF, XMP and the like. "
                           "It is cut out of the file; the picture itself is sent as it is, not compressed again.")));
}
MUX_INLINE_IN_RELEASE auto files_view(const palette& colours) {
  using header = page_header_t<sends<request::settings_home>, sends<request::close_settings>>;
  return SKIFF_NAMED(files_view, skiff::compose::column(
      skiff::compose::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      skiff::compose::styled({.depth = 1.0f, .background = colours.sidebar}, page_header<sends<request::settings_home>, sends<request::close_settings>>(colours, "Files", {}, {}, true, true)),
      files_settings_view(colours)));
}


// Settings' link to it, and the page.
MUX_INLINE_IN_RELEASE auto extend(contract::settings_links, type_tag<feature>, const palette& colours) {
  return settings_link(colours, "Files", icon::send{}, open_files{});
}
MUX_INLINE_IN_RELEASE auto extend(contract::settings_pages, const ui_needs& n, const files_page&) { return files_view(*n.colours); }

}  // namespace mux::plugins::files
