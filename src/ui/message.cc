// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:message -- A message: its bubble, its pictures, files, reactions and mentions.
export module mux.ui:message;

import std;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes;
import skiff.widgets;
import mux.core;
import mux.config;
import mux.logic.links;
export import :chat_list;

export namespace mux::ui {

// A link to a room or to a message in one, under the message that has it,
// as a card: a bar in the accent, the room's avatar, its name -- or
// "Message from" it -- over a second line: what the room is, or who said
// the message and a line of it where it is here. A press on it is seen by
// the messages' list, which follows it.
struct link_card : nodes::Stack {
  std::string url;
  struct bar : scene::Node {
    bar() { fState.apply({.width = 3.0f, .height = 36.0f, .cornerRadius = 1.5f, .background = accent_colour}); }
  } line;
  avatar_mark face;
  struct texts_column : nodes::Stack {
    nodes::Text title;
    nodes::Text said;
    texts_column(std::string t, std::string s)
        : title(std::move(t), 13.0f, accent_colour, true), said(std::move(s), 13.0f, dim_colour) {
      this->setGap(1.0f);
      fState.apply({.autoSize = scene::axes::kBoth, .alignSelf = scene::align::kMiddle});
      for (nodes::Text* each : {&title, &said}) {
        each->setElided(true);
        each->setMaxWidth(360.0f);
      }
    }
    void forEachChild(auto&& f) {
      f(title);
      f(said);
    }
  } texts;
  link_card(std::string where, std::string avatar_id, std::string avatar_name, std::string title, std::string said)
      : url(std::move(where)), face(std::move(avatar_id), std::move(avatar_name), 32.0f),
        texts(std::move(title), std::move(said)) {
    this->setHorizontal();
    this->setGap(8.0f);
    fState.apply({.autoSize = scene::axes::kBoth, .margin = {4.0f, 0.0f, 2.0f, 0.0f}});
    face.apply({.alignSelf = scene::align::kMiddle});
    fState.setCursor(scene::cursor::hand{});
  }
  void forEachChild(auto&& f) {
    f(line);
    f(face);
    f(texts);
  }
  [[nodiscard]] bool acceptsInput() const { return true; }
};

// One message, as Telegram Desktop shows it: a rounded bubble, on the right
// and blue for what was sent from here, on the left otherwise; in a group,
// the sender's name in their colour over the first of a run and their
// avatar beside its last; the time in the bubble's corner.
// A picture in a message, as tdesktop sizes one: its own size fitted into
// 430 by 430 (maxMediaSize), no side under 100 (minPhotoSize); rounded, the
// thumbnail drawn when it has come, a plate until then. With no caption,
// the time is on a dark pill over its corner (msgDateImgBg).
struct picture_view : scene::Node {
  std::string source;
  static constexpr float kMax = 430.0f, kMin = 100.0f;
  // Its own proportions: as its message says them, and as its picture has
  // them once it has come -- never another's.
  int width = 0, height = 0;
  // The time on a dark pill over its corner, where there is no caption.
  struct time_pill : nodes::Stack {
    nodes::Text label{"", 11.0f, skia::colorSetARGB(255, 255, 255, 255)};
    time_pill() {
      fState.apply({.place = scene::anchor::kBottomRight,
                    .autoSize = scene::axes::kBoth,
                    .margin = {0.0f, 6.0f, 6.0f, 0.0f},
                    .padding = {2.0f, 8.0f, 2.0f, 8.0f},
                    .cornerRadius = 9.0f,
                    .background = skia::colorSetARGB(0x54, 0, 0, 0)});
      this->setVisible(false);
    }
    void forEachChild(auto&& f) { f(label); }
  };
  struct parts_t {
    nodes::Image picture;
    time_pill time;
  } parts;

