// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:message_media -- What a message carries: its picture, album and file, and its reactions.
export module mux.ui.messages:message_media;

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
import :message_pieces;

export namespace mux::ui {
// What a press on a message's media asks for: a picture opened in the
// viewer, or a video -- played in it, or, built without video, by the
// system's player.
// A video where this build plays none: the file, opened by the system's player.
using media_open = std::variant<request::open_picture, request::open_video, request::open_file>;

// How a picture in a message is: a photo, a sticker -- no plate under it,
// what it does not cover showing what is behind, no larger than tdesktop's
// (maxStickerSize, 256 a side) -- or a video's thumbnail with its length;
// its time over its corner, where it has no caption; a cell of an album,
// its size given.
struct picture_look {
  bool sticker = false;
  std::optional<std::int64_t> video_ms;  // a video's length
  bool thumbnail = true;                  // a video with a picture of its own
  std::string time;
  std::optional<std::pair<float, float>> cell;
};
inline constexpr float kPictureMost = 430.0f, kPictureLeast = 100.0f, kStickerMost = 256.0f;
// Over a video's thumbnail, as Telegram's: a dark disc with the play mark in
// the middle, and its length on a dark pill at the top left.
inline auto video_marks(std::int64_t ms) {
  namespace c = skiff::compose;
  const skia::SkColor shade = skia::colorSetARGB(0x54, 0, 0, 0), white = skia::colorSetARGB(255, 255, 255, 255);
  return c::column(c::vbox(0.0f, {.fill = true}),
      c::column(c::justified(c::vbox(0.0f, {.place = scene::anchor::kCentre, .width = 44.0f, .height = 44.0f, .cornerRadius = 22.0f,
                                            .background = shade}),
                             nodes::justify::middle{}),
                c::styled({.width = 18.0f, .height = 18.0f, .alignSelf = scene::align::kMiddle}, nodes::Icon(shape_of(icon::play{}), white))),
      c::visible(ms > 0, c::column(c::vbox(0.0f, {.place = scene::anchor::kTopLeft, .autoSize = scene::axes::kBoth,
                                                  .margin = {6.0f, 0.0f, 0.0f, 6.0f}, .padding = {2.0f, 8.0f, 2.0f, 8.0f},
                                                  .cornerRadius = 9.0f, .background = shade}),
                                   nodes::Text(std::format("{}:{:02}", ms / 60000, (ms / 1000) % 60), 11.0f, white))));
}
// The time on a dark pill over a picture's corner.
inline auto time_pill(std::string when) {
  namespace c = skiff::compose;
  const bool shown = !when.empty();
  return c::visible(shown, c::column(c::vbox(0.0f, {.place = scene::anchor::kBottomRight, .autoSize = scene::axes::kBoth,
                                                    .margin = {0.0f, 6.0f, 6.0f, 0.0f}, .padding = {2.0f, 8.0f, 2.0f, 8.0f},
                                                    .cornerRadius = 9.0f, .background = skia::colorSetARGB(0x54, 0, 0, 0)}),
                                     nodes::Text(std::move(when), 11.0f, skia::colorSetARGB(255, 255, 255, 255))));
}
// A picture in a message, as tdesktop sizes one: its own size fitted into
// 430 by 430 (maxMediaSize), no side under 100 (minPhotoSize) -- its picture's
// own proportions once it has come, never another's; rounded, the blurred
// preview from its blurhash until it comes, a loader turning meanwhile.
// Pressed: what `open` asks -- it in the viewer.
inline auto picture_view(const palette& colours, std::string source, int width, int height, picture_look look, media_open open) {
  namespace c = skiff::compose;
  auto preview = c::visible(look.thumbnail, c::styled({.fill = true, .cornerRadius = 10.0f}, nodes::Image<from_previews>({source})));
  auto picture = c::visible(look.thumbnail, c::styled({.fill = true, .cornerRadius = 10.0f},
                                                      nodes::Image<from_moving_thumbnail>({source, look.sticker})));
  auto loader = c::visible(false, c::styled({.place = scene::anchor::kCentre}, widgets::RadialLoader<>{}));
  std::optional<decltype(video_marks(0))> video;
  if (look.video_ms)
    video.emplace(video_marks(*look.video_ms));
  // The picture's own proportions: its picture's, where it has come.
  const auto ratio = [](auto& box) { return std::get<0>(std::get<1>(box.fParts).fParts).ratio(); };
  return c::onClick(std::move(open),
      c::fitted({.width = static_cast<float>(width), .height = static_cast<float>(height),
                 .most = look.sticker ? kStickerMost : kPictureMost, .least = look.sticker ? 0.0f : kPictureLeast, .cell = look.cell},
                ratio,
                c::column(c::vbox(0.0f, {.cornerRadius = 10.0f, .background = look.sticker ? skia::SkColor{0} : colours.tile, .masking = true}),
                          std::move(preview),
                          c::while_coming(c::vbox(0.0f, {.fill = true}), std::move(picture), std::move(loader)),
                          std::move(video), time_pill(std::move(look.time)))),
      look.sticker ? "Sticker" : look.video_ms ? "Play video" : "Open picture");
}
using picture_view_t = decltype(picture_view(std::declval<const palette&>(), std::string(), 0, 0, picture_look{}, media_open{}));

// Several pictures in one message, as tdesktop shows an album: rows filling
// its width with a thin gap between -- where they are odd, the first alone
// and wide, then pairs -- each picture cut to fill its cell. In a column
// narrower than 360 -- a thread's panel, a narrow window -- every cell
// shrunk alike, as a single picture is fitted, rather than standing out of
// its bubble. Each cell pressed asks what its `opens` says.
inline constexpr float kAlbumWidth = 360.0f, kAlbumGap = 2.0f;
[[nodiscard]] inline std::vector<std::size_t> album_rows(std::size_t count) {
  std::vector<std::size_t> per_row;
  if (count % 2 == 1)
    per_row.push_back(1);
  for (std::size_t placed = count % 2; placed < count; placed += 2)
    per_row.push_back(2);
  return per_row;
}
inline auto album_view(const palette& colours, const std::vector<attachment>& items, std::vector<media_open> opens) {
  namespace c = skiff::compose;
  using row_t = c::Many<picture_view_t>;
  std::vector<std::pair<float, float>> sizes;
  std::vector<row_t> rows;
  std::size_t at = 0;
  for (const std::size_t count : album_rows(items.size())) {
    const float w = (kAlbumWidth - kAlbumGap * static_cast<float>(count - 1)) / static_cast<float>(count);
    rows.push_back(c::many(c::hbox(kAlbumGap, {.autoSize = scene::axes::kBoth}),
        std::ranges::to<std::vector>(std::views::transform(std::views::iota(at, at + count), [&](std::size_t i) {
          const attachment& item = items[i];
          const float ratio = item.width > 0 && item.height > 0 ? static_cast<float>(item.height) / static_cast<float>(item.width) : 0.75f;
          const float h = count == 1 ? std::clamp(w * ratio, 120.0f, 300.0f) : std::clamp(w * 0.8f, 100.0f, 220.0f);
          sizes.emplace_back(w, h);
          return picture_view(colours, item.source, item.width, item.height,
                              {.cell = std::pair{std::floor(w), std::floor(h)}}, std::move(opens[i]));
        }))));
    at += count;
  }
  return c::shrunk(kAlbumWidth, kAlbumGap,
      [sizes](auto& album, float share) {
        auto cells = std::views::join(std::views::transform(album.fParts, [](row_t& row) -> std::vector<picture_view_t>& { return row.fParts; }));
        for (auto&& [cell, size] : std::views::zip(cells, sizes)) {
          cell.fFit.cell = std::pair{std::floor(size.first * share), std::floor(size.second * share)};
          cell.invalidateLayout();
        }
      },
      c::many(c::vbox(kAlbumGap, {.autoSize = scene::axes::kBoth}), std::move(rows)));
}
using album_view_t = decltype(album_view(std::declval<const palette&>(), {}, {}));

// A file in a message, as tdesktop's row: a round icon in the accent, the
// name over its size; pressed, what `open` asks -- it saved and opened, or,
// a sound, played. A sound's disc is its play button and its size its time,
// as Telegram's voice messages have them, as the program says it plays it
// (audio_shown); while it plays, the program is asked to look again a few
// times a second (request::audio_tick).
inline constexpr float kFileIcon = 44.0f;  // tdesktop's msgFileSize
[[nodiscard]] inline std::string size_text(std::int64_t bytes) {
  if (bytes <= 0)
    return "File";
  if (bytes < 1024)
    return std::format("{} B", bytes);
  if (bytes < 1024 * 1024)
    return std::format("{:.1f} KB", static_cast<double>(bytes) / 1024.0);
  return std::format("{:.1f} MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
}
using file_open = std::variant<request::open_file, request::play_audio>;
// The row itself, pressed or not: a file waiting to be sent is the same row.
inline auto file_row(const palette& colours, std::string source, std::string name, std::int64_t bytes, bool sound) {
  namespace c = skiff::compose;
  const auto plays = [source, sound](const audio_shown& now) { return sound && now.source == source; };
  const auto line = [plays, size = size_text(bytes)](const audio_shown& now) {
    return plays(now) ? std::format("{} / {}", platform::audio::clock(now.at), platform::audio::clock(now.length)) : size;
  };
  // Its name over its size, each as wide as it reads, up to a limit: sized by
  // what they say, so that the bubble is.
  const auto capped = [](nodes::Text text) {
    text.setElided(true);
    text.setMaxWidth(360.0f);
    return text;
  };
  return c::row(c::hbox(11.0f, {.autoSize = scene::axes::kBoth, .minWidth = 268.0f - 24.0f, .padding = {2.0f, 0.0f, 2.0f, 0.0f}}),
      c::applied<audio_shown>([plays](const audio_shown& now) { return plays(now) && now.playing; },
          [sound](nodes::Icon& disc, bool playing) {
            disc.setShape(shape_of(!sound ? icon_t{icon::clip{}} : playing ? icon_t{icon::pause{}} : icon_t{icon::play{}}));
          },
          c::styled({.width = kFileIcon, .height = kFileIcon, .alignSelf = scene::align::kMiddle, .cornerRadius = kFileIcon / 2.0f,
                     .background = colours.accent},
                    nodes::Icon(shape_of(sound ? icon_t{icon::play{}} : icon_t{icon::clip{}}), colours.on_accent))),
      c::column(c::vbox(4.0f, {.autoSize = scene::axes::kBoth, .alignSelf = scene::align::kMiddle}),
                capped(nodes::Text(std::move(name), 14.0f, colours.text, true)),
                c::text_for<audio_shown>(line, capped(nodes::Text(size_text(bytes), 12.0f, colours.dim)))),
      c::shown_for<audio_shown>([plays](const audio_shown& now) { return plays(now) && now.playing; },
                                c::styled({.place = scene::anchor::kTopLeft, .width = 0.0f, .height = 0.0f},
                                          c::every<request::audio_tick>(200.0))));
}
using file_row_t = decltype(file_row(std::declval<const palette&>(), std::string(), std::string(), 0, false));
inline auto file_view(const palette& colours, std::string source, std::string name, std::int64_t bytes, bool sound, file_open open) {
  std::string label = name;
  return skiff::compose::onClick(std::move(open), file_row(colours, std::move(source), std::move(name), bytes, sound), std::move(label));
}
using file_view_t = decltype(file_view(std::declval<const palette&>(), std::string(), std::string(), 0, false, file_open{}));

} // namespace mux::ui
