// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:icons -- The icons, drawn with a pen.
export module mux.ui:icons;

import std;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes.icon;
import mux.core;
import mux.config;
import :base;

export namespace mux::ui {

// ---- icons and rows ---------------------------------------------------------------

// The icons, drawn with a pen rather than taken from a font: each its own
// type, drawn by its own overload.
namespace icon {
struct none {};
struct person {};
struct gear {};
struct power {};
struct plus {};
struct motion {};
struct back {};
struct close {};
struct info {};
struct people {};
struct add_person {};
struct bell {};
struct sliders {};
struct leave {};
struct check {};
struct clip {};
struct send {};
struct eye {};
struct minus {};
struct search {};  // a magnifier
struct up {};      // a chevron up
struct down {};    // a chevron down
struct download {};
struct compass {};  // Explore rooms
struct phone {};   // a handset: a call
struct hang_up {};  // the handset turned down: a call ended
struct microphone {};
struct microphone_off {};  // the microphone, crossed out: muted
struct threads {};  // a room's threads  // an arrow down onto a line: saved to the disk
struct reply {};   // tdesktop's historyReplyIcon: an arrow turned back
struct pencil {};  // tdesktop's historyEditIcon
struct smile {};   // tdesktop's historyEmojiIcon: a round face
struct play {};    // a triangle, pointing on
struct pause {};   // two bars
// A filled dot of a colour of its own, as a proxy profile's.
struct dot {
  skia::SkColor colour;
};
}  // namespace icon
using icon_t = spl::variant<icon::none, icon::person, icon::gear, icon::power, icon::plus, icon::motion, icon::back,
                            icon::close, icon::info, icon::people, icon::add_person, icon::bell, icon::sliders,
                            icon::leave, icon::check, icon::clip, icon::send, icon::eye, icon::dot, icon::minus,
                            icon::reply, icon::pencil, icon::search, icon::up, icon::down, icon::smile,
                            icon::play, icon::pause, icon::download, icon::compass, icon::threads, icon::phone,
                            icon::hang_up, icon::microphone, icon::microphone_off>;

// Each icon's shape, as data, in points from the middle of its box -- about
// 20 across -- for nodes::Icon to draw.
using nodes::IconShape;
using nodes::Mark;
namespace marks = nodes::mark;
namespace steps = nodes::path_step;

[[nodiscard]] inline IconShape shape_of(icon::none) { return {}; }
// A handset, filled, as the call buttons of Telegram and Element draw it:
// the earpiece at the upper left, the mouthpiece at the lower right, the
// grip curving between them.
[[nodiscard]] inline IconShape shape_of(icon::phone) {
  return {{{marks::path{{steps::move{-5.38f, -1.21f},
                         steps::cubic{-3.94f, 1.62f, -1.62f, 3.93f, 1.21f, 5.38f},
                         steps::line{3.41f, 3.18f},
                         steps::cubic{3.68f, 2.91f, 4.08f, 2.82f, 4.43f, 2.94f},
                         steps::cubic{5.55f, 3.31f, 6.76f, 3.51f, 8.00f, 3.51f},
                         steps::cubic{8.55f, 3.51f, 9.00f, 3.96f, 9.00f, 4.51f},
                         steps::line{9.00f, 8.00f},
                         steps::cubic{9.00f, 8.55f, 8.55f, 9.00f, 8.00f, 9.00f},
                         steps::cubic{-1.39f, 9.00f, -9.00f, 1.39f, -9.00f, -8.00f},
                         steps::cubic{-9.00f, -8.55f, -8.55f, -9.00f, -8.00f, -9.00f},
                         steps::line{-4.50f, -9.00f},
                         steps::cubic{-3.95f, -9.00f, -3.50f, -8.55f, -3.50f, -8.00f},
                         steps::cubic{-3.50f, -6.75f, -3.30f, -5.55f, -2.93f, -4.43f},
                         steps::cubic{-2.82f, -4.08f, -2.90f, -3.69f, -3.18f, -3.41f},
                         steps::line{-5.38f, -1.21f},
                         steps::close{}}},
            0.0f, true}}};
}
// The handset turned down, as Element's hang-up button has it.
[[nodiscard]] inline IconShape shape_of(icon::hang_up) {
  IconShape out = shape_of(icon::phone{});
  out.rotation = 135.0f;
  return out;
}
// A microphone: its head, the holder round it, its stem and foot.
[[nodiscard]] inline IconShape shape_of(icon::microphone) {
  return {{{marks::rect{-3.2f, -9.0f, 3.2f, 3.0f, 3.2f}, 1.8f},
           {marks::arc{-6.5f, -4.5f, 6.5f, 6.5f, 0.0f, 180.0f}, 1.8f},
           {marks::line{0.0f, 6.5f, 0.0f, 9.0f}, 1.8f},
           {marks::line{-3.5f, 9.0f, 3.5f, 9.0f}, 1.8f}}};
}
[[nodiscard]] inline IconShape shape_of(icon::microphone_off) {
  IconShape out = shape_of(icon::microphone{});
  out.marks.push_back({marks::line{-8.0f, -8.0f, 8.0f, 8.0f}, 1.8f});
  return out;
}
[[nodiscard]] inline IconShape shape_of(icon::person) {
  return {{{marks::circle{0.0f, -4.0f, 3.8f}}, {marks::arc{-7.5f, 2.0f, 7.5f, 17.0f, 180.0f, 180.0f}}}};
}
[[nodiscard]] inline IconShape shape_of(icon::gear) {
  IconShape out{{{marks::circle{0.0f, 0.0f, 2.8f}}, {marks::circle{0.0f, 0.0f, 6.5f}}}};
  for (int k = 0; k < 8; ++k) {
    const float a = static_cast<float>(k) * std::numbers::pi_v<float> / 4.0f;
    out.marks.push_back({marks::line{6.5f * std::cos(a), 6.5f * std::sin(a), 9.0f * std::cos(a), 9.0f * std::sin(a)}, 2.4f});
  }
  return out;
}
[[nodiscard]] inline IconShape shape_of(icon::power) {
  return {{{marks::arc{-7.5f, -6.5f, 7.5f, 8.5f, 300.0f, 300.0f}}, {marks::line{0.0f, -8.5f, 0.0f, -1.0f}}}};
}
[[nodiscard]] inline IconShape shape_of(icon::plus) {
  return {{{marks::line{-7.0f, 0.0f, 7.0f, 0.0f}, 2.0f}, {marks::line{0.0f, -7.0f, 0.0f, 7.0f}, 2.0f}}};
}
[[nodiscard]] inline IconShape shape_of(icon::motion) {
  return {{{marks::circle{3.5f, 0.0f, 4.5f}}, {marks::line{-9.0f, -3.5f, -3.5f, -3.5f}}, {marks::line{-7.0f, 3.5f, -3.5f, 3.5f}}}};
}
[[nodiscard]] inline IconShape shape_of(icon::back) {
  return {{{marks::line{7.0f, 0.0f, -7.0f, 0.0f}, 2.0f},
           {marks::line{-7.0f, 0.0f, -1.5f, -5.5f}, 2.0f},
           {marks::line{-7.0f, 0.0f, -1.5f, 5.5f}, 2.0f}}};
}
[[nodiscard]] inline IconShape shape_of(icon::close) {
  return {{{marks::line{-6.0f, -6.0f, 6.0f, 6.0f}, 2.0f}, {marks::line{-6.0f, 6.0f, 6.0f, -6.0f}, 2.0f}}};
}
[[nodiscard]] inline IconShape shape_of(icon::info) {
  return {{{marks::circle{0.0f, 0.0f, 8.5f}}, {marks::line{0.0f, -1.0f, 0.0f, 4.5f}, 2.0f}, {marks::circle{0.0f, -4.5f, 0.6f}, 2.2f}}};
}
[[nodiscard]] inline IconShape shape_of(icon::people) {
  return {{{marks::circle{-2.5f, -4.0f, 3.2f}, 1.6f},
           {marks::arc{-9.0f, 2.0f, 4.0f, 14.0f, 180.0f, 180.0f}, 1.6f},
           {marks::circle{4.5f, -3.0f, 2.6f}, 1.6f},
           {marks::arc{1.0f, 2.5f, 9.5f, 12.0f, 200.0f, 140.0f}, 1.6f}}};
}
[[nodiscard]] inline IconShape shape_of(icon::add_person) {
  return {{{marks::circle{-3.0f, -4.0f, 3.4f}, 1.6f},
           {marks::arc{-10.0f, 2.0f, 4.0f, 15.0f, 180.0f, 180.0f}, 1.6f},
           {marks::line{6.0f, -6.0f, 6.0f, 0.0f}, 1.6f},
           {marks::line{3.0f, -3.0f, 9.0f, -3.0f}, 1.6f}}};
}
[[nodiscard]] inline IconShape shape_of(icon::bell) {
  return {{{marks::arc{-6.0f, -8.0f, 6.0f, 4.0f, 180.0f, 180.0f}, 1.7f},
           {marks::line{-6.0f, -2.0f, -7.5f, 4.0f}, 1.7f},
           {marks::line{6.0f, -2.0f, 7.5f, 4.0f}, 1.7f},
           {marks::line{-7.5f, 4.0f, 7.5f, 4.0f}, 1.7f},
           {marks::arc{-2.5f, 4.5f, 2.5f, 9.0f, 0.0f, 180.0f}, 1.7f}}};
}
[[nodiscard]] inline IconShape shape_of(icon::sliders) {
  return {{{marks::line{-8.0f, -4.0f, 8.0f, -4.0f}, 1.7f},
           {marks::line{-8.0f, 4.0f, 8.0f, 4.0f}, 1.7f},
           {marks::circle{-3.0f, -4.0f, 2.4f}, 1.7f},
           {marks::circle{3.5f, 4.0f, 2.4f}, 1.7f}}};
}
[[nodiscard]] inline IconShape shape_of(icon::leave) {
  return {{{marks::line{-2.0f, -8.0f, -8.0f, -8.0f}, 1.7f},
           {marks::line{-8.0f, -8.0f, -8.0f, 8.0f}, 1.7f},
           {marks::line{-8.0f, 8.0f, -2.0f, 8.0f}, 1.7f},
           {marks::line{-3.0f, 0.0f, 8.0f, 0.0f}, 1.7f},
           {marks::line{8.0f, 0.0f, 4.5f, -3.5f}, 1.7f},
           {marks::line{8.0f, 0.0f, 4.5f, 3.5f}, 1.7f}}};
}
[[nodiscard]] inline IconShape shape_of(icon::check) {
  return {{{marks::line{-5.0f, 0.0f, -1.5f, 4.0f}, 2.2f}, {marks::line{-1.5f, 4.0f, 6.0f, -4.5f}, 2.2f}}};
}
[[nodiscard]] inline IconShape shape_of(icon::clip) {
  return {{{marks::rect{-3.5f, -10.0f, 3.5f, 8.0f, 3.5f}, 1.7f}, {marks::line{0.0f, -5.5f, 0.0f, 4.0f}, 1.7f}}, 45.0f};
}
// Telegram's: a paper plane, filled, pointing right.
[[nodiscard]] inline IconShape shape_of(icon::send) {
  return {{{marks::path{{steps::move{-9.0f, -8.5f}, steps::line{10.0f, 0.0f}, steps::line{-9.0f, 8.5f},
                         steps::line{-7.0f, 1.5f}, steps::line{2.0f, 0.0f}, steps::line{-7.0f, -1.5f}, steps::close{}}},
            0.0f, true}}};
}
[[nodiscard]] inline IconShape shape_of(icon::eye) {
  return {{{marks::arc{-10.0f, -7.0f, 10.0f, 11.0f, 200.0f, 140.0f}, 1.7f},
           {marks::arc{-10.0f, -11.0f, 10.0f, 7.0f, 20.0f, 140.0f}, 1.7f},
           {marks::circle{0.0f, 0.0f, 3.0f}, 1.7f}}};
}
// A filled dot of a colour of its own, as a proxy profile's.
[[nodiscard]] inline IconShape shape_of(icon::dot which) {
  return {{{marks::circle{0.0f, 0.0f, 6.0f}, 0.0f, true, which.colour}}};
}
[[nodiscard]] inline IconShape shape_of(icon::minus) { return {{{marks::line{-7.0f, 0.0f, 7.0f, 0.0f}, 2.0f}}}; }
// tdesktop's reply: an arrow pointing left, its shaft bending down to the right.
[[nodiscard]] inline IconShape shape_of(icon::reply) {
  return {{{marks::path{{steps::move{-3.0f, -8.0f}, steps::line{-9.0f, -2.5f}, steps::line{-3.0f, 3.0f}}}, 2.0f},
           {marks::path{{steps::move{-9.0f, -2.5f}, steps::line{1.0f, -2.5f},
                         steps::cubic{6.0f, -2.5f, 9.0f, 1.0f, 9.0f, 8.0f}}},
            2.0f}}};
}
[[nodiscard]] inline IconShape shape_of(icon::pencil) {
  return {{{marks::rect{-2.5f, -10.0f, 2.5f, 5.0f}, 1.8f},
           {marks::line{-2.5f, -6.5f, 2.5f, -6.5f}, 1.8f},
           {marks::path{{steps::move{-2.5f, 5.0f}, steps::line{0.0f, 9.5f}, steps::line{2.5f, 5.0f}}}, 1.8f}},
          45.0f};
}
// Threads, as Element's: a bubble, its tail at the lower left, two lines
// said in it.
[[nodiscard]] inline IconShape shape_of(icon::threads) {
  return {{{marks::path{{steps::move{-8.0f, -6.0f}, steps::line{8.0f, -6.0f}, steps::line{8.0f, 5.0f},
                         steps::line{-3.0f, 5.0f}, steps::line{-7.0f, 8.5f}, steps::line{-7.0f, 5.0f},
                         steps::line{-8.0f, 5.0f}, steps::close{}}},
            1.6f},
           {marks::line{-4.5f, -2.0f, 4.5f, -2.0f}, 1.6f},
           {marks::line{-4.5f, 1.5f, 2.0f, 1.5f}, 1.6f}}};
}
// Explore rooms, as Element's: a compass -- a ring, its needle across it.
[[nodiscard]] inline IconShape shape_of(icon::compass) {
  return {{{marks::circle{0.0f, 0.0f, 8.0f}, 1.8f},
           {marks::path{{steps::move{4.2f, -4.2f}, steps::line{1.2f, 1.2f}, steps::line{-4.2f, 4.2f},
                         steps::line{-1.2f, -1.2f}, steps::close{}}},
            0.0f, true}}};
}
[[nodiscard]] inline IconShape shape_of(icon::search) {
  return {{{marks::circle{-2.0f, -2.0f, 6.5f}, 2.0f}, {marks::line{2.8f, 2.8f, 8.0f, 8.0f}, 2.0f}}};
}
[[nodiscard]] inline IconShape shape_of(icon::up) {
  return {{{marks::line{-6.0f, 3.0f, 0.0f, -3.0f}, 2.0f}, {marks::line{0.0f, -3.0f, 6.0f, 3.0f}, 2.0f}}};
}
[[nodiscard]] inline IconShape shape_of(icon::down) {
  return {{{marks::line{-6.0f, -3.0f, 0.0f, 3.0f}, 2.0f}, {marks::line{0.0f, 3.0f, 6.0f, -3.0f}, 2.0f}}};
}
// tdesktop's media viewer's: an arrow down onto a line.
[[nodiscard]] inline IconShape shape_of(icon::download) {
  return {{{marks::line{0.0f, -9.0f, 0.0f, 4.0f}, 2.0f},
           {marks::path{{steps::move{-5.5f, -1.5f}, steps::line{0.0f, 4.0f}, steps::line{5.5f, -1.5f}}}, 2.0f},
           {marks::line{-8.0f, 9.0f, 8.0f, 9.0f}, 2.0f}}};
}
// A face: its round, two eyes, a smile.
[[nodiscard]] inline IconShape shape_of(icon::smile) {
  return {{{marks::circle{0.0f, 0.0f, 9.0f}, 1.7f},
           {marks::circle{-3.3f, -2.5f, 1.2f}, 0.0f, true},
           {marks::circle{3.3f, -2.5f, 1.2f}, 0.0f, true},
           {marks::arc{-5.0f, -4.0f, 5.0f, 5.5f, 20.0f, 140.0f}, 1.7f}}};
}
[[nodiscard]] inline IconShape shape_of(icon::play) {
  return {{{marks::path{{steps::move{-4.5f, -7.0f}, steps::line{7.5f, 0.0f}, steps::line{-4.5f, 7.0f}, steps::close{}}},
            0.0f, true}}};
}
[[nodiscard]] inline IconShape shape_of(icon::pause) {
  return {{{marks::path{{steps::move{-5.0f, -6.5f}, steps::line{-1.5f, -6.5f}, steps::line{-1.5f, 6.5f},
                         steps::line{-5.0f, 6.5f}, steps::close{}}},
            0.0f, true},
           {marks::path{{steps::move{1.5f, -6.5f}, steps::line{5.0f, -6.5f}, steps::line{5.0f, 6.5f},
                         steps::line{1.5f, 6.5f}, steps::close{}}},
            0.0f, true}}};
}
[[nodiscard]] inline IconShape shape_of(const icon_t& which) {
  return spl::visit([](auto one) { return shape_of(one); }, which);
}

// Whether an icon draws anything: all but none.
[[nodiscard]] constexpr bool drawn(icon::none) { return false; }
[[nodiscard]] constexpr bool drawn(const auto&) { return true; }
}  // namespace mux::ui