  // Rounded; a plate until the thumbnail comes, then the thumbnail covering
  // it, cut at the middle where the proportions differ by a rounding.
  picture_view(std::string where, int w, int h)
      : source(where), width(w), height(h), parts{.picture = nodes::Image([where] { return thumbnails().find(where); })} {
    fState.apply({.cornerRadius = 10.0f, .background = tile_colour, .masking = true});
    parts.picture.apply({.fill = true, .cornerRadius = 10.0f});
  }
  void show_time(std::string when) {
    parts.time.label.setText(when);
    parts.time.setVisible(!when.empty());
  }
  // Fitted into 430 by 430 and into the room there is, its proportions
  // kept; no side under 100 where the room allows.
  void measure(const skia::SkRect& parent) {
    float w = width > 0 ? static_cast<float>(width) : 320.0f;
    float h = height > 0 ? static_cast<float>(height) : 240.0f;
    // The picture's own proportions, where the message said none or others.
    if (const auto ratio = parts.picture.ratio(); ratio && (width <= 0 || height <= 0 || std::abs(w / h - *ratio) > 0.01f))
      h = w / *ratio;
    const float room = parent.width() > 0.0f ? parent.width() : kMax;
    const float scale = std::min({1.0f, kMax / w, kMax / h, room / w});
    w *= scale;
    h *= scale;
    if (w < kMin && h < kMin) {
      const float up = std::min(kMin / std::max(w, h), room / w);
      w *= up;
      h *= up;
    }
    fState.fWidth = std::floor(w);
    fState.fHeight = std::floor(h);
  }
  [[nodiscard]] bool acceptsInput() const { return true; }
};

// A file in a message, as tdesktop's row: a round icon in the accent, the
// name over its size; pressed, it is saved and opened.
struct file_view : nodes::Stack {
  std::string source;
  struct disc : scene::Node {
    disc() { fState.apply({.width = 44.0f, .height = 44.0f, .alignSelf = scene::align::kMiddle}); }
    void drawSelf(skia::SkCanvas* canvas, float alpha) {
      if (skia::SkFont* font = skiff::paint::defaultFont())
        skiff::paint::Painter(canvas, *font).fillRounded(fState.fBounds, 22.0f, accent_colour, alpha);
      draw_icon(canvas, icon::clip{}, fState.fBounds, on_accent_colour, alpha);
    }
  } icon;
  two_lines texts;
  [[nodiscard]] static std::string size_text(std::int64_t bytes) {
    if (bytes <= 0)
      return "File";
    if (bytes < 1024)
      return std::format("{} B", bytes);
    if (bytes < 1024 * 1024)
      return std::format("{:.1f} KB", static_cast<double>(bytes) / 1024.0);
    return std::format("{:.1f} MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
  }
  file_view(std::string where, std::string name, std::int64_t bytes)
      : source(std::move(where)), texts(std::move(name), size_text(bytes), 14.0f, 4.0f) {
    this->setHorizontal();
    this->setGap(11.0f);
    fState.apply({.autoSize = scene::axes::kBoth, .minWidth = 268.0f - 24.0f, .padding = {2.0f, 0.0f, 2.0f, 0.0f}});
    texts.apply({.grow = scene::axes::kNone, .maxWidth = 360.0f});
    texts.name.setMaxWidth(360.0f);
    texts.state.setMaxWidth(360.0f);
    fState.setCursor(scene::cursor::hand{});
  }
  void forEachChild(auto&& f) {
    f(icon);
    f(texts);
  }
  [[nodiscard]] bool acceptsInput() const { return true; }
};

// A message's reactions, as tdesktop's: a chip for each, its emoji and
// how many, the user's own in the accent; a press on one puts or takes back
// the user's.
struct reaction_chip : scene::Node {
  std::string key;
  std::size_t count = 0;
  bool mine = false;
  reaction_chip(std::string k, std::size_t n, bool own) : key(std::move(k)), count(n), mine(own) {
    fState.apply({.height = 26.0f});
  }
  [[nodiscard]] std::string label() const { return std::format("{} {}", key, count); }
  void measure(const skia::SkRect&) {
    if (skia::SkFont* font = skiff::paint::defaultFont())
      fState.fWidth = skiff::paint::Painter(nullptr, *font).measure(this->label(), 13.0f) + 18.0f;
  }
  void drawSelf(skia::SkCanvas* canvas, float alpha) {
    skia::SkFont* font = skiff::paint::defaultFont();
    if (font == nullptr)
      return;
    const skiff::paint::Painter p(canvas, *font);
    p.fillRounded(fState.fBounds, 13.0f, mine ? accent_colour : tile_colour, alpha);
    p.textIn(fState.fBounds, this->label(), 13.0f, mine ? on_accent_colour : text_colour, alpha, false, 9.0f);
  }
  [[nodiscard]] bool acceptsInput() const { return true; }
};
struct reaction_row : nodes::Flow<std::vector<reaction_chip>> {
  reaction_row() : nodes::Flow<std::vector<reaction_chip>>({.direction = nodes::direction::horizontal{}, .spacingX = 4.0f,
                                                             .spacingY = 4.0f},
                                                            {}) {
    fState.apply({.autoSize = scene::axes::kBoth, .maxWidth = 430.0f, .margin = {4.0f, 0.0f, 2.0f, 0.0f}});
  }
  std::vector<reaction_chip>& chips() { return std::get<0>(fChildren); }
  const std::vector<reaction_chip>& chips() const { return std::get<0>(fChildren); }
};

// Mentions, as pills: Matrix IDs in a message's text -- #alias:server,
// !room:server, @user:server -- and its HTML's matrix.to links to them, each
// drawn as a pill with a small avatar and the name it goes by here (a
// room's name, a member's), a link to it. The text is given back with the
// names in place of the IDs, and its links with it.
struct mentioned {
  std::string text;
  std::vector<nodes::Text::Link> links;
  std::vector<std::pair<std::string, logic::link::room>> cards;  // the link, and the room it is of
};
[[nodiscard]] inline mentioned with_mentions(std::string text, std::vector<nodes::Text::Link> links,
                                             const conversation& in, const model* now) {
  // What a mention is called here, and what it links to: a person by their
  // name in the chat, a room by its name where it is known.
  const auto name_of = [&](const logic::link_t& what) -> std::pair<std::string, std::string> {
    return std::visit(overloaded{[&](const logic::link::person& one) { return std::pair(sender_name(in, one.id), one.id); },
                                 [&](const logic::link::room& one) {
                                   if (now)
                                     if (const auto chat = logic::chat_of(*now, what))
                                       if (const conversation* found = now->find(*chat))
                                         return std::pair(display_name(*found), found->id.id);
                                   return std::pair(one.id, one.id);
                                 },
                                 [](const logic::link::xmpp_address& one) { return std::pair(one.jid, one.jid); }},
                      what);
  };
  // A word shaped as a Matrix ID: a sigil, a name, a colon, a server.
  const auto id_in = [](std::string_view word) -> std::optional<logic::link_t> {
    const auto colon = word.find(':');
    if (word.size() < 4 || colon == std::string_view::npos || colon < 2 || colon + 1 >= word.size())
      return std::nullopt;
    return logic::matrix_id_of(std::string(word));
  };
  // What in the text is replaced: by a pill, or by nothing where it is
  // shown as a card instead.
  struct replaced {
    std::size_t first, last;
    std::optional<logic::link_t> pill;
  };
  std::vector<replaced> spans;
  std::vector<nodes::Text::Link> kept;
  mentioned out;
  for (auto& link : links) {
    const auto what = logic::link_of(link.target);
    const std::string_view label = std::string_view(text).substr(link.first, link.last - link.first);
    const bool bare = label == link.target;  // the URL itself, not words over it
    if (!what) {
      kept.push_back(std::move(link));
      continue;
    }
    // A person: a pill. A room named by words over it: a pill; given as
    // its URL, or a message in it: a card, the URL out of the text. An
    // XMPP address: a link as it is.
    std::visit(overloaded{[&](const logic::link::person&) { spans.push_back({link.first, link.last, what}); },
                          [&](const logic::link::room& one) {
                            if (bare || one.event) {
                              spans.push_back({link.first, link.last, std::nullopt});
                              out.cards.emplace_back(link.target, one);
                            } else {
                              spans.push_back({link.first, link.last, what});
                            }
                          },
                          [&](const logic::link::xmpp_address&) { kept.push_back(link); }},
               *what);
  }
  for (std::size_t at = 0; at < text.size();) {
    const bool starts = at == 0 || std::string_view(" \n\t(").contains(text[at - 1]);
    if (starts) {
      std::size_t end = at;
      while (end < text.size() && !std::string_view(" \n\t,;)").contains(text[end]))
        ++end;
      while (end > at && std::string_view(".!?").contains(text[end - 1]))
        --end;
      const bool inside = std::ranges::any_of(spans, [&](const replaced& p) { return at < p.last && end > p.first; }) ||
                          std::ranges::any_of(kept, [&](const auto& l) { return at < l.last && end > l.first; });
      if (end > at && !inside)
        if (auto what = id_in(std::string_view(text).substr(at, end - at))) {
          spans.push_back({at, end, std::move(what)});
          at = end;
          continue;
        }
    }
    ++at;
  }
  // Replaced from the end, so what comes before keeps its place; links
  // after a replaced stretch move with it.
  std::ranges::sort(spans, std::ranges::greater{}, &replaced::first);
  for (const replaced& span : spans) {
    std::string shown;
    std::optional<nodes::Text::Link> pill;
    if (span.pill) {
      const auto [name, target] = name_of(*span.pill);
      shown = "\u2002\u2002" + name;  // room for its avatar
      pill = nodes::Text::Link{span.first, span.first + shown.size(), "https://matrix.to/#/" + target, true};
    }
    const std::ptrdiff_t grew =
        static_cast<std::ptrdiff_t>(shown.size()) - static_cast<std::ptrdiff_t>(span.last - span.first);
    text.replace(span.first, span.last - span.first, shown);
    for (auto* list : {&kept, &out.links})
      for (auto& link : *list)
        if (link.first >= span.last) {
          link.first = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(link.first) + grew);
          link.last = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(link.last) + grew);
        }
    if (pill)
      out.links.push_back(std::move(*pill));
  }
  for (auto& link : kept)
    out.links.push_back(std::move(link));
  // What is left of the text: without the space a card's link stood in.
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
    text.pop_back();
  out.text = std::move(text);
  return out;
}

