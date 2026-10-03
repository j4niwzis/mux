// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:message_pieces -- A message's marks, its first link, the fragment a quote marks, and its link's card and page preview.
export module mux.ui:message_pieces;

import std;
import chevron.escape;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
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
import :base;
import :icons;
import :avatars;
import :controls;
import :themes;
import :names;
import :html;

export namespace mux::ui {

// What became of a message, said to the left of its time as tdesktop says
// "edited": removed (kept, as the settings say), or edited.
[[nodiscard]] inline std::string mark_of(const message& said) {
  return said.redacted ? std::string("removed ") : said.edited ? std::string("edited ") : std::string();
}

// A link to a room or to a message in one, under the message that has it,
// as a card: a bar in the accent, the room's avatar, its name -- or
// "Message from" it -- over a second line: what the room is, or who said
// the message and a line of it where it is here. A press on it is seen by
// the messages' list, which follows it.
struct link_card : nodes::Stack {
  std::string url;
  struct bar : scene::Node {
    explicit bar(const palette& colours) {
      fState.apply({.width = 3.0f, .height = 36.0f, .cornerRadius = 1.5f, .background = colours.accent});
    }
  };
  struct texts_column : nodes::Stack {
    struct parts_t {
      nodes::Text title;
      nodes::Text said;
    } parts;
    texts_column(const palette& colours, std::string t, std::string s)
        : parts{.title = nodes::Text(std::move(t), 13.0f, colours.accent, true),
                .said = nodes::Text(std::move(s), 13.0f, colours.dim)} {
      this->setGap(1.0f);
      fState.apply({.autoSize = scene::axes::kBoth, .alignSelf = scene::align::kMiddle});
      for (nodes::Text* each : {&parts.title, &parts.said}) {
        each->setElided(true);
        each->setMaxWidth(360.0f);
      }
    }
  };
  struct parts_t {
    bar line;
    avatar_mark face;
    texts_column texts;
  } parts;
  link_card(const palette& colours, std::string where, std::string avatar_id, std::string avatar_name, std::string title,
            std::string said)
      : url(std::move(where)),
        parts{.line = bar(colours),
              .face = avatar_mark(std::move(avatar_id), std::move(avatar_name), 32.0f),
              .texts = texts_column(colours, std::move(title), std::move(said))} {
    this->setHorizontal();
    this->setGap(8.0f);
    fState.apply({.autoSize = scene::axes::kBoth, .margin = {4.0f, 0.0f, 2.0f, 0.0f}});
    parts.face.apply({.alignSelf = scene::align::kMiddle});
    fState.setCursor(scene::cursor::hand{});
  }
  [[nodiscard]] bool acceptsInput() const { return true; }
};

// The first link a message's text has: what its preview is of.
[[nodiscard]] inline std::optional<std::string> first_link_of(const message& said) {
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
[[nodiscard]] inline std::optional<std::string> trimmed_fragment(std::string_view said) {
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
[[nodiscard]] inline std::optional<std::string> quoted_fragment(const message& said) {
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
    out += "\u2026";
  }
  return out;
}

// A link's preview, as Telegram's: under the text, a stripe in the accent,
// the site's name in it, the page's title and a few lines about it, and its
// picture on the right.
struct page_preview : nodes::Stack {
  struct column : nodes::Stack {
    struct parts_t {
      nodes::Text site;
      nodes::Text title;
      nodes::Text about;
    } parts;
    column(const palette& colours, const link_preview& shown)
        : parts{.site = nodes::Text(shown.site, 13.0f, colours.accent, true),
                .title = nodes::Text(shown.title, 13.0f, colours.text, true),
                .about = nodes::Text(preview_line(shown.description), 13.0f, colours.text)} {
      this->setGap(1.0f);
      fState.apply({.autoSize = scene::axes::kY, .grow = scene::axes::kX});
      parts.site.setVisible(!shown.site.empty());
      parts.title.setVisible(!shown.title.empty());
      parts.about.setVisible(!shown.description.empty());
      for (nodes::Text* each : {&parts.site, &parts.title})
        each->setElided(true);
      parts.about.setWrapped(true);
      for (nodes::Text* each : {&parts.site, &parts.title, &parts.about})
        each->apply({.fillX = true});
    }
  };
  struct parts_t {
    nodes::Box<> stripe;
    column texts;
    std::optional<nodes::Image<from_avatars>> picture;
  } parts;
  // The link it is the preview of: pressed, it is followed.
  std::string url;
  page_preview(const palette& colours, const link_preview& shown, std::string where)
      : parts{.stripe = nodes::Box<>(colours.accent), .texts = column(colours, shown)}, url(std::move(where)) {
    this->setHorizontal();
    this->setGap(8.0f);
    // Lit under the pointer, as a link is: it is one.
    fState.apply({.fillX = true, .autoSize = scene::axes::kY, .margin = {6.0f, 0.0f, 2.0f, 0.0f},
                  .padding = {4.0f, 6.0f, 4.0f, 0.0f}, .cornerRadius = 4.0f,
                  .background = (colours.accent & 0x00FFFFFFu) | (0x18u << 24),
                  .hoverBackground = (colours.accent & 0x00FFFFFFu) | (0x34u << 24)});
    parts.stripe.apply({.fillY = true, .width = 3.0f, .cornerRadius = 1.5f});
    if (shown.image) {
      parts.picture.emplace(from_avatars{*shown.image});
      parts.picture->apply({.width = 56.0f, .height = 56.0f, .cornerRadius = 6.0f});
    }
  }
};

}  // namespace mux::ui
