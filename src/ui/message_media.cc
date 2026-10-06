// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:message_media -- What a message carries: its picture, album and file, and its reactions.
export module mux.ui:message_media;

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
import :message_pieces;

export namespace mux::ui {
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
  // Over a video's thumbnail, as Telegram's: a dark disc with the play mark
  // in the middle, and its length on a dark pill at the top left.
  struct video_marks : nodes::Stack {
    struct disc : nodes::Stack {
      struct parts_t {
        nodes::Icon mark{shape_of(icon::play{}), skia::colorSetARGB(255, 255, 255, 255)};
      } parts;
      disc() {
        fStack.justify = nodes::justify::middle{};
        fState.apply({.place = scene::anchor::kCentre, .width = 44.0f, .height = 44.0f, .cornerRadius = 22.0f,
                      .background = skia::colorSetARGB(0x54, 0, 0, 0)});
        parts.mark.apply({.width = 18.0f, .height = 18.0f, .alignSelf = scene::align::kMiddle});
      }
    };
    struct length : nodes::Stack {
      struct parts_t {
        nodes::Text label;
      } parts;
      explicit length(std::int64_t ms)
          : parts{.label = nodes::Text(std::format("{}:{:02}", ms / 60000, (ms / 1000) % 60), 11.0f,
                                       skia::colorSetARGB(255, 255, 255, 255))} {
        fState.apply({.place = scene::anchor::kTopLeft, .autoSize = scene::axes::kBoth,
                      .margin = {6.0f, 0.0f, 0.0f, 6.0f}, .padding = {2.0f, 8.0f, 2.0f, 8.0f}, .cornerRadius = 9.0f,
                      .background = skia::colorSetARGB(0x54, 0, 0, 0)});
        this->setVisible(ms > 0);
      }
    };
    struct parts_t {
      disc play;
      length runs;
    } parts;
    explicit video_marks(std::int64_t ms) : parts{.runs = length(ms)} { fState.apply({.fill = true}); }
  };
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
    widgets::RadialLoader<> loader{};  // while it is coming
    time_pill time{};
    // A video's: the play mark in the middle, its length at the top left.
    std::optional<video_marks> video;
  } parts;
  bool has_thumbnail = true;
  void show_video(std::int64_t duration_ms, bool thumbnail = true) {
    has_thumbnail = thumbnail;
    parts.video.emplace(duration_ms);
    parts.picture.setVisible(has_thumbnail);
    parts.preview.setVisible(has_thumbnail);
    if (!has_thumbnail)
      parts.loader.setVisible(false);
    this->invalidateLayout();
  }

  // Rounded; a plate until the thumbnail comes, then the thumbnail covering
  // it, cut at the middle where the proportions differ by a rounding.
  picture_view(const palette& colours, std::string where, int w, int h)
      : source(where), width(w), height(h),
        parts{.preview = nodes::Image<from_previews>({where}),
              .picture = nodes::Image<from_moving_thumbnail>({where})} {
    fState.apply({.cornerRadius = 10.0f, .background = colours.tile, .masking = true});
    parts.preview.apply({.fill = true, .cornerRadius = 10.0f});
    parts.picture.apply({.fill = true, .cornerRadius = 10.0f});
    parts.loader.apply({.place = scene::anchor::kCentre});
  }
  // The loader while the picture has not come; where it moves, drawn again
  // each frame for the next of its frames.
  [[nodiscard]] bool settling() const { return animations().has(source); }
  // Ticked while the picture is coming or moves. Its thumbnail coming is
  // seen by the picture itself, which marks this; every picture message
  // was ticked at every frame for as long as its whole picture was not
  // fetched -- that is, nearly always.
  [[nodiscard]] bool wantsTick() const { return parts.loader.visible() || animations().has(source); }
  void update(double) {
    const bool moving = animations().has(source);
    const bool coming = has_thumbnail && !moving && !thumbnails().has(source) && !whole_pictures().has(source);
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
    const float most = sticker ? kStickerMax : kMax;
    const float room = parent.width() > 0.0f ? parent.width() : most;
    const float scale = std::min({1.0f, most / w, most / h, room / w});
    w *= scale;
    h *= scale;
    if (!sticker && w < kMin && h < kMin) {
      const float up = std::min(kMin / std::max(w, h), room / w);
      w *= up;
      h *= up;
    }
    fState.fWidth = std::floor(w);
    fState.fHeight = std::floor(h);
  }
  [[nodiscard]] bool acceptsInput() const { return true; }
  // A sticker: no plate under it -- what it does not cover shows what is
  // behind -- and no larger than tdesktop's (maxStickerSize, 256 a side).
  bool sticker = false;
  static constexpr float kStickerMax = 256.0f;
  void as_sticker() {
    sticker = true;
    fState.apply({.background = skia::SkColor{0}});
    this->invalidateLayout();
  }
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
  album_view(const palette& colours, const std::vector<attachment>& items) {
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
        made.parts.cells.emplace_back(colours, item.source, item.width, item.height);
        made.parts.cells.back().set_cell(std::floor(w), std::floor(h));
        sizes.emplace_back(w, h);
      }
    }
  }
  // Each cell's size at the album's own width, and the share of it the
  // album is laid out at: in a column narrower than 360 -- a thread's panel,
  // a narrow window -- the album is fitted to it, as a single picture is,
  // rather than standing out of its bubble.
  std::vector<std::pair<float, float>> sizes;
  float fitted = 1.0f;
  void measure(const skia::SkRect& parent) {
    // Less the gap a row of two keeps, so that a row of two fits too.
    const float scale = parent.width() > 0.0f ? std::min(1.0f, (parent.width() - kGap) / (kWidth - kGap)) : 1.0f;
    if (scale <= 0.0f || scale == fitted)
      return;
    fitted = scale;
    auto cells = std::views::join(std::views::transform(parts.rows, [](row& one) -> std::vector<picture_view>& { return one.parts.cells; }));
    for (auto&& [cell, size] : std::views::zip(cells, sizes))
      cell.set_cell(std::floor(size.first * scale), std::floor(size.second * scale));
  }
};