// A card for a link to a room, or to a message in one: as the chat it is of
// is known here, or as a room not joined.
[[nodiscard]] inline link_card card_of(const std::string& url, const logic::link::room& room, const model* now) {
  const conversation* chat = nullptr;
  if (now)
    if (const auto found = logic::chat_of(*now, room))
      chat = now->find(*found);
  const std::string name = chat ? display_name(*chat) : room.id;
  const std::string id = chat ? chat->id.id : room.id;
  if (!room.event) {
    const std::string what =
        chat ? (chat->member_count > 0 ? std::format("Room · {} members", chat->member_count) : std::string("Room"))
             : std::string("Room · not joined");
    return link_card(url, id, name, name, what);
  }
  std::string said = "A message";
  if (chat)
    if (const auto it = std::ranges::find(chat->timeline, *room.event, &message::id); it != chat->timeline.end()) {
      said = (it->outgoing ? std::string("You") : sender_name(*chat, it->sender)) + ": " + it->body.plain;
      std::ranges::replace(said, '\n', ' ');
    }
  return link_card(url, id, name, "Message from " + name, said);
}

struct message_bubble : nodes::Stack {
  // The message as it was shown, and where in its sender's run: while
  // these are the same, the bubble is kept.
  message said;
  bool first = false, last = false;
  // The message: its id and text, for its menu.
  std::string message_id;
  std::string plain;
  bool outgoing = false;
  std::string sender;

