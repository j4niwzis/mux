// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.avatars.ui_top -- A chat's picture beside its row's text in the
// list: a rule on the text piece (mux.ui.lists' chat_row_text), around it --
// the two side by side, as the row had them when it drew its own.
export module mux.plugin.avatars.ui_top;

import std;
import skiff.scene;
import skiff.compose;
import mux.core;
import mux.composition;
import mux.ui.base;
import mux.ui.lists;
// The message bubbles lines, what a face is put beside.
import mux.ui.messages;
import mux.plugin.avatars.types;

export namespace mux::plugins::avatars {

using namespace ::mux::ui;

// A search result's: who said it (its facts the sender's).
struct found_picture {
  auto operator()(auto text, const found_row_facts& f) const {
    namespace c = skiff::compose;
    return c::row(c::hbox(10.0f, {.autoSize = scene::axes::kY, .grow = scene::axes::kX, .alignSelf = scene::align::kMiddle}),
                  c::styled({.alignSelf = scene::align::kMiddle}, avatar_mark(avatar_of::person{}, f.sender, f.name, 40.0f)), std::move(text));
  }
};
// A picture of `size`, then the text, `gap` apart: a row that grows across,
// as the text did.
template <class Kind>
struct picture_beside {
  float size;
  float gap;
  auto operator()(auto text, const auto& facts) const {
    namespace c = skiff::compose;
    return c::row(c::hbox(gap, {.autoSize = scene::axes::kY, .grow = scene::axes::kX, .alignSelf = scene::align::kMiddle}),
                  c::styled({.alignSelf = scene::align::kMiddle}, avatar_mark(Kind{}, facts.id, facts.name, size)), std::move(text));
  }
};
// A sender's face beside their bubble's line, as the core drew it: the line
// moved in by the face's room, the face hung at its bottom left in that room
// -- shown on a run's last, at the looks' opacity; pressed, their card.
struct face_beside {
  auto operator()(auto line, const sender_face_facts& f) const {
    namespace c = skiff::compose;
    const float room = f.with_face ? kBubbleAvatar + 8.0f : 0.0f;
    return c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 0.0f, 0.0f, room}}),
                     std::move(line),
                     c::visible(f.with_face, c::styled({.place = scene::anchor::kBottomLeft, .x = -(kBubbleAvatar + 8.0f), .y = -1.0f,
                                                        .alpha = f.shown ? f.opacity : 0.0f},
                                                       c::onPress(open_face{f.sender, f.shown},
                                                                  avatar_mark(avatar_of::person{}, f.sender, f.name, kBubbleAvatar), f.name))));
  }
};
// A picture of `size` to press (opening it), then the text, `gap` apart:
// a row that grows across, as the text did.
template <class Kind>
struct button_beside {
  float size;
  float gap;
  auto operator()(auto text, const auto& facts) const {
    namespace c = skiff::compose;
    return c::row(c::hbox(gap, {.autoSize = scene::axes::kY, .grow = scene::axes::kX, .alignSelf = scene::align::kMiddle}),
                  c::styled({.alignSelf = scene::align::kMiddle}, avatar_button(Kind{}, facts.id, facts.name, size)), std::move(text));
  }
};
// A chat's or a person's big picture above their heading, in the middle;
// pressed, the picture opened.
template <class Kind>
struct picture_above {
  float size;
  auto operator()(auto heading, const auto& facts) const {
    namespace c = skiff::compose;
    return c::column(c::vbox(2.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                     c::styled({.alignSelf = scene::align::kMiddle}, avatar_button(Kind{}, facts.id, facts.name, size)), std::move(heading));
  }
};
// Who read a message, in the menu: their faces at the right of what it
// says, out of the flow in the room kept for them -- the first the
// rightmost, each next overlapping it a little to its left, ringed.
struct seen_faces {
  auto operator()(auto said, const seen_faces_facts& f) const {
    namespace c = skiff::compose;
    const std::size_t shown = f.faces.size();
    const float step = kSeenFace - kSeenOverlap;
    const float width = shown ? kSeenFace + static_cast<float>(shown - 1) * step : 0.0f;
    auto faces = std::views::iota(std::size_t{0}, shown) | std::views::transform([&](std::size_t i) {
                   return c::styled({.place = scene::anchor::kCentreRight, .x = width + 8.0f - static_cast<float>(i) * step,
                                     .border = scene::Border{f.ring, 2.0f}},
                                    avatar_mark(avatar_of::person{}, f.faces[i].id, f.faces[i].name, kSeenFace));
                 }) | std::ranges::to<std::vector>();
    return c::row(c::hbox(0.0f, {.fill = true, .padding = {0.0f, shown ? width + 8.0f : 0.0f, 0.0f, 0.0f}}),
                  std::move(said), c::many(c::hbox(), std::move(faces)));
  }
};
// The chat list's chats, the search's results, the drawer's accounts, who
// read a message, a directory's rooms, a notification's chat: each its
// picture beside its text.
constexpr auto shape_rules(contract::shapes, type_tag<feature>) {
  using namespace skiff::compose;
  return on<chat_row_text_fn>(around(picture_beside<avatar_of::chat>{46.0f, 12.0f})) |
         on<found_row_text_fn>(around(found_picture{})) |
         on<drawer_account_text_fn>(around(picture_beside<avatar_of::person>{38.0f, 14.0f})) |
         on<reader_text_fn>(around(picture_beside<avatar_of::person>{30.0f, 14.0f})) |
         on<directory_room_text_fn>(around(picture_beside<avatar_of::chat>{40.0f, 12.0f})) |
         on<bubble_piece_fn>(around(face_beside{})) |
         on<toast_text_fn>(around(picture_beside<avatar_of::chat>{44.0f, 12.0f})) |
         on<chat_heading_fn>(around(picture_above<avatar_of::chat>{96.0f})) |
         on<member_text_fn>(around(picture_beside<avatar_of::person>{40.0f, 12.0f})) |
         on<person_pick_text_fn>(around(picture_beside<avatar_of::person>{36.0f, 12.0f})) |
         on<seen_summary_fn>(around(seen_faces{})) |
         on<person_cover_text_fn>(around(button_beside<avatar_of::person>{72.0f, 16.0f}));
}

}  // namespace mux::plugins::avatars
