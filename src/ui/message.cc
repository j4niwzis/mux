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
export import :chat_list;

export namespace mux::ui {

// A link under a message: what it says, in the accent. A press on it is
// seen by the messages' list, which opens it.
struct link_line : nodes::Stack {
  std::string url;
  nodes::Text label;
  link_line(std::string said, std::string where) : url(std::move(where)), label("🔗 " + std::move(said), 13.0f, accent_colour) {
    // As wide as it says, up to a bubble's width: cut there.
    fState.apply({.height = 20.0f, .autoSize = scene::axes::kX});
    label.setElided(true);
    label.setMaxWidth(480.0f);
    fState.setCursor(scene::cursor::hand{});
  }
  void forEachChild(auto&& f) { f(label); }
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
  std::string time;  // drawn over it where there is no caption
  static constexpr float kMax = 430.0f, kMin = 100.0f;
  // Its own proportions: as its message says them, and as its picture has
  // them once it has come -- never another's.
  int width = 0, height = 0;
  bool had_picture = false;
  void update(double) {
    if (!had_picture && avatar_images().has("thumb:" + source)) {
      had_picture = true;
      this->invalidateLayout();  // measured again, by the picture's proportions
    }
  }
  picture_view(std::string where, int w, int h) : source(std::move(where)), width(w), height(h) {}
  // Fitted into 430 by 430 and into the room there is, its proportions
  // kept; no side under 100 where the room allows.
  void measure(const skia::SkRect& parent) {
    float w = width > 0 ? static_cast<float>(width) : 320.0f;
    float h = height > 0 ? static_cast<float>(height) : 240.0f;
    if (const skia::Sp<skia::SkImage>* image = avatar_images().find("thumb:" + source); image && *image) {
      // The picture's own proportions, where the message said none or others.
      const float ratio = static_cast<float>((*image)->width()) / static_cast<float>((*image)->height());
      if (width <= 0 || height <= 0 || std::abs(w / h - ratio) > 0.01f)
        h = w / ratio;
    }
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
  void drawSelf(skia::SkCanvas* canvas, float alpha) {
    const skia::SkRect& box = fState.fBounds;
    const int saved = canvas->save();
    canvas->clipRRect(skia::SkRRect::MakeRectXY(box, 10.0f, 10.0f), true);
    if (const skia::Sp<skia::SkImage>* image = avatar_images().find("thumb:" + source); image && *image) {
      // Covering the box, cut at the middle where the proportions differ
      // by a rounding: never stretched.
      const float iw = static_cast<float>((*image)->width()), ih = static_cast<float>((*image)->height());
      const float scale = std::max(box.width() / iw, box.height() / ih);
      const float sw = box.width() / scale, sh = box.height() / scale;
      const skia::SkRect from = skia::SkRect::MakeXYWH((iw - sw) * 0.5f, (ih - sh) * 0.5f, sw, sh);
      skia::SkPaint paint;
      paint.setAlphaf(alpha);
      canvas->drawImageRect(*image, from, box, skia::SkSamplingOptions(skia::SkFilterMode::kLinear), &paint,
                            skia::SkCanvas::kFast_SrcRectConstraint);
    } else if (skia::SkFont* font = skiff::paint::defaultFont()) {
      skiff::paint::Painter(canvas, *font).fillRounded(box, 10.0f, tile_colour, alpha);
    }
    canvas->restoreToCount(saved);
    if (!time.empty())
      if (skia::SkFont* font = skiff::paint::defaultFont()) {
        const skiff::paint::Painter p(canvas, *font);
        const float width = p.measure(time, 11.0f) + 16.0f;
        const skia::SkRect pill = skia::SkRect::MakeXYWH(box.fRight - width - 6.0f, box.fBottom - 24.0f, width, 18.0f);
        p.fillRounded(pill, 9.0f, skia::colorSetARGB(0x54, 0, 0, 0), alpha);
        p.textIn(pill, time, 11.0f, skia::colorSetARGB(255, 255, 255, 255), alpha, false, 8.0f);
      }
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
[[nodiscard]] inline std::pair<std::string, std::vector<nodes::Text::Link>> with_mentions(
    std::string text, std::vector<nodes::Text::Link> links, const conversation& in, const model* now) {
  const auto name_of = [&](const std::string& id) -> std::pair<std::string, std::string> {  // name, id to link to
    if (id.starts_with('@'))
      return {sender_name(in, id), id};
    if (now)
      for (const auto& [account, one] : now->accounts())
        for (const auto& [key, chat] : one.conversations)
          if (chat.id.id == id || (chat.alias && *chat.alias == id))
            return {display_name(chat), chat.id.id};
    return {id, id};
  };
  const auto is_id = [](std::string_view word) {
    if (word.size() < 4 || !std::string_view("@#!").contains(word.front()))
      return false;
    const auto colon = word.find(':');
    return colon != std::string_view::npos && colon > 1 && colon + 1 < word.size();
  };
  // Where the pills go: the HTML's links to an ID, and the IDs standing in
  // the text on their own.
  struct pill_at {
    std::size_t first, last;
    std::string id;
  };
  std::vector<pill_at> pills;
  std::vector<nodes::Text::Link> kept;
  for (auto& link : links) {
    constexpr std::string_view to = "https://matrix.to/#/";
    if (link.target.starts_with(to)) {
      std::string id = link.target.substr(to.size());
      id = id.substr(0, id.find_first_of("/?"));
      if (is_id(id)) {
        pills.push_back({link.first, link.last, id});
        continue;
      }
    }
    kept.push_back(std::move(link));
  }
  for (std::size_t at = 0; at < text.size();) {
    const bool starts = at == 0 || std::string_view(" \n\t(").contains(text[at - 1]);
    if (starts && std::string_view("@#!").contains(text[at])) {
      std::size_t end = at + 1;
      while (end < text.size() && !std::string_view(" \n\t,;)").contains(text[end]))
        ++end;
      while (end > at && std::string_view(".!?").contains(text[end - 1]))
        --end;
      const std::string_view word = std::string_view(text).substr(at, end - at);
      const bool inside = std::ranges::any_of(pills, [&](const pill_at& p) { return at < p.last && end > p.first; }) ||
                          std::ranges::any_of(kept, [&](const auto& l) { return at < l.last && end > l.first; });
      if (is_id(word) && !inside) {
        pills.push_back({at, end, std::string(word)});
        at = end;
        continue;
      }
    }
    ++at;
  }
  // Replaced from the end, so what comes before keeps its place; links
  // after a replaced stretch move with it.
  std::ranges::sort(pills, std::ranges::greater{}, &pill_at::first);
  std::vector<nodes::Text::Link> out;
  for (const pill_at& pill : pills) {
    const auto [name, target] = name_of(pill.id);
    const std::string shown = "\u2002\u2002" + name;  // room for its avatar
    const std::ptrdiff_t grew =
        static_cast<std::ptrdiff_t>(shown.size()) - static_cast<std::ptrdiff_t>(pill.last - pill.first);
    text.replace(pill.first, pill.last - pill.first, shown);
    for (auto* list : {&kept, &out})
      for (auto& link : *list)
        if (link.first >= pill.last) {
          link.first = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(link.first) + grew);
          link.last = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(link.last) + grew);
        }
    out.push_back({pill.first, pill.first + shown.size(), "https://matrix.to/#/" + target, true});
  }
  for (auto& link : kept)
    out.push_back(std::move(link));
  return {std::move(text), std::move(out)};
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
      explicit bar(skia::SkColor c) : colour(c) { fState.apply({.width = 3.0f, .height = 32.0f}); }
      void drawSelf(skia::SkCanvas* canvas, float alpha) {
        if (skia::SkFont* font = skiff::paint::defaultFont())
          skiff::paint::Painter(canvas, *font).fillRounded(fState.fBounds, 1.5f, colour, alpha);
      }
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
    std::vector<link_line> links;
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
      if (!text.visible() || text.bounds().isEmpty() || links.size() > 0 || reactions)
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
                    .padding = {kPadY, kPadX, 5.0f, kPadX}});
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
      f(links);
      f(reactions);
      f(time);
    }
    void drawSelf(skia::SkCanvas* canvas, float alpha) {
      if (skia::SkFont* font = skiff::paint::defaultFont())
        skiff::paint::Painter(canvas, *font)
            .fillRounded(fState.fBounds, 12.0f, outgoing ? out_bubble_colour : bubble_colour, alpha);
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
          body.picture->time = when;
          body.time.setVisible(false);
          body.apply({.padding = {3.0f, 3.0f, 3.0f, 3.0f}});
        }
      }
    }
    // Formatted, it is drawn from its HTML: its text, and its links; plain,
    // its links are the URLs in it.
    // Its links in its text, where they stand: an <a>'s label going where
    // its href says, and the addresses in a plain text.
    if (said.body.html && !said.redacted) {
      auto read = read_html(*said.body.html);
      auto [text, links] = with_mentions(std::move(read.text), std::move(read.spans), in, now);
      body.text.setText(text + (said.edited ? " (edited)" : ""));
      body.text.setLinks(std::move(links), accent_colour);
    } else if (!said.redacted) {
      auto [text, links] = with_mentions(said.body.plain, link_spans_in(said.body.plain), in, now);
      body.text.setText(text + (said.edited ? " (edited)" : ""));
      body.text.setLinks(std::move(links), accent_colour);
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
