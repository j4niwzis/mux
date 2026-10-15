// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:viewer -- The picture viewer.
export module mux.ui:viewer;

import std;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.compose;
import skiff.nodes.box;
import skiff.nodes.flow;
import skiff.nodes.image;
import skiff.widgets.loader;
import skiff.widgets.sliderbar;
import skiff.model;
import mux.platform.video;
import mux.core;
import mux.config;
import mux.ui.base;

export namespace mux::ui {

// A picture seen whole, as tdesktop's media viewer -- but over this window,
// not in one of its own: dark behind; at the top, who sent it and when, and
// the buttons -- zoom out and in, save, close; the picture fitted in the
// rest, zoomed by the wheel or the buttons, dragged about when larger than
// the room. A press on the dark around it closes it; the whole picture
// replaces its thumbnail when it has come.
// A picture or a video open: its source (a video's thumbnail), who sent
// it, its name, when; and a video's own source, played once it is there.
struct viewer_facts {
  std::string source;
  std::string sender;
  std::string name;
  std::string when;
  std::string video;
  // The video's file, once it is there: played.
  std::optional<std::filesystem::path> file;
};
// The viewer's menu let go.
struct viewer_menu_closed {};
// The viewer's menu, where a right press on the picture opened it: the
// picture copied, or saved. Over all of the viewer, a press anywhere else
// closing it and doing nothing more.
inline auto viewer_menu(const palette& colours, const std::string& source, float x, float y) {
  namespace c = skiff::compose;
  const auto item = [&](auto asked, std::string label) {
    return c::onClick(std::tuple{asked, viewer_menu_closed{}},
        c::column(c::vbox(0.0f, {.fillX = true, .height = 33.0f, .padding = {0.0f, 17.0f, 0.0f, 17.0f}, .hoverBackground = colours.chosen}),
                  c::styled({.alignSelf = scene::align::kMiddle}, nodes::Text(label, 13.0f, colours.text))),
        label);
  };
  return c::onClick(viewer_menu_closed{},
      c::column(c::vbox(0.0f, {.fill = true}),
          c::column(c::vbox(0.0f, {.place = scene::anchor::kTopLeft, .x = x, .y = y, .width = 200.0f, .autoSize = scene::axes::kY,
                                   .padding = {6.0f, 0.0f, 6.0f, 0.0f}, .cornerRadius = 10.0f, .background = colours.sidebar,
                                   .border = scene::Border{colours.band, 1.0f}, .shadow = scene::Shadow{skia::colorSetARGB(70, 0, 0, 0), 3.0f}}),
                    item(request::copy_picture{source}, "Copy Image"), item(request::save_picture{source}, "Save As\u2026"))));
}
// The viewer's own state, as its parts change it: how far zoomed and moved,
// and its menu, where a right press on the picture opened it.
struct viewer_tag {};
struct viewer_view {
  float zoom = 1.0f, x = 0.0f, y = 0.0f;
  std::optional<std::pair<float, float>> menu;
  friend bool operator==(const viewer_view&, const viewer_view&) = default;
};
struct viewer_events {
  auto on(const skiff::compose::ZoomBy<viewer_tag>& by, const viewer_view& now) const {
    viewer_view next = now;
    next.zoom = std::clamp(now.zoom * by.factor, 1.0f, 8.0f);
    if (next.zoom == 1.0f)
      next.x = next.y = 0.0f;
    return skiff::model::over<viewer_view>(skiff::model::setTo(std::move(next)));
  }
  auto on(const skiff::compose::PanBy<viewer_tag>& by, const viewer_view& now) const {
    viewer_view next = now;
    next.x += by.dx;
    next.y += by.dy;
    return skiff::model::over<viewer_view>(skiff::model::setTo(std::move(next)));
  }
  auto on(const skiff::compose::MenuAt<viewer_tag>& at, const viewer_view& now) const {
    viewer_view next = now;
    next.menu = std::pair{at.x, at.y};
    return skiff::model::over<viewer_view>(skiff::model::setTo(std::move(next)));
  }
  auto on(const viewer_menu_closed&, const viewer_view& now) const {
    viewer_view next = now;
    next.menu.reset();
    return skiff::model::over<viewer_view>(skiff::model::setTo(std::move(next)));
  }
};
// What is coming, as the program has it: the whole picture, or a video's
// file -- how far, and whether its download was stopped.
struct viewer_load {
  bool coming = false;
  std::optional<float> progress;
  bool stopped = false;
  friend bool operator==(const viewer_load&, const viewer_load&) = default;
};
// A video playing in the viewer, as the program plays it: its frame (a
// number, newer as one comes), where it is, how long.
struct video_shown {
  std::string source;
  bool playing = false;
  double at = 0.0, length = 0.0;
  std::uint64_t frame = 0;
  friend bool operator==(const video_shown&, const video_shown&) = default;
};
// The frames of the video playing, by its source: the program's.
[[nodiscard]] inline std::map<std::string, skia::Sp<skia::SkImage>, std::less<>>& video_frames() {
  static std::map<std::string, skia::Sp<skia::SkImage>, std::less<>> frames;
  return frames;
}
// The picture shown: a video's frame where it plays, else the whole
// picture where it has come, else its thumbnail.
struct viewer_picture {
  std::string source, video;
  const skia::Sp<skia::SkImage>* operator()() const {
    if (const auto frame = video_frames().find(video); !video.empty() && frame != video_frames().end() && frame->second)
      return &frame->second;
    const skia::Sp<skia::SkImage>* one = whole_pictures().find(source);
    return one && *one ? one : thumbnails().find(source);
  }
};
[[nodiscard]] inline std::string clock_of_seconds(double seconds) {
  const auto whole = static_cast<std::int64_t>(seconds);
  return std::format("{}:{:02}", whole / 60, whole % 60);
}
// Seeking: where along its bar a video was pressed.
struct seek_to {
  using Answer = request::video_seek;
  request::video_seek operator()(float share) const { return {share}; }
};
// A picture or a video seen whole, as tdesktop's media viewer -- over this
// window, dark behind; at the top who sent it and when, and zoom out and
// in, save, close; the picture fitted in the rest, zoomed by the wheel or
// the buttons, dragged about when larger than the room; a press on the dark
// around it closes it, one on a video pauses or plays it, a right press on
// it its menu. Under a video, how far it has played (pressed, there) and
// its time. Its zoom, place and menu its own; the rest the program's.
inline auto picture_viewer(const ui_needs& n, const viewer_facts& facts) {
  namespace c = skiff::compose;
  const palette& colours = *n.colours;
  const skia::SkColor white = skia::colorSetARGB(255, 255, 255, 255);
  const scene::Spec middle{.alignSelf = scene::align::kMiddle};
  auto loader = c::shown_for<viewer_load>([](const viewer_load& now) { return now.coming; },
      c::applied<viewer_load>([](const viewer_load& now) { return now; },
          [](widgets::RadialLoader<sends<request::press_loader>>& turning, const viewer_load& now) {
            turning.setProgress(now.progress);
            turning.setStopped(now.stopped);
          },
          c::styled({.place = scene::anchor::kCentre},
                    widgets::RadialLoader<sends<request::press_loader>>(44.0f, {{facts.video.empty() ? facts.source : facts.video}}))));
  auto menu = c::bound<skiff::model::Field<&viewer_view::menu>>(c::mount<decltype(viewer_menu(colours, facts.source, 0.0f, 0.0f)), std::pair<float, float>>(
      [at = &colours, source = facts.source](const std::pair<float, float>& where) {
        return std::tuple{viewer_menu(*at, source, where.first, where.second)};
      },
      {.place = scene::anchor::kTopLeft, .fill = true}));
  return c::local<viewer_view>(viewer_events{},
      c::onKeys([](const scene::key::down& press) {
                  return press.key == scene::keys::kSpace ? std::optional(request::toggle_video{}) : std::nullopt;
                },
          c::column(c::vbox(0.0f, {.fill = true, .background = skia::colorSetARGB(0xe6, 0, 0, 0)}),
              c::row(c::hbox(8.0f, {.fillX = true, .height = 56.0f, .padding = {0.0f, 12.0f, 0.0f, 16.0f}}),
                     avatar_mark(avatar_of::person{}, facts.sender, facts.name, 36.0f),
                     two_lines(colours, facts.name, facts.when, 14.0f, 2.0f,
                               two_line_style{.first_ink = white, .second_ink = skia::colorSetARGB(255, 200, 200, 200)}),
                     c::styled({.height = 1.0f, .grow = scene::axes::kX}, nodes::Box<>(skia::SkColor{0})),
                     c::styled(middle, icon_button(colours, icon::minus{}, sends<skiff::compose::ZoomBy<viewer_tag>>{{1.0f / 1.25f}}, white)),
                     c::styled(middle, icon_button(colours, icon::plus{}, sends<skiff::compose::ZoomBy<viewer_tag>>{{1.25f}}, white)),
                     c::styled(middle, icon_button(colours, icon::download{}, sends<request::save_picture>{{facts.source}}, white)),
                     c::styled(middle, icon_button(colours, icon::close{}, sends<request::close_picture>{}, white))),
              c::column(c::vbox(0.0f, {.fillX = true, .grow = scene::axes::kY}),
                  c::zoomable_for<viewer_view, viewer_tag>([](const viewer_view& now) { return skiff::compose::ZoomView{now.zoom, now.x, now.y}; },
                      48.0f, request::close_picture{}, request::toggle_video{},
                      c::applied<video_shown>([](const video_shown& now) { return now.frame; },
                          [](nodes::Image<viewer_picture>& picture, std::uint64_t) { picture.markDamaged(); },
                          nodes::Image<viewer_picture>({facts.source, facts.video})),
                      {.fill = true, .masking = true}),
                  std::move(loader), std::move(menu)),
              c::shown_for<video_shown>([](const video_shown& now) { return now.length > 0.0; },
                  c::row(c::hbox(12.0f, {.fillX = true, .height = 44.0f, .padding = {0.0f, 20.0f, 0.0f, 20.0f}}),
                         c::applied<video_shown>([](const video_shown& now) { return now.length > 0.0 ? static_cast<float>(now.at / now.length) : 0.0f; },
                             [](widgets::SliderBar<seek_to>& bar, float share) { bar.setFraction(share); },
                             c::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle}, widgets::SliderBar<seek_to>(colours.widgets, seek_to{}))),
                         c::text_for<video_shown>([](const video_shown& now) { return clock_of_seconds(now.at) + " / " + clock_of_seconds(now.length); },
                             c::styled(middle, nodes::Text("", 13.0f, white))))))));
}
using picture_viewer_t = decltype(picture_viewer(std::declval<const ui_needs&>(), std::declval<const viewer_facts&>()));

}  // namespace mux::ui

