// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.gifs.ui -- The GIFs' page of the media panel, as tdesktop's GIF
// tab shows them: a grid of the saved ones playing, newest first; a press
// sends one into the chat. And Save GIF, in a moving picture's menu.
export module mux.plugin.gifs.ui;

import std;
import mux.composition;
import skiff.scene;
import skiff.compose;
import skiff.nodes.flow;
import skiff.nodes.image;
import skiff.nodes.scroll;
import skiff.nodes.text;
import mux.ui.base;
import mux.plugin.gifs.types;

export namespace mux::plugins::gifs {

inline auto cell(const ui::palette& colours, std::string path) {
  const std::string key = "gif:" + path;
  auto image = skiff::nodes::Image<ui::from_moving_whole>({key});
  image.keepBox();
  return skiff::compose::onClick(
      ui::request::send_gif{std::move(path)},
      skiff::compose::row(
          skiff::compose::vbox(0.0f, {.width = 104.0f,
                                     .height = 104.0f,
                                     .margin = {2.0f, 2.0f, 2.0f, 2.0f},
                                     .cornerRadius = 6.0f,
                                     .background = colours.tile,
                                     .masking = true}),
          skiff::compose::styled({.fill = true, .cornerRadius = 6.0f}, std::move(image))));
}
inline auto grid(const ui::palette& colours, const std::vector<std::string>& paths) {
  namespace c = skiff::compose;
  namespace scene = skiff::scene;
  namespace nodes = skiff::nodes;
  auto cells = std::ranges::to<std::vector>(std::views::transform(paths, [&](const auto& path) {
    return cell(colours, path);
  }));
  return c::column(c::vbox(0.0f, {.padding = {4.0f, 4.0f, 4.0f, 4.0f}}),
      c::visible(paths.empty(), c::styled({.fillX = true, .margin = {12.0f, 12.0f, 0.0f, 12.0f}},
          ui::wrapped(nodes::Text("No saved GIFs yet. Save one from a GIF's menu.", 13.0f, colours.dim)))),
      c::styled({.fillX = true, .grow = scene::axes::kY}, nodes::ScrollContainer(
          c::styled({.fillX = true, .autoSize = scene::axes::kY},
              nodes::Flow<std::vector<decltype(cell(colours, std::string()))>>(
                  {.direction = nodes::direction::horizontal{}, .wrap = true}, std::move(cells))))));
}

// Its page, in the media panel (mux.ui:emoji_popup).
auto extend(contract::media_pages, type_tag<feature>, const ui::ui_needs& needs, const shown& now) {
  return grid(*needs.colours, now.paths);
}

// Save GIF, in the menu of a GIF or a moving WebP (mux.ui:context_menu).
auto extend(contract::message_menu, type_tag<feature>, const ui::palette& colours, const ui::emoji_kept&,
            const ui::menu_facts& facts) {
  using row = ui::row_item<ui::sends<save>>;
  return skiff::compose::visible(facts.media.has_value() && facts.moving,
      row(colours, "Save GIF", ui::sends<save>{save{facts.media.value_or(std::string())}}, ui::icon::check{}, std::nullopt,
          ui::menu_row_look));
}

}  // namespace mux::plugins::gifs
