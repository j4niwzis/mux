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
// The emoji and stickers kept, as the program holds them: handed to the
// window with its colours (ui_needs), and from it to the panels.
struct emoji_kept {
  // The emoji picked lately, newest first, as tdesktop keeps them (at most 42).
  std::vector<std::string> recent_emoji;
  // The custom emoji and stickers of the chat the panel is opened over.
  std::vector<emote> chat_emotes, chat_stickers;
  // The stickers sent lately, newest first, as tdesktop's Recent (at most
  // 20); and the favourites, from a sticker's menu, in any chat.
  std::vector<emote> recent_stickers, favourite_stickers;
  // Whether the lists changed since the program last kept them.
  bool emoji_changed = false, stickers_changed = false;
  // The emoji or sticker the mouse rests on, shown large over its panel.
  std::optional<previewed> previewed_now;

  void remember_emoji(const std::string& glyph) {
    constexpr std::size_t kKept = 42;
    std::erase(recent_emoji, glyph);
    recent_emoji.insert(recent_emoji.begin(), glyph);
    if (recent_emoji.size() > kKept)
      recent_emoji.resize(kKept);
    emoji_changed = true;
  }
  void remember_sticker(const emote& one) {
    constexpr std::size_t kKept = 20;
    std::erase_if(recent_stickers, [&](const emote& each) { return each.url == one.url; });
    recent_stickers.insert(recent_stickers.begin(), one);
    if (recent_stickers.size() > kKept)
      recent_stickers.resize(kKept);
    stickers_changed = true;
  }
  [[nodiscard]] bool is_favourite(std::string_view url) const { return std::ranges::contains(favourite_stickers, url, &emote::url); }
  // Made a favourite, or no longer one.
  void flip_favourite(const emote& one) {
    if (is_favourite(one.url))
      std::erase_if(favourite_stickers, [&](const emote& each) { return each.url == one.url; });
    else
      favourite_stickers.insert(favourite_stickers.begin(), one);
    stickers_changed = true;
  }
};

// The input's popup's pages.
namespace popup_page {
struct emoji {};
struct stickers {};
struct gifs {};
}  // namespace popup_page
using popup_page_t = spl::variant<popup_page::emoji, popup_page::stickers, popup_page::gifs>;

// How long the mouse rests on one before it is shown large.
inline constexpr double kPreviewAfterMs = 450.0;
// A cell's resting: said once the mouse has stayed long enough, let go as
// it leaves. Kept by each cell.
struct dwell {
  std::optional<double> since;
  bool said = false;
  // While it counts, frames are wanted.
  [[nodiscard]] bool counting(bool hovered) const { return hovered && !said; }
  void step(bool hovered, double now, const previewed& what, emoji_kept& kept) {
    if (!hovered) {
      since.reset();
      if (said && kept.previewed_now == what)
        kept.previewed_now.reset();
      said = false;
      return;
    }
    if (!since)
      since = now;
    if (!said && now - *since >= kPreviewAfterMs) {
      said = true;
      kept.previewed_now = what;
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
inline bool follow_preview(std::optional<emote_preview>& shown, std::optional<previewed>& of, const palette& colours, const emoji_kept& kept) {
  if (of == kept.previewed_now)
    return false;
  of = kept.previewed_now;
  shown.reset();
  if (of)
    shown.emplace(colours, *of);
  return true;
}

}  // namespace mux::ui
