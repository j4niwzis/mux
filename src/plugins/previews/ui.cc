// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.previews.ui -- A link's preview, as Telegram's: under the text,
// a stripe in the accent, the site's name in it, the page's title and a few
// lines about it, and its picture on the right. Pressed -- it is a link --
// the link is followed; lit under the pointer, as a link is. The previews
// themselves are the program's, fetched as the chat's settings say.
module;
#include "skiff/named.h"
export module mux.plugin.previews.ui;

import std;
import skia;
import skiff.scene;
import skiff.compose;
import skiff.nodes.box;
import skiff.nodes.image;
import skiff.nodes.text;
import mux.composition;
import mux.core;
import mux.ui.base;
import mux.plugin.previews.types;

export namespace mux::plugins::previews {
using namespace ::mux::ui;

// A preview's line about its page, as Telegram shows one: its whitespace --
// the page's own lines, a menu's items one under another -- run together,
// and cut at a word near 200 bytes.
[[nodiscard]] inline std::string preview_line(std::string_view said) {
  std::string out;
  bool gap = false;
  for (const char c : said) {
    if (c == ' ' || c == '\n' || c == '\t' || c == '\r') {
      gap = !out.empty();
      continue;
    }
    if (gap)
      out += ' ';
    gap = false;
    out += c;
  }
  constexpr std::size_t kMost = 200;
  if (out.size() > kMost) {
    std::size_t cut = out.rfind(' ', kMost);
    if (cut == std::string::npos || cut < kMost / 2)
      cut = kMost;
    while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80)
      --cut;  // not inside a character
    out.resize(cut);
    out += "…";
  }
  return out;
}
inline auto page_preview(const palette& colours, const link_preview& shown, std::string where) {
  namespace c = skiff::compose;
  std::optional<nodes::Image<from_avatars>> picture;
  if (shown.image)
    picture.emplace(c::styled({.width = 56.0f, .height = 56.0f, .cornerRadius = 6.0f}, nodes::Image<from_avatars>(from_avatars{*shown.image})));
  return SKIFF_NAMED(page_preview, c::onClick(request::open_url{std::move(where)},
      c::row(c::hbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY, .margin = {6.0f, 0.0f, 2.0f, 0.0f},
                            .padding = {4.0f, 6.0f, 4.0f, 0.0f}, .cornerRadius = 4.0f,
                            .background = (colours.accent & 0x00FFFFFFu) | (0x18u << 24),
                            .hoverBackground = (colours.accent & 0x00FFFFFFu) | (0x34u << 24)}),
             c::styled({.fillY = true, .width = 3.0f, .cornerRadius = 1.5f}, nodes::Box<>(colours.accent)),
             c::column(c::vbox(1.0f, {.autoSize = scene::axes::kY, .grow = scene::axes::kX}),
                       c::visible(!shown.site.empty(), c::styled({.fillX = true}, elided(nodes::Text(shown.site, 13.0f, colours.accent, true)))),
                       c::visible(!shown.title.empty(), c::styled({.fillX = true}, elided(nodes::Text(shown.title, 13.0f, colours.text, true)))),
                       c::visible(!shown.description.empty(),
                                  c::styled({.fillX = true}, wrapped(nodes::Text(preview_line(shown.description), 13.0f, colours.text))))),
             std::move(picture)),
      "Open link"));
}
// The preview, marked with its address: what a menu over it copies.
struct previewed_link {
  std::string url;
};
using card = skiff::compose::Marked<previewed_link, decltype(page_preview(std::declval<const palette&>(), std::declval<const link_preview&>(),
                                                                         std::string()))>;

// Its card in a message's bubble (mux.ui.messages:message): the first
// link's preview, where the chat shows them and it has come.
inline std::optional<card> extend(contract::message_cards, type_tag<feature>, const card_facts& facts) {
  if (!facts.link || !facts.now || !facts.previews_shown)
    return std::nullopt;
  const auto found = facts.now->previews.find(*facts.link);
  if (found == facts.now->previews.end())
    return std::nullopt;
  return card(page_preview(facts.colours, found->second, *facts.link), previewed_link{*facts.link});
}
// The link a press on it is on, for the message's menu.
inline std::optional<std::string> pressed_link(contract::message_cards, type_tag<feature>, const card& shown, float x, float y) {
  return shown.bounds().contains(x, y) ? std::optional(shown.fTag.url) : std::nullopt;
}

}  // namespace mux::plugins::previews
