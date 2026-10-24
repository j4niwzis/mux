// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:sending -- Files being sent.
module;
#include "skiff/named.h"
export module mux.ui:sending;

import std;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.compose;
import skiff.nodes.flow;
import skiff.nodes.image;
import skiff.nodes.scroll;
import skiff.nodes.text;
import skiff.widgets.button;
import skiff.widgets.textarea;
import skiff.widgets.model;
import skiff.model;
import mux.core;
import mux.config;
import mux.ui.base;
import mux.ui.messages;
import :context_menu;

export namespace mux::ui {

// What is about to be sent, as tdesktop's send box shows it: its title, a
// picture scaled to the box or a file's row for each, a caption, and Cancel
// and Send.
struct pending_file {
  std::string name;
  std::string key;  // its picture's, where it is one: its local id, in thumbnails()
  std::int64_t size = 0;
  bool image = false;
};
// The files about to be sent, shown before they go.
struct send_facts {
  std::vector<pending_file> files;
};
// What is being written for them, the box's own until it sends: a caption,
// sent with the first.
struct send_draft {
  std::string caption;
  friend bool operator==(const send_draft&, const send_draft&) = default;
};
inline constexpr float kSendGap = 8.0f, kMostSendPreviews = 420.0f;
// A picture's size in the box: its own, scaled down to fit -- lower where
// there are several.
[[nodiscard]] inline std::pair<float, float> send_size_of(const pending_file& one, std::size_t count) {
  const skia::Sp<skia::SkImage>* image = thumbnails().find(one.key);
  const float w = image && *image ? static_cast<float>((*image)->width()) : 380.0f;
  const float h = image && *image ? static_cast<float>((*image)->height()) : 240.0f;
  const float scale = std::min({1.0f, 380.0f / w, (count > 1 ? 160.0f : 300.0f) / h});
  return {w * scale, h * scale};
}
// As high as what it holds: the pictures' heights and the files' rows, with
// the gaps between -- so the box is as high as it needs.
[[nodiscard]] inline float send_height_of(const std::vector<pending_file>& all) {
  return std::ranges::fold_left(all | std::views::transform([&](const pending_file& one) {
                                  return one.image ? send_size_of(one, all.size()).second : kFileIcon;
                                }),
                                all.empty() ? 0.0f : static_cast<float>(all.size() - 1) * kSendGap, std::plus{});
}
// The pictures' part of the box in a window of `high`: what they need, 420
// at most, and never more than the window leaves the box -- 90% of it, as
// the dialog has at most -- under its title, caption and buttons (about 220
// with their gaps). With a phone's keyboard up, the window is half as high,
// and pictures as high as before pushed Cancel and Send out of the box.
inline constexpr float kSendChrome = 220.0f, kLeastSendPreviews = 80.0f;
[[nodiscard]] inline float send_room_of(float needed, float high) {
  return std::min({needed, kMostSendPreviews, std::max(kLeastSendPreviews, high * 0.9f - kSendChrome)});
}
// What a box of these says it sends.
[[nodiscard]] inline std::string send_title_of(const std::vector<pending_file>& all) {
  return all.size() == 1 ? std::string(all.front().image ? "Send a photo" : "Send a file")
                         : std::format("Send {} {}", all.size(), std::ranges::all_of(all, &pending_file::image) ? "photos" : "files");
}
// The box: its title, each picture as it will look (rounded, its thumbnail
// by its local id) and each file's row -- scrolling past 420, which keeps
// the caption and buttons in view on a laptop's screen, where tdesktop's
// sendMediaPreviewHeightMax (1280) is more than a window -- the caption,
// and Cancel and Send: the caption sent with them as it is then.
inline auto send_box(const ui_needs& n, const send_facts& facts) {
  namespace c = skiff::compose;
  const palette& colours = *n.colours;
  const auto& all = facts.files;
  auto pictures = all | std::views::filter(&pending_file::image) | std::views::transform([&](const pending_file& one) {
                    const auto [w, h] = send_size_of(one, all.size());
                    return c::styled({.width = w, .height = h, .alignSelf = scene::align::kMiddle, .cornerRadius = 10.0f},
                                     nodes::Image<from_thumbnails>({one.key}));
                  }) | std::ranges::to<std::vector>();
  auto files = all | std::views::filter(std::not_fn(&pending_file::image)) | std::views::transform([&](const pending_file& one) {
                 return file_row(colours, std::string(), one.name, one.size, false);
               }) | std::ranges::to<std::vector>();
  return SKIFF_NAMED(send_box, c::local<send_draft>(
      c::column(c::vbox(12.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {18.0f, 20.0f, 16.0f, 20.0f}}),
          nodes::Text(send_title_of(all), 17.0f, colours.text, true),
          c::spec_for<window_shape>([needed = send_height_of(all)](const window_shape& now) {
            return scene::Spec{.fillX = true, .height = send_room_of(needed, now.height)};
          },
                    nodes::ScrollContainer(c::column(c::vbox(kSendGap, {.fillX = true, .autoSize = scene::axes::kY}),
                                                     c::many(c::vbox(kSendGap, {.fillX = true, .autoSize = scene::axes::kY}), std::move(pictures)),
                                                     c::many(c::vbox(kSendGap, {.fillX = true, .autoSize = scene::axes::kY}), std::move(files))))),
          c::bound<skiff::model::Field<&send_draft::caption>>(
              c::styled({.fillX = true}, widgets::TextAreaField<std::string>(colours.widgets, "Add a caption\u2026", 3))),
          c::row(c::justified(c::hbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY, .margin = {8.0f, 0.0f, 0.0f, 0.0f}}), nodes::justify::end{}),
                 c::styled({.width = 96.0f, .height = 36.0f},
                           widgets::Button<sends<request::close_send_box>>(colours.widgets, "Cancel", {})),
                 c::asking_for<send_draft>([](const send_draft& draft) { return request::send_files{draft.caption}; },
                     c::styled({.width = 96.0f, .height = 36.0f},
                               primary(widgets::Button<scene::NoAction>(colours.widgets, "Send", {}))))))));
}
using send_box_t = decltype(send_box(std::declval<const ui_needs&>(), std::declval<const send_facts&>()));
// Sized as it is opened, by the files it is opened with.
inline dialog_look content_look(std::type_identity<send_facts>) { return {}; }
template <class Needs>
auto make_content(const Needs& needs, const send_facts& facts) {
  return send_box(needs, facts);
}

}  // namespace mux::ui