  static constexpr float kPadX = 12.0f;
  static constexpr float kPadY = 7.0f;
  static constexpr float kAvatar = 34.0f;
  static constexpr float kMaxWidth = 480.0f;

  // The sender's avatar, beside the last of their run in a group; the
  // same room, empty, beside the rest.
  avatar_mark face;
  // What it answers: a bar in the sender's colour, beside who and a line.
  struct quote_row : nodes::Stack {
    struct bar : scene::Node {
      skia::SkColor colour;
      explicit bar(skia::SkColor c) : colour(c) { fState.apply({.width = 3.0f, .height = 32.0f, .cornerRadius = 1.5f, .background = c}); }
    } line;
    // Who said it over a line of it, each cut at the bubble's width.
    struct said_column : nodes::Stack {
      nodes::Text who;
      nodes::Text said;
      said_column(skia::SkColor colour, std::string name, std::string line)
          : who(std::move(name), 13.0f, colour, true), said(std::move(line), 13.0f, dim_colour) {
        this->setGap(1.0f);
        fState.apply({.autoSize = scene::axes::kBoth, .alignSelf = scene::align::kMiddle});
        for (nodes::Text* each : {&who, &said}) {
          each->setElided(true);
          each->setMaxWidth(kMaxWidth - 10.0f);
        }
      }
      void forEachChild(auto&& f) {
        f(who);
        f(said);
      }
    } texts;
    quote_row(skia::SkColor colour, std::string who, std::string said)
        : line(colour), texts(colour, std::move(who), std::move(said)) {
      this->setHorizontal();
      this->setGap(7.0f);
      fState.apply({.autoSize = scene::axes::kBoth, .margin = {2.0f, 0.0f, 4.0f, 0.0f}});
    }
    void forEachChild(auto&& f) {
      f(line);
      f(texts);
    }
  };
  // The bubble: as wide as what it says, up to its largest.
  struct body_column : nodes::Stack {
    bool outgoing = false;
    std::optional<nodes::Text> name;
    std::optional<quote_row> quote;
    std::optional<picture_view> picture;
    std::optional<file_view> file;
    nodes::Text text;
    std::vector<link_card> cards;
    std::optional<reaction_row> reactions;
    nodes::Text time;
    // A flash over it, fading, where it was jumped to.
    skiff::paint::Tween flash{0.0f, 1200.0f};
    [[nodiscard]] bool settling() const { return flash.moving(); }
    // The time goes in the last line of the text where that line leaves room
    // for it, as Telegram's does; on a line of its own where it does not.
    // Decided from the last layout; a change is laid out at the next.
    void update(double now_ms) {
      if (flash.step(now_ms))
        this->markDamaged();
      if (!text.visible() || text.bounds().isEmpty() || !cards.empty() || reactions)
        return;
      skia::SkFont* font = skiff::paint::defaultFont();
      if (font == nullptr)
        return;
      const float room = fState.contentBox().width();
      const float needs = text.lastLineWidth() + skiff::paint::Painter(nullptr, *font).measure(time.text(), 11.0f) + 10.0f;
      const bool inline_time = needs <= room;
      if (inline_time == time.visible())
        time.setVisible(!inline_time);
    }
    void draw(skia::SkCanvas* canvas, float alpha) {
      scene::drawDefault(*this, canvas, alpha);
      if (!time.visible())
        if (skia::SkFont* font = skiff::paint::defaultFont()) {
          const skiff::paint::Painter p(canvas, *font);
          const skia::SkRect inside = fState.contentBox();
          const float width = p.measure(time.text(), 11.0f);
          p.text(time.text(), inside.fRight - width, text.bounds().fBottom - 3.0f, 11.0f, time.colour(), alpha);
        }
      if (flash.value() > 0.0f)
        if (skia::SkFont* font = skiff::paint::defaultFont())
          skiff::paint::Painter(canvas, *font)
              .fillRounded(fState.fBounds, 12.0f, (accent_colour & 0x00FFFFFFu) | (90u << 24), alpha * flash.value());
    }
    body_column(bool mine, std::string said, std::string when)
        : outgoing(mine), text(std::move(said), 13.0f, text_colour), time(std::move(when), 11.0f,
                                                                           mine ? sent_time_colour : dim_colour) {
      this->setGap(2.0f);
      fState.apply({.autoSize = scene::axes::kBoth, .maxWidth = kMaxWidth + 2.0f * kPadX,
                    .padding = {kPadY, kPadX, 5.0f, kPadX}, .cornerRadius = 12.0f, .background = mine ? out_bubble_colour : bubble_colour});
      text.setWrapped(true);
      text.setShrinksToLines(true);
      time.apply({.alignSelf = scene::align::kEnd});
    }
    void forEachChild(auto&& f) {
      f(name);
      f(quote);
      f(picture);
      f(file);
      f(text);
      f(cards);
      f(reactions);
      f(time);
    }
  } body;

