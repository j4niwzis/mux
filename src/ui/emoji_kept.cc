// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:emoji_kept -- What the emoji and sticker panels keep: the recent and favourite ones, the chat's own, the page open, and a held emote's preview.
export module mux.ui.base:emoji_kept;

import std;
import splice;
import skia;
import skiff.scene;
import skiff.compose;
import skiff.nodes.flow;
import skiff.nodes.image;
import skiff.nodes.text;
import mux.core;
import :base;
import :avatars;

export namespace mux::ui {

// What the mouse rests on in a panel of emoji or stickers, shown large over
// it, as Telegram's preview: its picture (or its glyph) and its name. Cells
// say it here as the mouse stays on them, and let it go as it leaves; the
// panel shows what is said -- no pointer between them.
struct previewed {
  std::string key;    // a picture's source, or a glyph
  std::string label;  // its :shortcode:, or nothing
  bool picture = false;
  emote_kind kind = emote_kind::emoji;
  friend bool operator==(const previewed&, const previewed&) = default;
};
// The emoji and stickers kept, as the program holds them: handed to the
// window with its colours (ui_needs), and from it to the panels.
inline std::optional<emote> copied_emoji_at(std::string_view target) {
  const auto& copied = skiff::scene::clipboardCandidate();
  if (!copied) return std::nullopt;
  const auto found = std::ranges::find_if(copied->atoms, [&](const auto& atom) { return atom.picture && atom.target == target; });
  if (found == copied->atoms.end()) return std::nullopt;
  std::string code = found->plain;
  if (code.starts_with(':') && code.ends_with(':') && code.size() > 2) code = code.substr(1, code.size() - 2);
  return emote{.shortcode = std::move(code), .url = found->target, .body = found->plain};
}
struct emoji_kept {
  bool custom_emoji = true;
  // The emoji picked lately, newest first, as tdesktop keeps them (at most 42).
  std::vector<std::string> recent_emoji;
  std::vector<emote> favourite_emoji;
  // The custom emoji and stickers of the chat the panel is opened over.
  emote_list chat_emotes, chat_stickers;
  std::optional<account_id> pack_account;
  // The stickers sent lately, newest first, as tdesktop's Recent (at most
  // 20); and the favourites, from a sticker's menu, in any chat.
  std::vector<emote> recent_stickers, favourite_stickers;
  // Whether the lists changed since the program last kept them.
  bool emoji_changed = false, stickers_changed = false;
  // The emoji or sticker the mouse rests on, shown large over its panel.
  std::optional<previewed> previewed_now;

  [[nodiscard]] bool emoji_is_favourite(std::string_view url) const { return std::ranges::contains(favourite_emoji, url, &emote::url); }
  void flip_emoji_favourite(const emote& one) {
    if (emoji_is_favourite(one.url)) std::erase_if(favourite_emoji, [&](const emote& each) { return each.url == one.url; });
    else favourite_emoji.insert(favourite_emoji.begin(), one);
    emoji_changed = true;
  }
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
[[nodiscard]] inline auto emote_preview(const palette& colours, const previewed& shown) {
  namespace c = skiff::compose;
  std::optional<nodes::Image<from_emotes>> picture;
  if (shown.picture)
    picture.emplace(c::styled({.width = 200.0f, .height = 200.0f, .alignSelf = scene::align::kMiddle},
                               nodes::Image<from_emotes>({shown.key, shown.kind})));
  return c::column(c::justified(c::vbox(8.0f, {.place = scene::anchor::kCentre,
      .autoSize = scene::axes::kBoth, .padding = {16.0f, 16.0f, 16.0f, 16.0f},
      .cornerRadius = 14.0f, .background = (colours.sidebar & 0x00FFFFFFu) | (0xF0u << 24)}),
      nodes::justify::middle{}),
    std::move(picture),
    c::styled({.alignSelf = scene::align::kMiddle}, c::visible(!shown.picture,
        nodes::Text(shown.picture ? std::string{} : shown.key, 120.0f, colours.text))),
    c::styled({.alignSelf = scene::align::kMiddle}, c::visible(!shown.label.empty(),
        nodes::Text(shown.label, 14.0f, colours.text))));
}
using emote_preview_t = decltype(emote_preview(std::declval<const palette&>(), std::declval<const previewed&>()));
// A panel's preview kept to what the cells say: made anew as it changes.
inline bool follow_preview(std::optional<emote_preview_t>& shown, std::optional<previewed>& of, const palette& colours, const emoji_kept& kept) {
  if (of == kept.previewed_now)
    return false;
  of = kept.previewed_now;
  shown.reset();
  if (of)
    shown.emplace(emote_preview(colours, *of));
  return true;
}

// What a panel's page shows of what is kept with the emoji: the preview of
// what the pointer rests on, over the page -- said by its cells, through
// what is kept (previewed_now) -- made anew as that changes.
template <class N> struct previewing : skiff::compose::Stacked {
  const palette* colours_ = nullptr;
  const emoji_kept* kept_ = nullptr;
  std::optional<previewed> of;
  struct parts_t {
    N page;
    std::optional<emote_preview_t> preview;
  } parts;
  previewing(const palette& colours, const emoji_kept& kept, N page)
      : Stacked(skiff::compose::vbox(0.0f, {.fill = true})), colours_(&colours), kept_(&kept), parts{.page = std::move(page)} {
    parts.page.apply({.fill = true});
  }
  [[nodiscard]] bool settling() const { return kept_->previewed_now != of; }
  void update(double) {
    if (follow_preview(parts.preview, of, *colours_, *kept_))
      this->invalidateLayout();
  }
};
template <class N> previewing<N> with_preview(const palette& colours, const emoji_kept& kept, N page) {
  return previewing<N>(colours, kept, std::move(page));
}

// The emote pictures a subtree shows within a rect of the window -- what is
// scrolled out of it left -- for the program to fetch. Any page's: it says
// nothing of them itself.
inline void emote_pictures_in(nodes::Image<from_emotes>& image, const skia::SkRect& view, std::vector<emote_picture>& out) {
  const auto shift = skiff::scene::work::drawnOffset(image.fState.fId);
  if (image.fState.visible() && image.bounds().makeOffset(shift.x, shift.y).intersects(view))
    out.push_back({image.source().key, image.source().kind});
}
template <class N> void emote_pictures_in(N& node, const skia::SkRect& view, std::vector<emote_picture>& out) {
  if (!skiff::scene::stateOf(node).visible())
    return;
  skiff::scene::eachChild(node, [&](auto& child) { emote_pictures_in(child, view, out); });
}

}  // namespace mux::ui
