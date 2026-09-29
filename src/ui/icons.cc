// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:icons -- The icons, drawn with a pen.
export module mux.ui:icons;

import std;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes;
import skiff.widgets;
import mux.core;
import mux.config;
export import :base;

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
struct reply {};   // tdesktop's historyReplyIcon: an arrow turned back
struct pencil {};  // tdesktop's historyEditIcon
// A filled dot of a colour of its own, as a proxy profile's.
struct dot {
  skia::SkColor colour;
};
}  // namespace icon
using icon_t = std::variant<icon::none, icon::person, icon::gear, icon::power, icon::plus, icon::motion, icon::back,
                            icon::close, icon::info, icon::people, icon::add_person, icon::bell, icon::sliders,
                            icon::leave, icon::check, icon::clip, icon::send, icon::eye, icon::dot, icon::minus,
                            icon::reply, icon::pencil, icon::search, icon::up, icon::down>;

[[nodiscard]] inline skia::SkPaint pen(skia::SkColor colour, float alpha, float width = 1.8f) {
  skia::SkPaint out;
  out.setAntiAlias(true);
  out.setColor(colour);
  out.setAlphaf(alpha);
  out.setStyle(skia::kStrokeStyle);
  out.setStrokeWidth(width);
  out.setStrokeCap(skia::kRoundCap);
  return out;
}

