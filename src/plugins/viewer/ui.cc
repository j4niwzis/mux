// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.viewer.ui -- The media viewer: a picture or a video seen whole,
// over the window (contract::window_layers).
module;
#include "../../inline_in_release.h"
#include "skiff/named.h"
export module mux.plugin.viewer.ui;

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
import mux.composition;
import skiff.bind;
import mux.plugin.viewer.types;

export namespace mux::plugins::viewer {

using namespace ::mux::ui;

// A picture seen whole, as tdesktop's media viewer -- but over this window,
// not in one of its own: dark behind; at the top, who sent it and when, and
// the buttons -- zoom out and in, save, close; the picture fitted in the
// rest, zoomed by the wheel or the buttons, dragged about when larger than
// the room. A press on the dark around it closes it; the whole picture
// replaces its thumbnail when it has come.
// The viewer's menu let go.
struct viewer_menu_closed {};
// The viewer's menu, where a right press on the picture opened it: the
// picture copied, or saved. Over all of the viewer, a press anywhere else
// closing it and doing nothing more.
MUX_INLINE_IN_RELEASE auto viewer_menu(const palette& colours, const std::string& source, float x, float y) {
  namespace c = skiff::compose;
  const auto item = [&](auto asked, std::string label) {
    return c::onClick(std::tuple{asked, viewer_menu_closed{}},
        c::column(c::vbox(0.0f, {.fillX = true, .height = 33.0f, .padding = {0.0f, 17.0f, 0.0f, 17.0f}, .hoverBackground = colours.chosen}),
                  c::styled({.alignSelf = scene::align::kMiddle}, nodes::Text(label, 13.0f, colours.text))),
        label);
  };
  return SKIFF_NAMED(viewer_menu, c::onClick(viewer_menu_closed{},
      c::column(c::vbox(0.0f, {.fill = true}),
          c::column(c::vbox(0.0f, {.place = scene::anchor::kTopLeft, .x = x, .y = y, .width = 200.0f, .autoSize = scene::axes::kY,
                                   .padding = {6.0f, 0.0f, 6.0f, 0.0f}, .cornerRadius = 10.0f, .background = colours.sidebar,
                                   .border = scene::Border{colours.band, 1.0f}, .shadow = scene::Shadow{skia::colorSetARGB(70, 0, 0, 0), 3.0f}}),
                    item(request::copy_picture{source}, "Copy Image"), item(request::save_picture{source}, "Save As\u2026")))));
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
// The frames of the video playing, by its source: the program's.
[[nodiscard]] inline std::map<std::string, skia::Sp<skia::SkImage>, std::less<>>& video_frames() {
  static std::map<std::string, skia::Sp<skia::SkImage>, std::less<>> frames;
  return frames;
}
// The viewer's picture, woken as a frame of its video comes, or its whole
// picture, or its thumbnail: drawn again then, by itself.
[[nodiscard]] inline skiff::scene::Waiters& viewer_waiters() {
  static skiff::scene::Waiters waiting;
  static const bool told = [] {
    whole_pictures().also = &waiting;
    thumbnails().also = &waiting;
    return true;
  }();
  (void)told;
  return waiting;
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
  // Woken as a frame of its video comes, or its picture.
  skiff::scene::Waiters& waiters() const { return viewer_waiters(); }
};
[[nodiscard]] inline std::string clock_of_seconds(double seconds) {
  const auto whole = static_cast<std::int64_t>(seconds);
  return std::format("{}:{:02}", whole / 60, whole % 60);
}
// Seeking: where along its bar a video was pressed.
struct seek_to {
  using Answer = video_seek;
  video_seek operator()(float share) const { return {share}; }
};
// A picture or a video seen whole, as tdesktop's media viewer -- over this
// window, dark behind; at the top who sent it and when, and zoom out and
// in, save, close; the picture fitted in the rest, zoomed by the wheel or
// the buttons, dragged about when larger than the room; a press on the dark
// around it closes it, one on a video pauses or plays it, a right press on
// it its menu. Under a video, how far it has played (pressed, there) and
// its time. Its zoom, place and menu its own; the rest the program's.
MUX_INLINE_IN_RELEASE auto picture_viewer(const ui_needs& n, const viewer_facts& facts) {
  namespace c = skiff::compose;
  const palette& colours = *n.colours;
  const skia::SkColor white = skia::colorSetARGB(255, 255, 255, 255);
  const scene::Spec middle{.alignSelf = scene::align::kMiddle};
  // Its loader, while the whole picture or the video's file comes: pressed,
  // its download stopped or asked again -- a picture's by the client's own
  // request, a video's by this plugin's.
  const auto loader_of = [&]<class Pressed>(std::type_identity<Pressed>, bool here, Pressed pressed) {
    return c::visible(here, c::shown_for<viewer_progress>([](const viewer_progress& now) { return now.load.coming; },
        c::applied<viewer_progress>([](const viewer_progress& now) { return now.load; },
            [](widgets::RadialLoader<sends<Pressed>>& turning, const viewer_load& now) {
              turning.setProgress(now.progress);
              turning.setStopped(now.stopped);
            },
            c::styled({.place = scene::anchor::kCentre}, widgets::RadialLoader<sends<Pressed>>(44.0f, {std::move(pressed)})))));
  };
  auto loader = c::column(c::vbox(0.0f, {.place = scene::anchor::kTopLeft, .fill = true}),
      loader_of(std::type_identity<request::press_loader>{}, facts.video.empty(), request::press_loader{facts.source}),
      loader_of(std::type_identity<press_video_loader>{}, !facts.video.empty(), press_video_loader{facts.video}));
  auto menu = c::bound<skiff::model::Field<&viewer_view::menu>>(c::mount<decltype(viewer_menu(colours, facts.source, 0.0f, 0.0f)), std::pair<float, float>>(
      [at = &colours, source = facts.source](const std::pair<float, float>& where) {
        return std::tuple{viewer_menu(*at, source, where.first, where.second)};
      },
      {.place = scene::anchor::kTopLeft, .fill = true}));
  return SKIFF_NAMED(picture_viewer, c::local<viewer_view>(viewer_events{},
      c::onKeys([](const scene::key::down& press) {
                  return press.key == scene::keys::kSpace ? std::optional(toggle_video{}) : std::nullopt;
                },
          c::column(c::vbox(0.0f, {.fill = true, .background = skia::colorSetARGB(0xe6, 0, 0, 0)}),
              c::row(c::hbox(8.0f, {.fillX = true, .height = 56.0f, .padding = {0.0f, 12.0f, 0.0f, 16.0f}}),
                     face_spot(avatar_of::person{}, facts.sender, facts.name, 36.0f),
                     two_lines(colours, facts.name, facts.when, 14.0f, 2.0f,
                               two_line_style{.first_ink = white, .second_ink = skia::colorSetARGB(255, 200, 200, 200)}),
                     c::styled({.height = 1.0f, .grow = scene::axes::kX}, nodes::Box<>(skia::SkColor{0})),
                     c::styled(middle, icon_button(colours, icon::minus{}, sends<skiff::compose::ZoomBy<viewer_tag>>{{1.0f / 1.25f}}, white)),
                     c::styled(middle, icon_button(colours, icon::plus{}, sends<skiff::compose::ZoomBy<viewer_tag>>{{1.25f}}, white)),
                     c::styled(middle, icon_button(colours, icon::download{}, sends<request::save_picture>{{facts.source}}, white)),
                     c::styled(middle, icon_button(colours, icon::close{}, sends<::mux::ui::request::close_shown<viewer_facts>>{}, white))),
              c::column(c::vbox(0.0f, {.fillX = true, .grow = scene::axes::kY}),
                  c::zoomable_for<viewer_view, viewer_tag>([](const viewer_view& now) { return skiff::compose::ZoomView{now.zoom, now.x, now.y}; },
                      48.0f, ::mux::ui::request::close_shown<viewer_facts>{}, toggle_video{},
                      nodes::Image<viewer_picture>({facts.source, facts.video}),
                      {.fill = true, .masking = true}),
                  std::move(loader), std::move(menu)),
              c::shown_for<viewer_progress>([](const viewer_progress& now) { return now.video.length > 0.0; },
                  c::row(c::hbox(12.0f, {.fillX = true, .height = 44.0f, .padding = {0.0f, 20.0f, 0.0f, 20.0f}}),
                         c::applied<viewer_progress>([](const viewer_progress& now) { return now.video.length > 0.0 ? static_cast<float>(now.video.at / now.video.length) : 0.0f; },
                             [](widgets::SliderBar<seek_to>& bar, float share) { bar.setFraction(share); },
                             c::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle}, widgets::SliderBar<seek_to>(colours.widgets, seek_to{}))),
                         c::text_for<viewer_progress>([](const viewer_progress& now) { return clock_of_seconds(now.video.at) + " / " + clock_of_seconds(now.video.length); },
                             c::styled(middle, nodes::Text("", 13.0f, white)))))))));
}
using picture_viewer_t = decltype(picture_viewer(std::declval<const ui_needs&>(), std::declval<const viewer_facts&>()));

struct make_viewer {
  auto operator()(const ui_needs& n, const viewer_facts& facts) const { return picture_viewer(n, facts); }
};
// Its layer, over the window: there while a picture is open in it.
MUX_INLINE_IN_RELEASE auto extend(contract::window_layers, type_tag<feature>, const ui_needs& needs) {
  return layer_made_by<viewer_facts, make_viewer>(needs);
}

}  // namespace mux::plugins::viewer
