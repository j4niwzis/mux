// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:emoji_kept -- What the emoji and sticker panels keep: the recent and favourite ones, the chat's own, the page open, and a held emote's preview.
export module mux.ui:emoji_kept;

import std;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes.box;
import skiff.nodes.flow;
import skiff.nodes.image;
import skiff.nodes.scroll;
import skiff.nodes.text;
import skiff.widgets.textbox;
import mux.core;
import mux.config;
import mux.logic.emoji;
import :base;
import :icons;
import :controls;
import :themes;
import :avatars;
import :timeline;
import :conversations;
import :forms;
import :names;

export namespace mux::ui {

// The emoji picked lately, newest first, as tdesktop keeps them (at most
// 42); and what is told when one is picked, so that the program keeps the
// list in its file.
inline std::vector<std::string>& recent_emoji() {
  static std::vector<std::string> kept;
  return kept;
}
// Whether the list changed since the program last kept it: the program
// reads it, and keeps the list.
inline bool& recent_emoji_changed() {
  static bool changed = false;
  return changed;
}
// The custom emoji of the chat the panel is opened over, as the program says.
inline std::vector<emote>& chat_emotes() {
  static std::vector<emote> kept;
  return kept;
}
// And its stickers.
inline std::vector<emote>& chat_stickers() {
  static std::vector<emote> kept;
  return kept;
}

// The input's popup's pages.
namespace popup_page {
struct emoji {};
struct stickers {};
struct gifs {};
}  // namespace popup_page
using popup_page_t = splice::variant<popup_page::emoji, popup_page::stickers, popup_page::gifs>;

// What the mouse rests on in a panel of emoji or stickers, shown large over
// it, as Telegram's preview: its picture (or its glyph) and its name. Cells
// say it here as the mouse stays on them, and let it go as it leaves; the
// panel shows what is said -- no pointer between them.
struct previewed {
  std::string key;    // a picture's source, or a glyph
  std::string label;  // its :shortcode:, or nothing
  bool picture = false;
  friend bool operator==(const previewed&, const previewed&) = default;
};
inline std::optional<previewed>& previewed_emote() {
  static std::optional<previewed> now;
  return now;
}
// How long the mouse rests on one before it is shown large.
inline constexpr double kPreviewAfterMs = 450.0;
// A cell's resting: said once the mouse has stayed long enough, let go as
// it leaves. Kept by each cell.
struct dwell {
  std::optional<double> since;
  bool said = false;
  // While it counts, frames are wanted.
  [[nodiscard]] bool counting(bool hovered) const { return hovered && !said; }
  void step(bool hovered, double now, const previewed& what) {
    if (!hovered) {
      since.reset();
      if (said && previewed_emote() == what)
        previewed_emote().reset();
      said = false;
      return;
    }
    if (!since)
      since = now;
    if (!said && now - *since >= kPreviewAfterMs) {
      said = true;
      previewed_emote() = what;
    }
  }
};
// The preview, over a panel: a dark plate, the picture or glyph large, its
// name under it.
struct emote_preview : nodes::Stack {
  static constexpr float kSide = 200.0f;
  struct parts_t {
    std::optional<nodes::Image<from_avatars>> picture;
    nodes::Text glyph;
    nodes::Text label;
  } parts;
  emote_preview(const palette& colours, const previewed& shown)
      : parts{.glyph = nodes::Text(shown.picture ? std::string() : shown.key, 120.0f, colours.text),
              .label = nodes::Text(shown.label, 14.0f, colours.text)} {
    this->setGap(8.0f);
    fStack.justify = nodes::justify::middle{};
    fState.apply({.place = scene::anchor::kCentre, .autoSize = scene::axes::kBoth,
                  .padding = {16.0f, 16.0f, 16.0f, 16.0f}, .cornerRadius = 14.0f,
                  .background = (colours.sidebar & 0x00FFFFFFu) | (0xF0u << 24)});
    if (shown.picture) {
      parts.picture.emplace(from_avatars{shown.key});
      parts.picture->apply({.width = kSide, .height = kSide, .alignSelf = scene::align::kMiddle});
    }
    parts.glyph.setVisible(!shown.picture);
    parts.glyph.apply({.alignSelf = scene::align::kMiddle});
    parts.label.setVisible(!shown.label.empty());
    parts.label.apply({.alignSelf = scene::align::kMiddle});
  }
};
// A panel's preview kept to what the cells say: made anew as it changes.
inline bool follow_preview(std::optional<emote_preview>& shown, std::optional<previewed>& of, const palette& colours) {
  if (of == previewed_emote())
    return false;
  of = previewed_emote();
  shown.reset();
  if (of)
    shown.emplace(colours, *of);
  return true;
}

// The stickers sent lately, newest first, as tdesktop's Recent (at most 20):
// kept while the program runs.
inline std::vector<emote>& recent_stickers() {
  static std::vector<emote> kept;
  return kept;
}
// The favourites, as tdesktop's Favorite stickers: from a sticker's menu,
// in any chat; newest first.
inline std::vector<emote>& favourite_stickers() {
  static std::vector<emote> kept;
  return kept;
}
// Whether either list changed since the program last kept them.
inline bool& stickers_changed() {
  static bool changed = false;
  return changed;
}
inline void remember_sticker(const emote& one) {
  constexpr std::size_t kKept = 20;
  auto& all = recent_stickers();
  std::erase_if(all, [&](const emote& each) { return each.url == one.url; });
  all.insert(all.begin(), one);
  if (all.size() > kKept)
    all.resize(kKept);
  stickers_changed() = true;
}
[[nodiscard]] inline bool is_favourite(std::string_view url) {
  return std::ranges::contains(favourite_stickers(), url, &emote::url);
}
// Made a favourite, or no longer one.
inline void flip_favourite(const emote& one) {
  auto& all = favourite_stickers();
  if (is_favourite(one.url))
    std::erase_if(all, [&](const emote& each) { return each.url == one.url; });
  else
    all.insert(all.begin(), one);
  stickers_changed() = true;
}

}  // namespace mux::ui