  // Declared: the avatar's room and the bubble, at the right where it is
  // one's own; the bubble a column of the name, the quote, the text, the
  // links, the reactions and the time.
  message_bubble(const conversation& in, const message& said, bool first_of_run, bool last_of_run,
                 const model* now = nullptr)
      : said(said), first(first_of_run), last(last_of_run), message_id(said.id), plain(said.body.plain),
        outgoing(said.outgoing), sender(said.sender), face(said.sender, sender_name(in, said.sender), kAvatar),
        body(said.outgoing, said.redacted ? std::string("(removed)") : said.body.plain + (said.edited ? " (edited)" : ""),
             clock_of(said.at)) {
    this->setHorizontal();
    this->setGap(8.0f);
    // As tdesktop: a sender's messages one under the other nearly touch;
    // where the sender changes, a gap.
    fState.apply({.fillX = true, .autoSize = scene::axes::kY,
                  .padding = {first_of_run ? 8.0f : 1.0f, 0.0f, 1.0f, 0.0f}});
    if (outgoing)
      fStack.justify = nodes::justify::end{};
    const bool group = is_group(in);
    face.setVisible(group && !outgoing);
    face.apply({.alignSelf = scene::align::kEnd});
    if (!(group && !outgoing && last_of_run))
      face.fState.setAlpha(0.0f);  // its room kept, so the run's bubbles line up
    if (group && !outgoing && first_of_run) {
      body.name.emplace(sender_name(in, said.sender), 13.0f, avatar_colour(said.sender), true);
      body.name->setElided(true);
      body.name->setMaxWidth(kMaxWidth);
    }
    // Anyone's words can be selected and copied, as in Telegram.
    body.text.setSelectable(true);
    body.text.setSelectionColour((accent_colour & 0x00FFFFFFu) | (110u << 24));  // the accent, see-through
    std::string when = clock_of(said.at);
    when += std::visit(overloaded{[](const delivery::sending&) { return " · sending"; },
                                  [](const delivery::failed&) { return " · not sent"; },
                                  [](const auto&) { return ""; }},
                       said.delivery);
    body.time.setText(when);
    // What it carries: a picture, sized as tdesktop's; or a file's row.
    if (said.attachment && !said.redacted) {
      const mux::attachment& carried = *said.attachment;
      std::visit(overloaded{[&](attachment_kind::image) {
                              body.picture.emplace(carried.source, carried.width, carried.height);
                            },
                            [&](attachment_kind::file) { body.file.emplace(carried.source, carried.name, carried.size); }},
                 carried.kind);
      // No caption: the text goes, and a picture has its time over it.
      if (said.body.plain.empty() && !said.body.html) {
        body.text.setVisible(false);
        if (body.picture) {
          body.picture->show_time(when);
          body.time.setVisible(false);
          body.apply({.padding = {3.0f, 3.0f, 3.0f, 3.0f}});
        }
      }
    }
    // Formatted, it is drawn from its HTML: its text, and its links; plain,
    // its links are the URLs in it.
    // Its links in its text, where they stand: an <a>'s label going where
    // its href says, and the addresses in a plain text.
    mentioned shown;
    if (said.body.html && !said.redacted) {
      auto read = read_html(*said.body.html);
      shown = with_mentions(std::move(read.text), std::move(read.spans), in, now);
    } else if (!said.redacted) {
      shown = with_mentions(said.body.plain, link_spans_in(said.body.plain), in, now);
    }
    if (!said.redacted) {
      body.text.setText(shown.text + (said.edited ? " (edited)" : ""));
      body.text.setLinks(std::move(shown.links), accent_colour);
      body.text.setVisible(!shown.text.empty() || said.edited);
      for (const auto& [url, room] : shown.cards)
        body.cards.push_back(card_of(url, room, now));
    }
    if (said.replies_to) {
      const auto found = std::ranges::find(in.timeline, *said.replies_to, &message::id);
      const bool known = found != in.timeline.end();
      std::string line = known ? found->body.plain : std::string("not loaded");
      std::ranges::replace(line, '\n', ' ');
      body.quote.emplace(known ? avatar_colour(found->sender) : accent_colour,
                         known ? (found->outgoing ? std::string("You") : sender_name(in, found->sender))
                               : std::string("A message"),
                         std::move(line));
      body.apply({.minWidth = 160.0f});
    }
    if (!said.reactions.empty()) {
      body.reactions.emplace();
      for (const auto& [key, who] : said.reactions)
        if (!who.empty())
          body.reactions->chips().emplace_back(key, who.size(), who.contains(said.in.account.address));
    }
  }