// A file in a message, as tdesktop's row: a round icon in the accent, the
// name over its size; pressed, it is saved and opened.
struct file_view : nodes::Stack {
  std::string source;
  // tdesktop's msgFileSize: the icon, and so the row, is this high.
  static constexpr float kIcon = 44.0f;
  struct disc : nodes::Icon {
    explicit disc(const palette& colours) : nodes::Icon(shape_of(icon::clip{}), colours.on_accent) {
      fState.apply({.width = kIcon, .height = kIcon, .alignSelf = scene::align::kMiddle, .cornerRadius = kIcon / 2.0f,
                    .background = colours.accent});
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
    texts_column(const palette& colours, std::string name, std::string size)
        : parts{.name = nodes::Text(std::move(name), 14.0f, colours.text, true),
                .size = nodes::Text(std::move(size), 12.0f, colours.dim)} {
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
  // What plays it: the program's, handed down; none where nothing plays.
  platform::audio::speaker* speaker_ = nullptr;
  [[nodiscard]] bool settling() const { return sound && speaker_ && speaker_->holds(source); }
  // A voice message's is ticked, for its button and its time; a file's not.
  [[nodiscard]] bool wantsTick() const { return sound; }
  void update(double) {
    if (!sound || !speaker_)
      return;
    auto& speaker = *speaker_;
    speaker.tick();
    const bool playing = speaker.playing(source);
    if (playing != shown_playing) {
      shown_playing = playing;
      parts.icon.setShape(shape_of(playing ? icon_t{icon::pause{}} : icon_t{icon::play{}}));
    }
    const std::string time = speaker.holds(source)
                                 ? std::format("{} / {}", mux::platform::audio::clock(speaker.position()),
                                               mux::platform::audio::clock(speaker.length()))
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
  file_view(const palette& colours, platform::audio::speaker* speaker, std::string where, std::string name, std::int64_t bytes,
            bool is_sound = false)
      : source(std::move(where)), parts{.icon = disc(colours), .texts = texts_column(colours, name, size_text(bytes))}, sound(is_sound),
        size_line(size_text(bytes)), speaker_(speaker) {
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
    // Frosted, where the bubbles are: what is behind, blurred.
    std::optional<frost_pane> frost;
    std::optional<nodes::Image<from_avatars>> picture;
    nodes::Text label;
    // Who reacted, as Telegram shows them: their avatars in place of the
    // count, where they are three or fewer.
    std::vector<avatar_mark> who;
  } parts;
  [[nodiscard]] static bool pictured(std::string_view k) { return proto::is_media(k); }
  static constexpr std::size_t kFacesShown = 3;
  // What the chip says: the count beside a picture; else the reaction and
  // the count -- cut where it is drawn, by its width (kLabelMost), not by
  // its bytes: cut at 20 bytes, ten Cyrillic letters had an ellipsis with
  // room to spare.
  [[nodiscard]] static std::string label_of(std::string_view k, std::size_t n) {
    if (pictured(k))
      return std::to_string(n);
    return std::format("{} {}", k, n);
  }
  static constexpr float kLabelMost = 240.0f;
  // `people`: who reacted, by id and name.
  reaction_chip(const palette& colours, const looks_shown& looks, std::string k, std::size_t n, bool own,
                const std::vector<std::pair<std::string, std::string>>& people = {})
      : key(std::move(k)), count(n), mine(own),
        parts{.label = nodes::Text(label_of(key, n), 13.0f, own ? colours.on_accent : colours.text)} {
    this->setHorizontal();
    this->setGap(4.0f);
    fStack.justify = nodes::justify::middle{};
    fState.apply({.height = 26.0f, .autoSize = scene::axes::kX, .minWidth = 26.0f, .padding = {0.0f, 9.0f, 0.0f, 9.0f},
                  .cornerRadius = 13.0f,
                  .background = at_opacity(own ? colours.accent : colours.tile,
                                           element_opacity_of(looks.bubbles, &config::element_opacity::reactions))});
    // Frosted, where the bubbles are, as its own blur says.
    if (frosts(looks.bubbles)) {
      parts.frost.emplace(frost_source{}, element_blur_of(looks.bubbles, &config::element_blur::reactions, looks.window));
      parts.frost->apply({.place = scene::anchor::kTopLeft, .fill = true, .margin = {0.0f, -9.0f, 0.0f, -9.0f}, .cornerRadius = 13.0f});
      // Its colour over the frost: the pane's tint, its own none.
      parts.frost->setTint(fState.fBackground);
      fState.apply({.background = skia::SkColor{0}});
    }
    if (pictured(key)) {
      parts.picture.emplace(from_avatars{key});
      parts.picture->apply({.width = 18.0f, .height = 18.0f, .alignSelf = scene::align::kMiddle});
    }
    parts.label.apply({.alignSelf = scene::align::kMiddle});
    parts.label.setElided(true);
    parts.label.setMaxWidth(kLabelMost);
    if (!people.empty() && people.size() <= kFacesShown) {
      // The count's place taken by the faces: the reaction alone before them.
      parts.label.setText(pictured(key) ? std::string() : label_of(key, 0).substr(0, label_of(key, 0).size() - 2));
      parts.label.setVisible(!pictured(key));
      parts.who.reserve(people.size());
      for (const auto& [id, name] : people) {
        parts.who.emplace_back(id, name, 20.0f);
        parts.who.back().apply({.margin = {0.0f, 0.0f, 0.0f, parts.who.size() == 1 ? 2.0f : -6.0f},
                                .border = scene::Border{own ? colours.accent : colours.tile, 1.5f}});
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

}  // namespace mux::ui