// Each in a box, centred in it, about 20 points across.
inline void draw_icon(skia::SkCanvas*, icon::none, const skia::SkRect&, skia::SkColor, float) {}
inline void draw_icon(skia::SkCanvas* canvas, icon::person, const skia::SkRect& box, skia::SkColor colour, float alpha) {
  const auto p = pen(colour, alpha);
  const float x = box.centerX(), y = box.centerY();
  canvas->drawCircle(x, y - 4.0f, 3.8f, p);
  canvas->drawArc(skia::SkRect::MakeLTRB(x - 7.5f, y + 2.0f, x + 7.5f, y + 17.0f), 180.0f, 180.0f, false, p);
}
inline void draw_icon(skia::SkCanvas* canvas, icon::gear, const skia::SkRect& box, skia::SkColor colour, float alpha) {
  const auto p = pen(colour, alpha);
  const float x = box.centerX(), y = box.centerY();
  canvas->drawCircle(x, y, 2.8f, p);
  canvas->drawCircle(x, y, 6.5f, p);
  for (int k = 0; k < 8; ++k) {
    const float a = static_cast<float>(k) * std::numbers::pi_v<float> / 4.0f;
    canvas->drawLine(x + 6.5f * std::cos(a), y + 6.5f * std::sin(a), x + 9.0f * std::cos(a), y + 9.0f * std::sin(a),
                     pen(colour, alpha, 2.4f));
  }
}
inline void draw_icon(skia::SkCanvas* canvas, icon::power, const skia::SkRect& box, skia::SkColor colour, float alpha) {
  const auto p = pen(colour, alpha);
  const float x = box.centerX(), y = box.centerY() + 1.0f;
  canvas->drawArc(skia::SkRect::MakeLTRB(x - 7.5f, y - 7.5f, x + 7.5f, y + 7.5f), 300.0f, 300.0f, false, p);
  canvas->drawLine(x, y - 9.5f, x, y - 2.0f, p);
}
inline void draw_icon(skia::SkCanvas* canvas, icon::plus, const skia::SkRect& box, skia::SkColor colour, float alpha) {
  const auto p = pen(colour, alpha, 2.0f);
  const float x = box.centerX(), y = box.centerY();
  canvas->drawLine(x - 7.0f, y, x + 7.0f, y, p);
  canvas->drawLine(x, y - 7.0f, x, y + 7.0f, p);
}
inline void draw_icon(skia::SkCanvas* canvas, icon::motion, const skia::SkRect& box, skia::SkColor colour, float alpha) {
  const auto p = pen(colour, alpha);
  const float x = box.centerX(), y = box.centerY();
  canvas->drawCircle(x + 3.5f, y, 4.5f, p);
  canvas->drawLine(x - 9.0f, y - 3.5f, x - 3.5f, y - 3.5f, p);
  canvas->drawLine(x - 7.0f, y + 3.5f, x - 3.5f, y + 3.5f, p);
}
inline void draw_icon(skia::SkCanvas* canvas, icon::back, const skia::SkRect& box, skia::SkColor colour, float alpha) {
  const auto p = pen(colour, alpha, 2.0f);
  const float x = box.centerX(), y = box.centerY();
  canvas->drawLine(x + 7.0f, y, x - 7.0f, y, p);
  canvas->drawLine(x - 7.0f, y, x - 1.5f, y - 5.5f, p);
  canvas->drawLine(x - 7.0f, y, x - 1.5f, y + 5.5f, p);
}
inline void draw_icon(skia::SkCanvas* canvas, icon::close, const skia::SkRect& box, skia::SkColor colour, float alpha) {
  const auto p = pen(colour, alpha, 2.0f);
  const float x = box.centerX(), y = box.centerY();
  canvas->drawLine(x - 6.0f, y - 6.0f, x + 6.0f, y + 6.0f, p);
  canvas->drawLine(x - 6.0f, y + 6.0f, x + 6.0f, y - 6.0f, p);
}
inline void draw_icon(skia::SkCanvas* canvas, icon::info, const skia::SkRect& box, skia::SkColor colour, float alpha) {
  const auto p = pen(colour, alpha);
  const float x = box.centerX(), y = box.centerY();
  canvas->drawCircle(x, y, 8.5f, p);
  canvas->drawLine(x, y - 1.0f, x, y + 4.5f, pen(colour, alpha, 2.0f));
  canvas->drawCircle(x, y - 4.5f, 0.6f, pen(colour, alpha, 2.2f));
}
inline void draw_icon(skia::SkCanvas* canvas, icon::people, const skia::SkRect& box, skia::SkColor colour, float alpha) {
  const auto p = pen(colour, alpha, 1.6f);
  const float x = box.centerX() - 2.5f, y = box.centerY();
  canvas->drawCircle(x, y - 4.0f, 3.2f, p);
  canvas->drawArc(skia::SkRect::MakeLTRB(x - 6.5f, y + 2.0f, x + 6.5f, y + 14.0f), 180.0f, 180.0f, false, p);
  canvas->drawCircle(x + 7.0f, y - 3.0f, 2.6f, p);
  canvas->drawArc(skia::SkRect::MakeLTRB(x + 3.5f, y + 2.5f, x + 12.0f, y + 12.0f), 200.0f, 140.0f, false, p);
}
inline void draw_icon(skia::SkCanvas* canvas, icon::add_person, const skia::SkRect& box, skia::SkColor colour,
                      float alpha) {
  const auto p = pen(colour, alpha, 1.6f);
  const float x = box.centerX() - 3.0f, y = box.centerY();
  canvas->drawCircle(x, y - 4.0f, 3.4f, p);
  canvas->drawArc(skia::SkRect::MakeLTRB(x - 7.0f, y + 2.0f, x + 7.0f, y + 15.0f), 180.0f, 180.0f, false, p);
  canvas->drawLine(x + 9.0f, y - 6.0f, x + 9.0f, y + 0.0f, p);
  canvas->drawLine(x + 6.0f, y - 3.0f, x + 12.0f, y - 3.0f, p);
}
inline void draw_icon(skia::SkCanvas* canvas, icon::bell, const skia::SkRect& box, skia::SkColor colour, float alpha) {
  const auto p = pen(colour, alpha, 1.7f);
  const float x = box.centerX(), y = box.centerY();
  canvas->drawArc(skia::SkRect::MakeLTRB(x - 6.0f, y - 8.0f, x + 6.0f, y + 4.0f), 180.0f, 180.0f, false, p);
  canvas->drawLine(x - 6.0f, y - 2.0f, x - 7.5f, y + 4.0f, p);
  canvas->drawLine(x + 6.0f, y - 2.0f, x + 7.5f, y + 4.0f, p);
  canvas->drawLine(x - 7.5f, y + 4.0f, x + 7.5f, y + 4.0f, p);
  canvas->drawArc(skia::SkRect::MakeLTRB(x - 2.5f, y + 4.5f, x + 2.5f, y + 9.0f), 0.0f, 180.0f, false, p);
}
inline void draw_icon(skia::SkCanvas* canvas, icon::sliders, const skia::SkRect& box, skia::SkColor colour, float alpha) {
  const auto p = pen(colour, alpha, 1.7f);
  const float x = box.centerX(), y = box.centerY();
  canvas->drawLine(x - 8.0f, y - 4.0f, x + 8.0f, y - 4.0f, p);
  canvas->drawLine(x - 8.0f, y + 4.0f, x + 8.0f, y + 4.0f, p);
  canvas->drawCircle(x - 3.0f, y - 4.0f, 2.4f, p);
  canvas->drawCircle(x + 3.5f, y + 4.0f, 2.4f, p);
}
inline void draw_icon(skia::SkCanvas* canvas, icon::leave, const skia::SkRect& box, skia::SkColor colour, float alpha) {
  const auto p = pen(colour, alpha, 1.7f);
  const float x = box.centerX(), y = box.centerY();
  canvas->drawLine(x - 2.0f, y - 8.0f, x - 8.0f, y - 8.0f, p);
  canvas->drawLine(x - 8.0f, y - 8.0f, x - 8.0f, y + 8.0f, p);
  canvas->drawLine(x - 8.0f, y + 8.0f, x - 2.0f, y + 8.0f, p);
  canvas->drawLine(x - 3.0f, y, x + 8.0f, y, p);
  canvas->drawLine(x + 8.0f, y, x + 4.5f, y - 3.5f, p);
  canvas->drawLine(x + 8.0f, y, x + 4.5f, y + 3.5f, p);
}
inline void draw_icon(skia::SkCanvas* canvas, icon::check, const skia::SkRect& box, skia::SkColor colour, float alpha) {
  const auto p = pen(colour, alpha, 2.2f);
  const float x = box.centerX(), y = box.centerY();
  canvas->drawLine(x - 5.0f, y, x - 1.5f, y + 4.0f, p);
  canvas->drawLine(x - 1.5f, y + 4.0f, x + 6.0f, y - 4.5f, p);
}
inline void draw_icon(skia::SkCanvas* canvas, icon::clip, const skia::SkRect& box, skia::SkColor colour, float alpha) {
  const auto p = pen(colour, alpha, 1.7f);
  const float x = box.centerX(), y = box.centerY();
  const int save = canvas->save();
  canvas->translate(x, y);
  canvas->rotate(45.0f);
  canvas->drawRoundRect(skia::SkRect::MakeLTRB(-3.5f, -10.0f, 3.5f, 8.0f), 3.5f, 3.5f, p);
  canvas->drawLine(0.0f, -5.5f, 0.0f, 4.0f, p);
  canvas->restoreToCount(save);
}
inline void draw_icon(skia::SkCanvas* canvas, icon::send, const skia::SkRect& box, skia::SkColor colour, float alpha) {
  // Telegram's: a paper plane, filled, pointing right.
  skia::SkPaint fill;
  fill.setAntiAlias(true);
  fill.setColor(colour);
  fill.setAlphaf(alpha);
  const float x = box.centerX(), y = box.centerY();
  skia::SkPathBuilder plane;
  plane.moveTo(x - 9.0f, y - 8.5f);
  plane.lineTo(x + 10.0f, y);
  plane.lineTo(x - 9.0f, y + 8.5f);
  plane.lineTo(x - 7.0f, y + 1.5f);
  plane.lineTo(x + 2.0f, y);
  plane.lineTo(x - 7.0f, y - 1.5f);
  plane.close();
  canvas->drawPath(plane.detach(), fill);
}
inline void draw_icon(skia::SkCanvas* canvas, icon::eye, const skia::SkRect& box, skia::SkColor colour, float alpha) {
  const auto p = pen(colour, alpha, 1.7f);
  const float x = box.centerX(), y = box.centerY();
  canvas->drawArc(skia::SkRect::MakeLTRB(x - 10.0f, y - 7.0f, x + 10.0f, y + 11.0f), 200.0f, 140.0f, false, p);
  canvas->drawArc(skia::SkRect::MakeLTRB(x - 10.0f, y - 11.0f, x + 10.0f, y + 7.0f), 20.0f, 140.0f, false, p);
  canvas->drawCircle(x, y, 3.0f, p);
}
inline void draw_icon(skia::SkCanvas* canvas, icon::dot which, const skia::SkRect& box, skia::SkColor, float alpha) {
  skia::SkPaint fill;
  fill.setAntiAlias(true);
  fill.setColor(which.colour);
  fill.setAlphaf(alpha);
  canvas->drawCircle(box.centerX(), box.centerY(), 6.0f, fill);
}
inline void draw_icon(skia::SkCanvas* canvas, icon::minus, const skia::SkRect& box, skia::SkColor colour, float alpha) {
  const float x = box.centerX(), y = box.centerY();
  canvas->drawLine(x - 7.0f, y, x + 7.0f, y, pen(colour, alpha, 2.0f));
}
inline void draw_icon(skia::SkCanvas* canvas, icon::reply, const skia::SkRect& box, skia::SkColor colour, float alpha) {
  // An arrow pointing left, its shaft bending down to the right.
  const auto p = pen(colour, alpha, 2.0f);
  const float x = box.centerX(), y = box.centerY();
  skia::SkPathBuilder arrow;
  arrow.moveTo(x - 3.0f, y - 8.0f);
  arrow.lineTo(x - 9.0f, y - 2.5f);
  arrow.lineTo(x - 3.0f, y + 3.0f);
  canvas->drawPath(arrow.detach(), p);
  skia::SkPathBuilder shaft;
  shaft.moveTo(x - 9.0f, y - 2.5f);
  shaft.lineTo(x + 1.0f, y - 2.5f);
  shaft.cubicTo(x + 6.0f, y - 2.5f, x + 9.0f, y + 1.0f, x + 9.0f, y + 8.0f);
  canvas->drawPath(shaft.detach(), p);
}
inline void draw_icon(skia::SkCanvas* canvas, icon::pencil, const skia::SkRect& box, skia::SkColor colour, float alpha) {
  const auto p = pen(colour, alpha, 1.8f);
  const int save = canvas->save();
  canvas->translate(box.centerX(), box.centerY());
  canvas->rotate(45.0f);
  canvas->drawRect(skia::SkRect::MakeLTRB(-2.5f, -10.0f, 2.5f, 5.0f), p);
  canvas->drawLine(-2.5f, -6.5f, 2.5f, -6.5f, p);
  skia::SkPathBuilder tip;
  tip.moveTo(-2.5f, 5.0f);
  tip.lineTo(0.0f, 9.5f);
  tip.lineTo(2.5f, 5.0f);
  canvas->drawPath(tip.detach(), p);
  canvas->restoreToCount(save);
}
inline void draw_icon(skia::SkCanvas* canvas, icon::search, const skia::SkRect& box, skia::SkColor colour, float alpha) {
  const auto p = pen(colour, alpha, 2.0f);
  const float x = box.centerX() - 2.0f, y = box.centerY() - 2.0f;
  canvas->drawCircle(x, y, 6.5f, p);
  canvas->drawLine(x + 4.8f, y + 4.8f, x + 10.0f, y + 10.0f, p);
}
inline void draw_icon(skia::SkCanvas* canvas, icon::up, const skia::SkRect& box, skia::SkColor colour, float alpha) {
  const auto p = pen(colour, alpha, 2.0f);
  const float x = box.centerX(), y = box.centerY();
  canvas->drawLine(x - 6.0f, y + 3.0f, x, y - 3.0f, p);
  canvas->drawLine(x, y - 3.0f, x + 6.0f, y + 3.0f, p);
}
inline void draw_icon(skia::SkCanvas* canvas, icon::down, const skia::SkRect& box, skia::SkColor colour, float alpha) {
  const auto p = pen(colour, alpha, 2.0f);
  const float x = box.centerX(), y = box.centerY();
  canvas->drawLine(x - 6.0f, y - 3.0f, x, y + 3.0f, p);
  canvas->drawLine(x, y + 3.0f, x + 6.0f, y - 3.0f, p);
}
// Whether an icon draws anything: all but none.
[[nodiscard]] constexpr bool drawn(icon::none) { return false; }
[[nodiscard]] constexpr bool drawn(const auto&) { return true; }
inline void draw_icon(skia::SkCanvas* canvas, const icon_t& which, const skia::SkRect& box, skia::SkColor colour,
                      float alpha) {
  std::visit([&](auto one) { draw_icon(canvas, one, box, colour, alpha); }, which);
}

}  // namespace mux::ui
