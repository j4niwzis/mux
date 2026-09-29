// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:message -- A message: its bubble, its pictures, files, reactions and mentions.
export module mux.ui:message;

import std;
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
import mux.audio;
import mux.core;
import mux.config;
import mux.logic.links;
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
    bar() { fState.apply({.width = 3.0f, .height = 36.0f, .cornerRadius = 1.5f, .background = accent_colour}); }
  };
  struct texts_column : nodes::Stack {
    struct parts_t {
      nodes::Text title;
      nodes::Text said;
    } parts;
    texts_column(std::string t, std::string s)
        : parts{.title = nodes::Text(std::move(t), 13.0f, accent_colour, true),
                .said = nodes::Text(std::move(s), 13.0f, dim_colour)} {
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
  link_card(std::string where, std::string avatar_id, std::string avatar_name, std::string title, std::string said)
      : url(std::move(where)),
        parts{.face = avatar_mark(std::move(avatar_id), std::move(avatar_name), 32.0f),
              .texts = texts_column(std::move(title), std::move(said))} {
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
        !span.target.starts_with("https://matrix.to/"))
      return span.target;
  return std::nullopt;
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
    explicit column(const link_preview& shown)
        : parts{.site = nodes::Text(shown.site, 13.0f, accent_colour, true),
                .title = nodes::Text(shown.title, 13.0f, text_colour, true),
                .about = nodes::Text(preview_line(shown.description), 13.0f, text_colour)} {
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
    nodes::Box<> stripe{accent_colour};
    column texts;
    std::optional<nodes::Image<from_avatars>> picture;
  } parts;
  // The link it is the preview of: pressed, it is followed.
  std::string url;
  page_preview(const link_preview& shown, std::string where) : parts{.texts = column(shown)}, url(std::move(where)) {
    this->setHorizontal();
    this->setGap(8.0f);
    // Lit under the pointer, as a link is: it is one.
    fState.apply({.fillX = true, .autoSize = scene::axes::kY, .margin = {6.0f, 0.0f, 2.0f, 0.0f},
                  .padding = {4.0f, 6.0f, 4.0f, 0.0f}, .cornerRadius = 4.0f,
                  .background = (accent_colour & 0x00FFFFFFu) | (0x18u << 24),
                  .hoverBackground = (accent_colour & 0x00FFFFFFu) | (0x34u << 24)});
    parts.stripe.apply({.fillY = true, .width = 3.0f, .cornerRadius = 1.5f});
    if (shown.image) {
      parts.picture.emplace(from_avatars{*shown.image});
      parts.picture->apply({.width = 56.0f, .height = 56.0f, .cornerRadius = 6.0f});
    }
  }
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
    struct parts_t {
      nodes::Text label{"", 11.0f, skia::colorSetARGB(255, 255, 255, 255)};
    } parts;
    time_pill() {
      fState.apply({.place = scene::anchor::kBottomRight,
                    .autoSize = scene::axes::kBoth,
                    .margin = {0.0f, 6.0f, 6.0f, 0.0f},
                    .padding = {2.0f, 8.0f, 2.0f, 8.0f},
                    .cornerRadius = 9.0f,
                    .background = skia::colorSetARGB(0x54, 0, 0, 0)});
      this->setVisible(false);
    }
  };
  struct parts_t {
    nodes::Image<from_previews> preview;  // blurred, from its blurhash, until it comes
    nodes::Image<from_moving_thumbnail> picture;
    widgets::RadialLoader loader{};  // while it is coming
    time_pill time{};
  } parts;

  // Rounded; a plate until the thumbnail comes, then the thumbnail covering
  // it, cut at the middle where the proportions differ by a rounding.
  picture_view(std::string where, int w, int h)
      : source(where), width(w), height(h),
        parts{.preview = nodes::Image<from_previews>({where}),
              .picture = nodes::Image<from_moving_thumbnail>({where})} {
    fState.apply({.cornerRadius = 10.0f, .background = tile_colour, .masking = true});
    parts.preview.apply({.fill = true, .cornerRadius = 10.0f});
    parts.picture.apply({.fill = true, .cornerRadius = 10.0f});
    parts.loader.apply({.place = scene::anchor::kCentre});
  }
  // The loader while the picture has not come; where it moves, drawn again
  // each frame for the next of its frames.
  [[nodiscard]] bool settling() const { return animations().has(source); }
  void update(double) {
    const bool moving = animations().has(source);
    const bool coming = !moving && !thumbnails().has(source) && !whole_pictures().has(source);
    if (coming != parts.loader.visible())
      parts.loader.setVisible(coming);
    if (moving)
      parts.picture.markDamaged();
  }
  void show_time(std::string when) {
    parts.time.parts.label.setText(when);
    parts.time.setVisible(!when.empty());
  }
  // Fitted into 430 by 430 and into the room there is, its proportions
  // kept; no side under 100 where the room allows.
  // A cell of an album: its size given, the picture cut to fill it.
  float cell_w = 0.0f, cell_h = 0.0f;
  void set_cell(float w, float h) {
    cell_w = w;
    cell_h = h;
    this->invalidateLayout();
  }
  void measure(const skia::SkRect& parent) {
    if (cell_w > 0.0f) {
      fState.fWidth = cell_w;
      fState.fHeight = cell_h;
      return;
    }
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

// Several pictures in one message, as tdesktop shows an album: rows filling
// its width with a thin gap between -- where they are odd, the first alone
// and wide, then pairs -- each picture cut to fill its cell.
struct album_view : nodes::Stack {
  struct row : nodes::Stack {
    struct parts_t {
      std::vector<picture_view> cells;
    } parts;
    row() {
      this->setHorizontal();
      this->setGap(2.0f);
      fState.apply({.autoSize = scene::axes::kBoth});
    }
  };
  struct parts_t {
    std::vector<row> rows;
  } parts;
  static constexpr float kWidth = 360.0f, kGap = 2.0f;
  explicit album_view(const std::vector<attachment>& items) {
    this->setGap(kGap);
    fState.apply({.autoSize = scene::axes::kBoth});
    std::vector<std::size_t> per_row;
    std::size_t placed = items.size() % 2 == 1 ? 1 : 0;
    if (placed == 1)
      per_row.push_back(1);
    while (placed < items.size()) {
      per_row.push_back(std::min<std::size_t>(2, items.size() - placed));
      placed += per_row.back();
    }
    parts.rows.reserve(per_row.size());
    std::size_t at = 0;
    for (const std::size_t count : per_row) {
      auto& made = parts.rows.emplace_back();
      const float w = (kWidth - kGap * static_cast<float>(count - 1)) / static_cast<float>(count);
      for (std::size_t i = 0; i < count; ++i, ++at) {
        const attachment& item = items[at];
        const float ratio = item.width > 0 && item.height > 0
                                ? static_cast<float>(item.height) / static_cast<float>(item.width)
                                : 0.75f;
        const float h = count == 1 ? std::clamp(w * ratio, 120.0f, 300.0f) : std::clamp(w * 0.8f, 100.0f, 220.0f);
        made.parts.cells.emplace_back(item.source, item.width, item.height);
        made.parts.cells.back().set_cell(std::floor(w), std::floor(h));
      }
    }
  }
};


// A file in a message, as tdesktop's row: a round icon in the accent, the
// name over its size; pressed, it is saved and opened.
struct file_view : nodes::Stack {
  std::string source;
  // tdesktop's msgFileSize: the icon, and so the row, is this high.
  static constexpr float kIcon = 44.0f;
  struct disc : nodes::Icon {
    disc() : nodes::Icon(shape_of(icon::clip{}), on_accent_colour) {
      fState.apply({.width = kIcon, .height = kIcon, .alignSelf = scene::align::kMiddle, .cornerRadius = kIcon / 2.0f,
                    .background = accent_colour});
    }
  };
  // Its name over its size, each as wide as it reads, up to a limit: sized
  // by what they say, so that the bubble is (a block that only grew took
  // nothing in a bubble sized by its content, and showed neither).
  struct texts_column : nodes::Stack {
    struct parts_t {
      nodes::Text name;
      nodes::Text size;
    } parts;
    texts_column(std::string name, std::string size)
        : parts{.name = nodes::Text(std::move(name), 14.0f, text_colour, true),
                .size = nodes::Text(std::move(size), 12.0f, dim_colour)} {
      this->setGap(4.0f);
      fState.apply({.autoSize = scene::axes::kBoth, .alignSelf = scene::align::kMiddle});
      for (nodes::Text* each : {&parts.name, &parts.size}) {
        each->setElided(true);
        each->setMaxWidth(360.0f);
      }
    }
  };
  struct parts_t {
    disc icon;
    texts_column texts;
  } parts;
  // Sound: the disc a play button, the size the time, as Telegram's voice
  // messages have them; as the speaker plays it, drawn each frame.
  bool sound = false;
  bool shown_playing = false;
  std::string shown_time;
  std::string size_line;
  [[nodiscard]] bool settling() const { return sound && mux::audio::the_speaker().holds(source); }
  void update(double) {
    if (!sound)
      return;
    auto& speaker = mux::audio::the_speaker();
    speaker.tick();
    const bool playing = speaker.playing(source);
    if (playing != shown_playing) {
      shown_playing = playing;
      parts.icon.setShape(shape_of(playing ? icon_t{icon::pause{}} : icon_t{icon::play{}}));
    }
    const std::string time = speaker.holds(source)
                                 ? std::format("{} / {}", mux::audio::clock(speaker.position()),
                                               mux::audio::clock(speaker.length()))
                                 : size_line;
    if (time != shown_time) {
      shown_time = time;
      parts.texts.parts.size.setText(time);
    }
  }
  [[nodiscard]] static std::string size_text(std::int64_t bytes) {
    if (bytes <= 0)
      return "File";
    if (bytes < 1024)
      return std::format("{} B", bytes);
    if (bytes < 1024 * 1024)
      return std::format("{:.1f} KB", static_cast<double>(bytes) / 1024.0);
    return std::format("{:.1f} MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
  }
  file_view(std::string where, std::string name, std::int64_t bytes, bool is_sound = false)
      : source(std::move(where)), parts{.texts = texts_column(name, size_text(bytes))}, sound(is_sound),
        size_line(size_text(bytes)) {
    if (sound)
      parts.icon.setShape(shape_of(icon::play{}));
    this->setHorizontal();
    this->setGap(11.0f);
    fState.apply({.autoSize = scene::axes::kBoth, .minWidth = 268.0f - 24.0f, .padding = {2.0f, 0.0f, 2.0f, 0.0f}});
    fState.setCursor(scene::cursor::hand{});
  }
  [[nodiscard]] bool acceptsInput() const { return true; }
};

// A message's reactions, as tdesktop's: a chip for each, its emoji and
// how many, the user's own in the accent; a press on one puts or takes back
// the user's.
//
// A reaction is whatever text a client put in it: an emoji mostly; an
// mxc:// URL where it is a picture -- a custom emoji, shown as the picture,
// fetched as an avatar is -- and words where it is words, cut to a chip's
// length.
struct reaction_chip : nodes::Stack {
  std::string key;
  std::size_t count = 0;
  bool mine = false;
  struct parts_t {
    std::optional<nodes::Image<from_avatars>> picture;
    nodes::Text label;
    // Who reacted, as Telegram shows them: their avatars in place of the
    // count, where they are three or fewer.
    std::vector<avatar_mark> who;
  } parts;
  [[nodiscard]] static bool pictured(std::string_view k) { return k.starts_with("mxc://"); }
  static constexpr std::size_t kFacesShown = 3;
  // What the chip says: the count beside a picture; else the reaction, cut
  // at a character's edge past 20 bytes, and the count.
  [[nodiscard]] static std::string label_of(std::string_view k, std::size_t n) {
    if (pictured(k))
      return std::to_string(n);
    constexpr std::size_t kLong = 20;
    if (k.size() <= kLong)
      return std::format("{} {}", k, n);
    std::size_t cut = kLong;
    while (cut > 0 && (static_cast<unsigned char>(k[cut]) & 0xC0) == 0x80)
      --cut;
    return std::format("{}… {}", k.substr(0, cut), n);
  }
  // `people`: who reacted, by id and name.
  reaction_chip(std::string k, std::size_t n, bool own,
                const std::vector<std::pair<std::string, std::string>>& people = {})
      : key(std::move(k)), count(n), mine(own),
        parts{.label = nodes::Text(label_of(key, n), 13.0f, own ? on_accent_colour : text_colour)} {
    this->setHorizontal();
    this->setGap(4.0f);
    fStack.justify = nodes::justify::middle{};
    fState.apply({.height = 26.0f, .autoSize = scene::axes::kX, .minWidth = 26.0f, .padding = {0.0f, 9.0f, 0.0f, 9.0f},
                  .cornerRadius = 13.0f, .background = own ? accent_colour : tile_colour});
    if (pictured(key)) {
      parts.picture.emplace(from_avatars{key});
      parts.picture->apply({.width = 18.0f, .height = 18.0f, .alignSelf = scene::align::kMiddle});
    }
    parts.label.apply({.alignSelf = scene::align::kMiddle});
    if (!people.empty() && people.size() <= kFacesShown) {
      // The count's place taken by the faces: the reaction alone before them.
      parts.label.setText(pictured(key) ? std::string() : label_of(key, 0).substr(0, label_of(key, 0).size() - 2));
      parts.label.setVisible(!pictured(key));
      parts.who.reserve(people.size());
      for (const auto& [id, name] : people) {
        parts.who.emplace_back(id, name, 20.0f);
        parts.who.back().apply({.margin = {0.0f, 0.0f, 0.0f, parts.who.size() == 1 ? 2.0f : -6.0f},
                                .border = scene::Border{own ? accent_colour : tile_colour, 1.5f}});
      }
    }
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
  std::vector<nodes::Text::Styled> styles;
};
[[nodiscard]] inline mentioned with_mentions(std::string text, std::vector<nodes::Text::Link> links,
                                             const conversation& in, const model* now,
                                             std::vector<nodes::Text::Styled> styles = {}) {
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
    if (!logic::id_shaped(word))
      return std::nullopt;
    return logic::matrix_id_of(std::string(word));
  };
  // What in the text is replaced: by a pill, or by nothing where it is
  // shown as a card instead.
  struct replaced {
    std::size_t first, last;
    std::optional<logic::link_t> pill;
    // Shown as written, not by its name: a room's address as the message
    // has it -- a pill only where the room is known.
    std::optional<std::string> as_written = std::nullopt;
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
      if (end > at && !inside) {
        if (auto what = id_in(std::string_view(text).substr(at, end - at))) {
          spans.push_back({at, end, std::move(what), std::string(text.substr(at, end - at))});
          at = end;
          continue;
        }
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
    const bool person = span.pill && std::visit(overloaded{[](const logic::link::person&) { return true; },
                                                           [](const auto&) { return false; }},
                                                *span.pill);
    if (span.pill && span.as_written && !person) {
      // A room's address written in the text: shown as written, not by the
      // room's name; a pill with its avatar where the room is one this
      // account knows, else the address alone.
      const bool known = now && logic::chat_of(*now, *span.pill).has_value();
      const auto [name, target] = name_of(*span.pill);
      if (known) {
        shown = "\u2002\u2002" + *span.as_written;
        pill = nodes::Text::Link{span.first, span.first + shown.size(), "https://matrix.to/#/" + target, true};
      } else {
        shown = *span.as_written;
      }
    } else if (span.pill) {
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
    // And the styles: moved where they are after it, grown or shrunk where
    // it is inside them.
    for (auto& style : styles) {
      if (style.first >= span.last)
        style.first = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(style.first) + grew);
      if (style.last >= span.last)
        style.last = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(style.last) + grew);
    }
    if (pill)
      out.links.push_back(std::move(*pill));
  }
  for (auto& link : kept)
    out.links.push_back(std::move(link));
  // What is left of the text: without the space a card's link stood in.
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
    text.pop_back();
  for (auto& style : styles)
    style.last = std::min(style.last, text.size());
  std::erase_if(styles, [](const auto& style) { return style.first >= style.last; });
  out.styles = std::move(styles);
  out.text = std::move(text);
  return out;
}

// A message's text as a quote's one line shows it: its HTML read, and its
// mentions -- people, rooms -- by their names, as in the message itself,
// not the raw addresses; without the room a pill's avatar takes.
[[nodiscard]] inline std::string quote_line_of(const message& said, const conversation& in, const model* now) {
  mentioned shown;
  if (said.body.html) {
    auto read = read_html(*said.body.html);
    shown = with_mentions(std::move(read.text), std::move(read.spans), in, now);
  } else {
    shown = with_mentions(said.body.plain, link_spans_in(said.body.plain), in, now);
  }
  std::string out = std::move(shown.text);
  for (std::size_t at = out.find("\u2002\u2002"); at != std::string::npos; at = out.find("\u2002\u2002", at))
    out.erase(at, std::string_view("\u2002\u2002").size());
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


// What a message's text draws of the program's: a pill's avatar -- the one
// of what it names, else its initials on its colours -- and a custom
// emoji's picture, fetched as an avatar is. Given to its text as a type.
struct message_pictures {
  static std::optional<skiff::scene::PillPicture> pill(std::string_view target) {
    const auto at = target.find("#/");
    const std::string_view id = at == std::string_view::npos ? target : target.substr(at + 2);
    const auto [top, bottom] = userpic_colours(id);
    return skiff::scene::PillPicture{avatar_images().find(id), top, bottom, initials_of(id)};
  }
  static const skia::Sp<skia::SkImage>* picture(std::string_view target) {
    return avatar_images().find(std::string(target));
  }
};

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

  // tdesktop's msgPadding, margins(11, 8, 11, 8); its time sits lower than
  // the text by msgDateDelta, point(2, 5): 5 over the bubble's bottom.
  static constexpr float kPadX = 11.0f;
  static constexpr float kPadY = 8.0f;
  static constexpr float kTimeLower = kPadY - 5.0f;
  static constexpr float kAvatar = 34.0f;
  static constexpr float kMaxWidth = 480.0f;

  // What it answers, as tdesktop's reply (history_view_reply.cpp): a block
  // tinted with the sender's colour (at 0.12), rounded 5, with a bar of it
  // down its left (3, at 0.9); the sender's name in it, semibold, over a
  // line of the message in the text's colour; a quoted picture's thumbnail,
  // 32 and rounded, at its left (historyReplyPreview). Its padding,
  // historyReplyPadding: 2 above and below, 11 at the left, 6 at the right.
  static constexpr float kReplyLineMax = 240.0f;  // tdesktop's maxSignatureSize
  [[nodiscard]] static skia::SkColor with_alpha(skia::SkColor colour, float alpha) {
    return (colour & 0x00FFFFFFu) | (static_cast<skia::SkColor>(std::lround(alpha * 255.0f)) << 24);
  }
  struct quote_row : nodes::Stack {
    // Who said it over a line of it, each cut at the bubble's width.
    struct said_column : nodes::Stack {
      struct parts_t {
        nodes::Text who;
        nodes::Text said;
      } parts;
      said_column(skia::SkColor colour, std::string name, std::string line)
          : parts{.who = nodes::Text(std::move(name), 13.0f, colour, true),
                  .said = nodes::Text(std::move(line), 13.0f, text_colour)} {
        auto& [who, said] = parts;
        fState.apply({.autoSize = scene::axes::kY, .grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
        // Both as wide as the quote, cut where it ends: the quote is as wide
        // as its bubble.
        for (nodes::Text* each : {&who, &said}) {
          each->setElided(true);
          each->apply({.fillX = true});
        }
      }
    };
    struct parts_t {
      nodes::Box<> bar;
      // A picture quoted: its thumbnail.
      std::optional<nodes::Image<from_thumbnails>> thumb;
      said_column texts;
    } parts;
    quote_row(skia::SkColor colour, std::string who, std::string said, std::optional<std::string> picture = std::nullopt)
        : parts{.bar = nodes::Box<>(with_alpha(colour, 0.9f)), .texts = said_column(colour, std::move(who), std::move(said))} {
      auto& [bar, thumb, texts] = parts;
      this->setHorizontal();
      this->setGap(4.0f);
      fState.apply({.fillX = true,
                    .autoSize = scene::axes::kY,
                    .margin = {2.0f, 0.0f, 4.0f, 0.0f},
                    .padding = {2.0f, 6.0f, 2.0f, picture ? 7.0f : 11.0f},
                    .cornerRadius = 5.0f,
                    .background = with_alpha(colour, 0.12f),
                    .masking = true});
      bar.apply({.place = scene::anchor::kTopLeft, .x = picture ? -7.0f : -11.0f, .y = -2.0f, .fillY = true, .width = 3.0f});
      if (picture) {
        thumb.emplace(from_thumbnails{*picture});
        thumb->apply({.width = 32.0f, .height = 32.0f, .alignSelf = scene::align::kMiddle,
                      .margin = {2.0f, 0.0f, 2.0f, 0.0f}, .cornerRadius = 3.0f, .background = tile_colour});
      }
    }
  };
  // The bubble: as wide as what it says, up to its largest.
  struct body_column : nodes::Stack {
    bool outgoing = false;
    // The sender's name over their run, in their colour, and after it their
    // role in the room, dim, as Telegram shows "admin".
    struct name_row : nodes::Stack {
      struct parts_t {
        nodes::Text name;
        nodes::Text role;
      } parts;
      name_row(std::string who, skia::SkColor colour, std::string role)
          : parts{.name = nodes::Text(std::move(who), 13.0f, colour, true),
                  .role = nodes::Text(std::move(role), 12.0f, dim_colour)} {
        this->setHorizontal();
        this->setGap(10.0f);
        fState.apply({.autoSize = scene::axes::kBoth});
        // Sized as the name alone was: cut where it passes the bubble's
        // widest, the role's room kept.
        parts.name.setElided(true);
        parts.name.setMaxWidth(kMaxWidth - 60.0f);
        parts.role.setVisible(!parts.role.text().empty());
        parts.role.apply({.alignSelf = scene::align::kEnd});
      }
    };
    struct parts_t {
      std::optional<name_row> name;
      std::optional<quote_row> quote;
      std::optional<picture_view> picture;
      std::optional<album_view> album;
      std::optional<file_view> file;
      nodes::BasicText<message_pictures> text;
      std::vector<link_card> cards;
      std::optional<page_preview> preview;
      std::optional<reaction_row> reactions;
      nodes::Text time;
      // The time inside the last line of the text, where that line leaves
      // room for it, as Telegram's: out of the column's flow, at its end.
      nodes::Text inline_time;
      // The last of a run's tail, as Telegram's: out of the flow, at the
      // corner on the sender's side, in the bubble's colour.
      std::optional<nodes::Icon> tail;
    } parts;
    skia::SkColor plate = bubble_colour;
    // Its least width as the message asks it (a quote's), and as the time
    // beside the last line asks it: the bubble widened to hold both.
    float base_min = 0.0f;
    float widened = 0.0f;
    // Whether where the time goes has been decided, from a layout: until it
    // has, the bubble asks for frames, for a window at rest updates nothing
    // and the time stayed under the text.
    bool time_placed = false;
    [[nodiscard]] bool settling() const { return !time_placed; }
    [[nodiscard]] static skia::SkColor mixed(skia::SkColor from, skia::SkColor to, float amount) {
      const auto channel = [&](int shift) {
        const float a = static_cast<float>((from >> shift) & 0xFF), b = static_cast<float>((to >> shift) & 0xFF);
        return static_cast<skia::SkColor>(std::lround(a + (b - a) * amount)) << shift;
      };
      return (from & 0xFF000000u) | channel(16) | channel(8) | channel(0);
    }
    // The time goes beside the last line of the text wherever the two fit in
    // the bubble at its widest, as tdesktop's -- the bubble widened to them
    // where it is narrower; on a line of its own only where they do not.
    // Decided from the last layout; a change is laid out at the next.
    void update(double now_ms) {
      auto& [name, quote, picture, album, file, text, cards, preview, reactions, time, inline_time, tail] = parts;
      if (!cards.empty() || preview || (!text.visible() && !reactions)) {
        time_placed = true;  // under it, as it is
        return;
      }
      skia::SkFont* font = skiff::paint::defaultFont();
      if (font == nullptr)
        return;
      if (!reactions && text.bounds().isEmpty()) {
        this->guess_time(*font);
        return;
      }
      // What the time goes beside: the reactions where there are some, as
      // Telegram puts it on their line, else the text's last line.
      const skia::SkRect last = reactions ? reactions->bounds() : text.bounds();
      if (last.isEmpty())
        return;
      time_placed = true;
      float last_width = text.lastLineWidth();
      if (reactions)
        last_width = reactions->chips().empty() ? 0.0f : reactions->chips().back().bounds().fRight - last.fLeft;
      const float needs =
          last_width + skiff::paint::Painter(nullptr, *font).measure(inline_time.text(), 11.0f) + 10.0f;
      const bool inside = needs <= kMaxWidth;
      const float widest = inside ? std::ceil(needs) + 2.0f * kPadX : 0.0f;
      if (inside == time.visible() || widest != widened) {
        time.setVisible(!inside);
        inline_time.setVisible(inside);
        widened = widest;
        fState.apply({.minWidth = std::max(base_min, widest)});
        this->invalidateLayout();
      }
      // On the last line: its bottom where the text's is, wherever the text
      // ends in the bubble -- anchored to the bubble's bottom alone, it stood
      // above the line it is beside.
      if (inside) {
        const float drop = last.fBottom - fState.contentBox().fBottom;
        if (std::abs(drop - time_drop) > 0.25f) {
          time_drop = drop;
          inline_time.apply({.y = drop + kTimeLower});
          this->invalidateLayout();
        }
      }
    }
    float time_drop = 0.0f;
    // The tail: 10 by 12, its straight side on the bubble's edge, curving
    // down and out to its tip at the bubble's bottom.
    static IconShape tail_shape(bool mine) {
      const float side = mine ? -5.0f : 5.0f, tip = -side;
      return {{{marks::path{{steps::move{side + (mine ? -1.0f : 1.0f), -6.0f}, steps::line{side, -6.0f},
                             steps::cubic{side, 1.0f, side * 0.2f, 5.0f, tip, 6.0f},
                             steps::line{side + (mine ? -1.0f : 1.0f), 6.0f}, steps::close{}}},
                0.0f, true}}};
    }
    void grow_tail(bool mine) {
      parts.tail.emplace(tail_shape(mine), plate);
      parts.tail->apply({.place = mine ? scene::anchor::kBottomRight : scene::anchor::kBottomLeft,
                         .x = mine ? kPadX + 10.0f : -(kPadX + 10.0f),
                         .y = kPadY,
                         .width = 10.0f,
                         .height = 12.0f});
      fState.apply({.corners = mine ? scene::Corners{12.0f, 12.0f, 0.0f, 12.0f} : scene::Corners{12.0f, 12.0f, 12.0f, 0.0f}});
    }
    // Before its first layout, where the time goes is guessed from the text
    // wrapped at the bubble's widest -- where it does wrap, but in a chat
    // narrower than a bubble. So a bubble made anew -- sent, edited, reacted
    // to -- is drawn right from its first frame, not with its time under
    // its text for one and then beside it, a jump each time. The layout
    // checks the guess, above.
    bool guessed = false;
    void guess_time(skia::SkFont& font) {
      auto& [name, quote, picture, album, file, text, cards, preview, reactions, time, inline_time, tail] = parts;
      if (std::exchange(guessed, true) || text.text().empty())
        return;
      const skiff::paint::Painter p(nullptr, font);
      const auto lines = p.wrap(text.text(), kMaxWidth, 13.0f, false);
      if (lines.empty())
        return;
      const float needs = p.measure(lines.back(), 13.0f, false) + p.measure(inline_time.text(), 11.0f) + 10.0f;
      if (needs > kMaxWidth)
        return;
      time.setVisible(false);
      inline_time.setVisible(true);
      widened = std::ceil(needs) + 2.0f * kPadX;
      fState.apply({.minWidth = std::max(base_min, widened)});
    }
    body_column(bool mine, std::string said, std::string when)
        : outgoing(mine),
          parts{.text = nodes::BasicText<message_pictures>(std::move(said), 13.0f, text_colour),
                .time = nodes::Text(when, 11.0f, mine ? sent_time_colour : dim_colour),
                .inline_time = nodes::Text(when, 11.0f, mine ? sent_time_colour : dim_colour)},
          plate(mine ? out_bubble_colour : bubble_colour) {
      auto& [name, quote, picture, album, file, text, cards, preview, reactions, time, inline_time, tail] = parts;
      this->setGap(2.0f);
      fState.apply({.autoSize = scene::axes::kBoth, .maxWidth = kMaxWidth + 2.0f * kPadX,
                    .padding = {kPadY, kPadX, kPadY, kPadX}, .cornerRadius = 12.0f, .background = mine ? out_bubble_colour : bubble_colour});
      text.setWrapped(true);
      text.setShrinksToLines(true);
      time.apply({.alignSelf = scene::align::kEnd});
      // Shown once the last line is found to leave room for it.
      inline_time.apply({.place = scene::anchor::kBottomRight, .y = kTimeLower});
      inline_time.setVisible(false);
    }
  };

  // tdesktop's bar over the first unread: its words in the middle of a band
  // the width of the chat.
  struct unread_bar_t : nodes::Stack {
    static constexpr float kHeight = 26.0f;
    struct parts_t {
      nodes::Text label{"Unread messages", 13.0f, dim_colour, true};
    } parts;
    unread_bar_t() {
      fStack.justify = nodes::justify::middle{};
      fState.apply({.place = scene::anchor::kTopLeft, .y = -(kHeight + 4.0f), .fillX = true, .height = kHeight,
                    .background = sidebar_colour});
      parts.label.apply({.alignSelf = scene::align::kMiddle});
    }
  };
  struct parts_t {
    // The sender's avatar, beside the last of their run in a group; the
    // same room, empty, beside the rest.
    avatar_mark face;
    body_column body;
    // The arrow a swipe shows, filling as it reaches its mark.
    nodes::Icon swipe_mark{shape_of(icon::back{}), dim_colour};
    // Over the first unread message of a chat opened: tdesktop's bar.
    std::optional<unread_bar_t> unread_bar;
  } parts;
  // Whether it has the bar: kept while the chat's first unread is it.
  bool unread_start = false;
  // tdesktop's "Unread messages" bar, across the whole row, over it.
  void mark_unread_start() {
    unread_start = true;
    parts.unread_bar.emplace();
    fState.apply({.padding = {fState.fPadding.fTop + unread_bar_t::kHeight + 6.0f, fState.fPadding.fRight,
                              fState.fPadding.fBottom, fState.fPadding.fLeft}});
    this->invalidateLayout();
  }

  // Declared: the avatar's room and the bubble, at the right where it is
  // one's own; the bubble a column of the name, the quote, the text, the
  // links, the reactions and the time.
  message_bubble(const conversation& in, const message& said, bool first_of_run, bool last_of_run,
                 const model* now = nullptr, bool show_events = true)
      : said(said), first(first_of_run), last(last_of_run), message_id(said.id), plain(said.body.plain),
        outgoing(said.outgoing), sender(said.sender),
        parts{.face = avatar_mark(said.sender, sender_name(in, said.sender), kAvatar),
              .body = body_column(said.outgoing, said.body.plain, mark_of(said) + clock_of(said.at))} {
    auto& [face, body, swipe_mark, unread_bar] = parts;
    swipe_mark.apply({.place = scene::anchor::kCentreRight,
                      .x = -6.0f,
                      .width = 28.0f,
                      .height = 28.0f,
                      .cornerRadius = 14.0f,
                      .background = tile_colour,
                      .alpha = 0.0f});
    this->setHorizontal();
    this->setGap(8.0f);
    // As tdesktop: a sender's messages one under the other nearly touch;
    // where the sender changes, a gap.
    const bool group = is_group(in);
    // The avatar hangs at the row's bottom left, as tdesktop's: out of the
    // row's flow, the row keeping its width on the left. In the flow, a
    // 34-high avatar made a one-line bubble's row taller, and the last
    // bubble of a run stood apart from the rest.
    const bool with_face = group && !outgoing && !said.service;
    fState.apply({.fillX = true, .autoSize = scene::axes::kY,
                  .padding = {first_of_run ? 8.0f : 1.0f, 0.0f, 1.0f, with_face ? kAvatar + 8.0f : 0.0f}});
    if (outgoing)
      fStack.justify = nodes::justify::end{};
    face.setVisible(with_face);
    // Placed in the content box: back over the padding kept for it.
    face.apply({.place = scene::anchor::kBottomLeft, .x = -(kAvatar + 8.0f), .y = -1.0f});
    if (!(group && !outgoing && last_of_run))
      face.fState.setAlpha(0.0f);  // its room kept, so the run's bubbles line up
    if (group && !outgoing && first_of_run && !said.service) {
      // Their role, where the room gives them a say: Matrix's 100 and 50.
      const auto level = in.powers.find(said.sender);
      const std::int64_t power = level == in.powers.end() ? in.power_default : level->second;
      body.parts.name.emplace(sender_name(in, said.sender), avatar_colour(said.sender),
                              power >= 100 ? std::string("admin") : power >= 50 ? std::string("mod") : std::string());
    }
    // Something done, not said: a line in the middle, on a plate of its own,
    // with no avatar and no name -- as tdesktop's service messages.
    if (said.service) {
      fStack.justify = nodes::justify::middle{};
      face.setVisible(false);
      body.apply({.cornerRadius = 12.0f, .background = tile_colour});
      // Not shown where the chat's settings say so: kept, and out of the
      // flow, taking no room.
      events_shown = show_events;
      this->setVisible(show_events);
    }
    // Anyone's words can be selected and copied, as in Telegram.
    body.parts.text.setSelectable(true);
    body.parts.text.setSelectionColour((accent_colour & 0x00FFFFFFu) | (110u << 24));  // the accent, see-through
    std::string when = mark_of(said) + clock_of(said.at);
    when += std::visit(overloaded{[](const delivery::sending&) { return " · sending"; },
                                  [](const delivery::failed&) { return " · not sent"; },
                                  [](const auto&) { return ""; }},
                       said.delivery);
    body.parts.time.setText(when);
    // What it carries: a picture, sized as tdesktop's; or a file's row.
    if (said.attachment) {
      const mux::attachment& carried = *said.attachment;
      std::visit(overloaded{[&](attachment_kind::image) {
                              body.parts.picture.emplace(carried.source, carried.width, carried.height);
                            },
                            [&](attachment_kind::file) {
                              body.parts.file.emplace(carried.source, carried.name, carried.size,
                                                      audio_type(carried.mimetype, carried.name));
                            }},
                 carried.kind);
      // No caption: the text goes, and a picture has its time over it.
      if (said.body.plain.empty() && !said.body.html) {
        body.parts.text.setVisible(false);
        if (body.parts.picture) {
          body.parts.picture->show_time(when);
          body.parts.time.setVisible(false);
          body.apply({.padding = {3.0f, 3.0f, 3.0f, 3.0f}});
        }
      }
    }
    // A gallery: its pictures as an album, its body the caption under it.
    if (!said.album.empty())
      body.parts.album.emplace(said.album);
    // Formatted, it is drawn from its HTML: its text, and its links; plain,
    // its links are the URLs in it.
    // Its links in its text, where they stand: an <a>'s label going where
    // its href says, and the addresses in a plain text.
    mentioned shown;
    if (said.body.html) {
      auto read = read_html(*said.body.html);
      shown = with_mentions(std::move(read.text), std::move(read.spans), in, now, std::move(read.styles));
    } else {
      shown = with_mentions(said.body.plain, link_spans_in(said.body.plain), in, now);
    }
    {
      body.parts.text.setText(shown.text);
      body.parts.text.setLinks(std::move(shown.links), accent_colour);
      body.parts.text.setStyles(std::move(shown.styles), accent_colour);
      body.parts.text.setVisible(!shown.text.empty());
      for (const auto& [url, room] : shown.cards)
        body.parts.cards.push_back(card_of(url, room, now));
    }
    // The last of a run: its bottom corner on the sender's side squared and
    // a tail grown from it, as Telegram draws one -- not for a line of what
    // was done, nor a picture with nothing under it.
    const bool bare_picture = said.attachment && said.body.plain.empty() && !said.body.html && body.parts.picture;
    if (last_of_run && !said.service && !bare_picture)
      body.grow_tail(outgoing);
    // The first link's preview, where it has come.
    if (const auto link = first_link_of(said); link && now)
      if (const auto found = now->previews.find(*link); found != now->previews.end()) {
        body.parts.preview.emplace(found->second, *link);
        preview_known = true;
      }
    if (said.replies_to) {
      const auto in_timeline = std::ranges::find(in.timeline, *said.replies_to, &message::id);
      const auto aside = in.quoted.find(*said.replies_to);
      const message* found = in_timeline != in.timeline.end() ? &*in_timeline
                             : aside != in.quoted.end()      ? &aside->second
                                                             : nullptr;
      const bool known = found != nullptr;
      quote_known = known;
      // A picture's: its thumbnail, and its caption or "Photo"; a file's:
      // its name; else its text.
      std::optional<std::string> picture;
      std::string line = known ? quote_line_of(*found, in, now) : std::string("not loaded");
      if (known && found->attachment) {
        if (is_picture(found->attachment->kind))
          picture = found->attachment->source;
        if (line.empty())
          line = is_picture(found->attachment->kind) ? std::string("Photo") : found->attachment->name;
      }
      std::ranges::replace(line, '\n', ' ');
      const bool with_picture = picture.has_value();
      body.parts.quote.emplace(known ? avatar_colour(found->sender) : accent_colour,
                         known ? (found->outgoing ? std::string("You") : sender_name(in, found->sender))
                               : std::string("A message"),
                         std::move(line), std::move(picture));
      // The quote spans its bubble, as tdesktop's; the bubble is at least as
      // wide as the quote asks -- its name and its line, the line counted up
      // to maxSignatureSize (240), so that a short answer to a long message
      // does not stretch its bubble to the full width.
      float asks = 160.0f;
      if (skia::SkFont* font = skiff::paint::defaultFont()) {
        skiff::paint::Painter measure(nullptr, *font);
        const auto& texts = body.parts.quote->parts.texts.parts;
        const float words = std::max(measure.measure(texts.who.text(), 13.0f),
                                     std::min(measure.measure(texts.said.text(), 13.0f), kReplyLineMax));
        const float around = (with_picture ? 7.0f + 32.0f + 4.0f : 11.0f) + 6.0f + 2.0f * kPadX;
        asks = std::min(std::ceil(words) + around, kMaxWidth + 2.0f * kPadX);
      }
      body.base_min = asks;
      body.apply({.minWidth = asks});
    }
    if (!said.reactions.empty()) {
      body.parts.reactions.emplace();
      for (const auto& [key, who] : said.reactions)
        if (!who.empty())
          body.parts.reactions->chips().emplace_back(key, who.size(), who.contains(said.in.account.address), [&] {
            std::vector<std::pair<std::string, std::string>> people;
            for (const std::string& one : who)
              people.emplace_back(one, sender_name(in, one));
            return people;
          }());
    }
  }

  // Swiped to the left to answer it: how far, following the pointer, and
  // back to its place when let go. Its avatar and bubble drawn moved --
  // where they are laid out does not change -- and the arrow of a reply
  // coming in at the right, out of the row's flow, lit once it is far
  // enough.
  // Whether the message its reply quotes was there to quote when it was
  // made: made again once it is, from the timeline or fetched beside it.
  bool quote_known = true;
  // Whether room events were shown when it was made: made again when that
  // changes.
  bool events_shown = true;
  // Whether its link's preview had come when it was made.
  bool preview_known = false;
  skiff::paint::Tween swipe{0.0f, 180.0f, skiff::paint::movement::subtle{}};
  static constexpr float kSwipeToReply = 70.0f;
  // Where it was jumped to: the whole row -- from the message to the edges,
  // as Telegram's -- washed in the accent, fading.
  skiff::paint::Tween flash{0.0f, 1200.0f};
  // A message that has just come: in from below, fading in, as Telegram's.
  // Unseen until its first frame is laid out, so the time is where it goes
  // when it is first seen -- not under the text, then beside it.
  skiff::paint::Tween appearing{1.0f, 220.0f};
  // A stretch of its text marked -- what a reply quoted of it -- while it
  // is flashed; let go as the flash ends.
  bool marked = false;
  // Marks what is found of `fragment` in its text; where, as the offset in
  // it, or nothing.
  std::optional<std::size_t> mark(std::string_view fragment) {
    auto& text = parts.body.parts.text;
    const auto at = std::string_view(text.text()).find(fragment);
    if (fragment.empty() || at == std::string_view::npos)
      return std::nullopt;
    auto styles = text.styles();
    styles.push_back({.first = at, .last = at + fragment.size(), .marked = true});
    text.setStyles(std::move(styles), accent_colour);
    marked = true;
    return at;
  }
  void unmark() {
    auto& text = parts.body.parts.text;
    auto styles = text.styles();
    std::erase_if(styles, [](const auto& one) { return one.marked; });
    text.setStyles(std::move(styles), accent_colour);
    marked = false;
  }
  void appear() {
    appearing.jump(0.0f);
    appearing.setTarget(1.0f);
    fState.apply({.alpha = 0.0f, .shiftY = 12.0f});
  }
  // The swipe's offset as last drawn: a drag jumps the tween, which then
  // does not move, and the bubble followed only once it was let go.
  float swipe_drawn = 0.0f;
  [[nodiscard]] bool settling() const {
    return swipe.moving() || swipe.value() != swipe_drawn || flash.moving() || appearing.moving() || marked;
  }
  void update(double now_ms) {
    if (flash.step(now_ms))
      fState.apply({.background = (accent_colour & 0x00FFFFFFu) |
                                  (static_cast<skia::SkColor>(std::lround(80.0f * flash.value())) << 24)});
    if (marked && !flash.moving())
      this->unmark();
    if (appearing.step(now_ms)) {
      const float shown = appearing.value();
      fState.apply({.alpha = shown, .shiftY = (1.0f - shown) * 12.0f});
    }
    const bool stepped = swipe.step(now_ms);
    if (!stepped && swipe.value() == swipe_drawn)
      return;
    auto& [face, body, swipe_mark, unread_bar] = parts;
    const float shift = swipe.value();
    swipe_drawn = shift;
    const float reached = std::clamp(-shift / kSwipeToReply, 0.0f, 1.0f);
    face.apply({.shiftX = shift});
    body.apply({.shiftX = shift});
    swipe_mark.apply({.background = reached >= 1.0f ? accent_colour : tile_colour, .alpha = reached});
    swipe_mark.setColour(reached >= 1.0f ? on_accent_colour : dim_colour);
  }

  // Pressed with the right button, it asks for its menu.
  [[nodiscard]] bool acceptsInput() const { return true; }
};

}  // namespace mux::ui
