// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.encryption.ui -- An emoji verification's dialog, as Element
// shows it, among the window's (contract::window_dialogs): with whom, and
// where it is -- asked of you (Accept, Decline), waiting on them (Cancel),
// the 7 emoji to compare (They match, They don't match), over (OK).
module;
#include "../../inline_in_release.h"
#include "skiff/named.h"
export module mux.plugin.encryption.ui;

export import :page;

import std;
import splice;
import mux.composition;
import skia;
import skiff.scene;
import skiff.compose;
import skiff.bind;
import skiff.model;
import skiff.nodes.box;
import skiff.nodes.flow;
import skiff.nodes.text;
import mux.core;
import mux.ui.base;
import mux.plugin.encryption.types;

export namespace mux::plugins::encryption {

using namespace ::mux::ui;

// A line of it: wrapped, selectable, as wide as the box.
[[nodiscard]] MUX_INLINE_IN_RELEASE auto verification_line(std::string text, float size, skia::SkColor colour, bool bold = false) {
  return skiff::compose::styled({.fillX = true}, wrapped(nodes::Text(std::move(text), size, colour, bold, true)));
}
// One emoji, big, its name under it.
[[nodiscard]] MUX_INLINE_IN_RELEASE auto verification_emoji(const palette& colours, std::string_view picture, std::string_view name) {
  using skiff::compose::styled;
  return skiff::compose::column(skiff::compose::vbox(4.0f, {.width = 52.0f, .autoSize = scene::axes::kY}),
                                styled({.alignSelf = scene::align::kMiddle}, nodes::Text(std::string(picture), 30.0f, colours.text)),
                                styled({.fillX = true, .alignSelf = scene::align::kMiddle}, wrapped(nodes::Text(std::string(name), 11.0f, colours.dim))));
}
using verification_emoji_t = decltype(verification_emoji(std::declval<const palette&>(), {}, {}));
// The seven to compare wrap in their original order on narrow screens.
[[nodiscard]] inline nodes::Flow<std::vector<verification_emoji_t>> emoji_row(const palette& colours, const verification_step_t& step) {
  auto cells = spl::visit(spl::overloaded{
      [&](const verification_step::compare& shown) {
        return shown.emoji | std::views::transform([&](int index) {
          const auto& [picture, name] = sas_emoji[static_cast<std::size_t>(index & 63)];
          return verification_emoji(colours, picture, name);
        }) | std::ranges::to<std::vector>();
      }, [](const auto&) { return std::vector<verification_emoji_t>{}; }}, step);
  const bool comparing = !cells.empty();
  return skiff::compose::visible(comparing, skiff::compose::styled(
      {.fillX = true, .autoSize = scene::axes::kY},
      nodes::Flow<std::vector<verification_emoji_t>>({.direction = nodes::direction::horizontal{}, .spacingX = 8.0f, .spacingY = 12.0f, .wrap = true}, std::move(cells))));
}

// Which of a verification's buttons its step shows.
[[nodiscard]] inline bool in_step(const verification_step_t& step, auto shows) { return spl::visit(shows, step); }
[[nodiscard]] inline bool step_asks(const verification_step_t& step) {
  return in_step(step, spl::overloaded{[](verification_step::asked) { return true; }, [](const auto&) { return false; }});
}
[[nodiscard]] inline bool step_waits(const verification_step_t& step) {
  return in_step(step, spl::overloaded{[](verification_step::asked) { return true; }, [](verification_step::waiting) { return true; },
                                       [](const auto&) { return false; }});
}
[[nodiscard]] inline bool step_compares(const verification_step_t& step) {
  return in_step(step, spl::overloaded{[](const verification_step::compare&) { return true; }, [](const auto&) { return false; }});
}
[[nodiscard]] inline bool step_over(const verification_step_t& step) {
  return in_step(step, spl::overloaded{[](verification_step::done) { return true; }, [](const verification_step::cancelled&) { return true; },
                                       [](const auto&) { return false; }});
}

// An emoji verification, as Element shows it: with whom, and where it is --
// asked of you (Accept, Decline), waiting on them (Cancel), the 7 emoji to
// compare (They match, They don't match), over (OK). What shows is what
// its step says.
// What the verification says, under who it is with: as its step goes.
[[nodiscard]] inline std::string verification_note(const verification_view& view) {
  const std::string device = view.device.empty() ? std::string("one of their devices") : "device " + view.device;
  return spl::visit(
      spl::overloaded{
          [&](verification_step::asked) { return std::format("{} ({}) asks to verify with emoji.", view.user, device); },
          [&](verification_step::waiting) { return std::format("Waiting for {} ({})\u2026", view.user, device); },
          [&](const verification_step::compare&) {
            return std::string("Compare these emoji with the ones on the other screen, in the same order. Only if all of "
                               "them match, say so.");
          },
          [&](verification_step::done) { return std::format("Verified: {} ({}).", view.user, device); },
          [&](const verification_step::cancelled& why) { return "Not verified: " + why.reason; }},
      view.step);
}
// A button of it: at the end, as wide as said, there where its step has it.
template <class Button>
[[nodiscard]] auto verification_answer(bool shown, float width, Button button) {
  return skiff::compose::visible(shown, skiff::compose::styled({.width = width, .height = 34.0f, .alignSelf = scene::align::kEnd}, std::move(button)));
}
// The box, made from the verification as it is now: made again as its
// step moves on.
MUX_INLINE_IN_RELEASE auto verification_box_of(const palette& colours, const verification_view& view) {
  return SKIFF_NAMED(verification_box_of, skiff::compose::column(
      skiff::compose::vbox(10.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {20.0f, 22.0f, 20.0f, 22.0f}}),
      verification_line("Verify " + view.user, 17.0f, colours.text, true), verification_line(verification_note(view), 14.0f, colours.dim),
      emoji_row(colours, view.step),
      verification_answer(step_asks(view.step), 120.0f, primary(button_for<sends<verify_accept_now>>(colours.widgets, "Accept", {}))),
      verification_answer(step_waits(view.step), 120.0f,
          button_for<sends<verify_cancel_now>>(colours.widgets, step_asks(view.step) ? "Decline" : "Cancel", {})),
      verification_answer(step_compares(view.step), 160.0f, primary(button_for<sends<verify_match>>(colours.widgets, "They match", {}))),
      verification_answer(step_compares(view.step), 160.0f, button_for<sends<verify_mismatch>>(colours.widgets, "They don't match", {})),
      verification_answer(step_over(view.step), 90.0f, primary(button_for<sends<::mux::ui::request::close_shown<verification_view>>>(colours.widgets, "OK", {})))));
}
using verification_box = decltype(verification_box_of(std::declval<const palette&>(), std::declval<const verification_view&>()));
// The window's dialog of it: as wide as it fits, up to 440; not put away
// by a press off it -- the verification says when it is over.
inline dialog_look content_look(std::type_identity<verification_view>) { return {.size = dialog_size::fitting{440.0f}, .dismissable = false}; }
template <class Needs>
auto make_content(const Needs& needs, const verification_view& view) {
  return verification_box_of(*needs.colours, view);
}

// Its dialog, among the window's (mux.ui:window).
constexpr type_tag<verification_box> extend(contract::window_dialogs, type_tag<feature>) { return {}; }

}  // namespace mux::plugins::encryption