  void forEachChild(auto&& f) {
    f(face);
    f(body);
  }

  // Swiped to the left to answer it: how far, following the pointer, and
  // back to its place when let go. Drawn moved, with the arrow of a reply
  // coming in at the right; where it is laid out does not change.
  skiff::paint::Tween swipe{0.0f, 180.0f, skiff::paint::movement::subtle{}};
  static constexpr float kSwipeToReply = 70.0f;
  [[nodiscard]] bool settling() const { return swipe.moving(); }
  void update(double now_ms) {
    if (swipe.step(now_ms))
      this->markDamaged();
  }
  void draw(skia::SkCanvas* canvas, float alpha) {
    const float shift = swipe.value();
    if (shift == 0.0f) {
      scene::drawDefault(*this, canvas, alpha);
      return;
    }
    const int saved = canvas->save();
    canvas->translate(shift, 0.0f);
    scene::drawDefault(*this, canvas, alpha);
    canvas->restoreToCount(saved);
    const float reached = std::clamp(-shift / kSwipeToReply, 0.0f, 1.0f);
    const skia::SkRect& box = fState.fBounds;
    const skia::SkRect disc = skia::SkRect::MakeXYWH(box.fRight - 34.0f, box.centerY() - 14.0f, 28.0f, 28.0f);
    if (skia::SkFont* font = skiff::paint::defaultFont())
      skiff::paint::Painter(canvas, *font).fillRounded(disc, 14.0f, reached >= 1.0f ? accent_colour : tile_colour,
                                                       alpha * reached);
    draw_icon(canvas, icon::back{}, disc, reached >= 1.0f ? on_accent_colour : dim_colour, alpha * reached);
  }
  // Pressed with the right button, it asks for its menu.
  [[nodiscard]] bool acceptsInput() const { return true; }
};

}  // namespace mux::ui
