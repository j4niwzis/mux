// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:message_pieces -- A message's marks, its first link, the fragment a quote marks, and its link's card and page preview.
module;
#include "../inline_in_release.h"
#include "skiff/named.h"
export module mux.ui.messages:message_pieces;

import std;
import chevron.escape;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.compose;
import skiff.nodes.box;
import skiff.nodes.flow;
import skiff.nodes.icon;
import skiff.nodes.image;
import skiff.nodes.text;
import skiff.widgets.loader;
import skiff.widgets.pill;
import mux.platform.audio;
import mux.core;
import mux.config;
import mux.logic.links;
import mux.protocols;
import mux.ui.base;
import mux.plugins.ui;
import mux.ui.proto;

export namespace mux::ui {

// What became of a message, said to the left of its time as tdesktop says
// "edited": removed (kept, as the settings say), or edited.
[[nodiscard]] std::string mark_of(const message& said) {
  return said.redacted ? std::string("removed ") : said.edited ? std::string("edited ") : std::string();
}

// A protocol link's card, composed from its stripe, its chat's face (where
// faces go: the avatars plugin's) and two lines. A builder: built where the
// message's row is.
MUX_INLINE_IN_RELEASE auto link_card(const palette& colours, std::string where, std::string avatar_id,
                      std::string avatar_name, std::string title, std::string said) {
  namespace c = skiff::compose;
  const auto line = [](std::string text, skia::SkColor colour, bool bold) {
    auto node = nodes::Text(std::move(text), 13.0f, colour, bold);
    node.setElided(true);
    node.setMaxWidth(360.0f);
    return node;
  };
  return SKIFF_NAMED(link_card, c::onClick(request::open_url{std::move(where)},
      c::row(c::hbox(8.0f, {.autoSize = scene::axes::kBoth, .margin = {4.0f, 0.0f, 2.0f, 0.0f}}),
          c::styled({.width = 3.0f, .height = 36.0f, .cornerRadius = 1.5f}, nodes::Box<>(colours.accent)),
          c::styled({.alignSelf = scene::align::kMiddle}, face_spot(avatar_of::chat{}, std::move(avatar_id), std::move(avatar_name), 32.0f)),
          c::column(c::vbox(1.0f, {.autoSize = scene::axes::kBoth, .alignSelf = scene::align::kMiddle}),
              line(std::move(title), colours.accent, true), line(std::move(said), colours.dim, false))),
      "Open linked room or message"));
}
using link_card_t = decltype(link_card(std::declval<const palette&>(), "", "", "", "", ""));

// The first link a message's text has: what its preview is of.
[[nodiscard]] std::optional<std::string> first_link_of(const message& said) {
  if (said.service)
    return std::nullopt;
  const auto spans = said.body.html ? read_html(*said.body.html).spans : link_spans_in(said.body.plain);
  for (const auto& span : spans)
    if (!span.picture && (span.target.starts_with("https://") || span.target.starts_with("http://")) &&
        !logic::link_of(span.target))  // a link some protocol reads is a mention's, not a page
      return span.target;
  return std::nullopt;
}

// A quoted stretch as a fragment to mark: its spaces at either end cut, and
// nothing where it is all spaces.
[[nodiscard]] std::optional<std::string> trimmed_fragment(std::string_view said) {
  const auto space = [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; };
  while (!said.empty() && space(said.back()))
    said.remove_suffix(1);
  while (!said.empty() && space(said.front()))
    said.remove_prefix(1);
  if (said.empty())
    return std::nullopt;
  return std::string(said);
}

// What a reply quoted of the message it answers, where it quoted some: the
// first quote in its own text -- its HTML's blockquote, or its plain lines
// after "> " -- and not the fallback that repeats who was answered.
[[nodiscard]] std::optional<std::string> quoted_fragment(const message& said) {
  std::string out;
  if (said.body.html) {
    const auto read = read_html(*said.body.html);
    for (const auto& style : read.styles)
      if (style.quote) {
        out = read.text.substr(style.first, style.last - style.first);
        break;
      }
  } else {
    for (std::size_t at = 0; at < said.body.plain.size();) {
      const auto end = said.body.plain.find('\n', at);
      const std::string_view line =
          std::string_view(said.body.plain).substr(at, end == std::string::npos ? std::string::npos : end - at);
      if (line.starts_with("> ") && !line.starts_with("> <")) {
        if (!out.empty())
          out += '\n';
        out += line.substr(2);
      } else if (!out.empty()) {
        break;
      }
      if (end == std::string::npos)
        break;
      at = end + 1;
    }
  }
  while (!out.empty() && std::isspace(static_cast<unsigned char>(out.back())))
    out.pop_back();
  while (!out.empty() && std::isspace(static_cast<unsigned char>(out.front())))
    out.erase(out.begin());
  if (out.empty())
    return std::nullopt;
  return out;
}


}  // namespace mux::ui
