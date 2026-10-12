// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.gifs.ui -- The GIFs' page of the media panel, as tdesktop's GIF
// tab shows them: a grid of the saved ones playing, newest first; a press
// sends one into the chat.
export module mux.plugin.gifs.ui;

import std;
import skiff.scene;
import skiff.compose;
import skiff.nodes.flow;
import skiff.nodes.image;
import skiff.nodes.scroll;
import skiff.nodes.text;
import mux.ui;
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
template <class Actions>
auto extend(contract::media_pages, type_tag<feature>, const ui::ui_needs<Actions>& needs, const shown& now) {
  return grid(*needs.colours, now.paths);
}

}  // namespace mux::plugins::gifs
