// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.calls.ui -- A call, as Element shows one: in the chat it is in, a
// view of its own under the head -- the other's picture, their name, where
// the call is, and round buttons: the microphone, and hanging up; anywhere
// else, and while it rings here, a card at the top of the window, as
// Element's call toast -- Decline and Accept while it rings. A call ended
// stays a moment, saying why.
module;
#include "skiff/named.h"
export module mux.plugin.calls.ui;

import std;
import splice;
import mux.composition;
import skia;
import skiff.scene;
import skiff.compose;
import skiff.bind;
import skiff.model;
import skiff.nodes.box;
import skiff.nodes.icon;
import skiff.nodes.flow;
import skiff.nodes.text;
import skiff.widgets.avatar;
import mux.core;
import mux.ui.base;
import mux.plugin.calls.types;

export namespace mux::plugins::calls {
// What a call's views are made of: the window's own.
using namespace ::mux::ui;

[[nodiscard]] inline bool rings_here(const call_view& view) {
  return spl::visit(spl::overloaded{[](call_phase::ringing_in) { return true; },
                                    [](const auto &) { return false; }},
                    view.phase);
}
[[nodiscard]] inline bool has_ended(const call_view& view) {
  return spl::visit(
      spl::overloaded{[](const call_phase::ended &) { return true; },
                      [](const auto &) { return false; }},
      view.phase);
}
[[nodiscard]] inline std::string said_of(const call_view& view) {
  const std::string where = spl::visit(
      spl::overloaded{
          [](call_phase::ringing_in) {
            return std::string("Incoming voice call");
          },
          [](call_phase::ringing_out) { return std::string("Calling…"); },
          [](call_phase::connecting) { return std::string("Connecting…"); },
          [](const call_phase::connected &now) {
            return std::format("{}:{:02}", now.seconds / 60, now.seconds % 60);
          },
          [](const call_phase::ended &done) { return done.why; }},
      view.phase);
  if (has_ended(view))
    return where;
  if (!view.available)
    return where + " · calls aren't in this build";
  return view.encrypted ? where : where + " · not end-to-end encrypted";
}

// Only the timer moves every second. Its text is bound separately, keeping
// the buttons and their keyboard focus while the call's structure is same.
[[nodiscard]] inline std::optional<call_view>
call_structure(std::optional<call_view> view) {
  if (view)
    spl::visit(spl::overloaded{[](call_phase::connected &connected) {
                                 connected.seconds = 0;
                               },
                               [](auto &) {}},
               view->phase);
  return view;
}
struct call_words {
  std::string operator()(const call_shown &shown) const {
    return shown.now ? said_of(*shown.now) : std::string();
  }
};
using call_status_t = decltype(skiff::compose::text_of<call_shown>(
    call_words{}, nodes::Text("", 13.0f, 0u)));
struct call_card_of {
  std::optional<call_view> operator()(const call_shown &shown) const {
    return shown.now && !shown.now->whole &&
                   (!shown.now->in_view || rings_here(*shown.now))
               ? call_structure(shown.now)
               : std::nullopt;
  }
};
struct call_screen_of {
  std::optional<call_view> operator()(const call_shown &shown) const {
    return shown.now && shown.now->whole ? call_structure(shown.now)
                                         : std::nullopt;
  }
};
struct call_panel_of {
  std::optional<call_view> operator()(const call_shown &shown) const {
    return shown.now && !shown.now->whole && shown.now->in_view &&
                   !rings_here(*shown.now)
               ? call_structure(shown.now)
               : std::nullopt;
  }
};
// Element's call colours: hanging up and declining red, answering green.
inline constexpr skia::SkColor kHangUpRed = skia::colorSetARGB(255, 0xFF, 0x5B, 0x55);
inline constexpr skia::SkColor kAnswerGreen = skia::colorSetARGB(255, 0x0D, 0xBD, 0x8B);

inline scene::Spec call_button_look(float size, skia::SkColor plate, skia::SkColor hover) {
  return {.width = size, .height = size, .cornerRadius = size / 2.0f, .background = plate,
          .hoverBackground = hover, .focusBackground = hover};
}
template <class Event>
auto call_button(const palette& colours, float size, skia::SkColor plate, skia::SkColor hover,
                 skia::SkColor ink, icon_t mark, Event event, bool shown, std::string label) {
  return skiff::compose::visible(shown, skiff::compose::onClick(std::move(event),
      skiff::compose::styled(call_button_look(size, plate, hover), nodes::Icon(shape_of(mark), ink)), std::move(label)));
}
inline auto call_buttons(const palette& colours, float size, const call_view& view) {
  const auto white = skia::colorSetARGB(255, 255, 255, 255);
  const bool ringing = rings_here(view), ended = has_ended(view);
  return SKIFF_NAMED(call_buttons, skiff::compose::keep_ticking(ended, skiff::compose::row(
      skiff::compose::hbox(size / 2.0f, {.autoSize = scene::axes::kBoth, .alignSelf = scene::align::kMiddle}),
      call_button(colours, size, colours.tile, colours.chosen, colours.text,
                  view.muted ? icon_t{icon::microphone_off{}} : icon_t{icon::microphone{}},
                  mute_call{}, !ringing && !ended, view.muted ? "Unmute" : "Mute"),
      call_button(colours, size, kHangUpRed, kHangUpRed, white, icon::hang_up{}, decline_call{}, ringing, "Decline"),
      call_button(colours, size, kHangUpRed, kHangUpRed, white, icon::hang_up{}, hang_up{}, !ringing && !ended, "Hang up"),
      call_button(colours, size, kAnswerGreen, kAnswerGreen, white, icon::phone{}, accept_call{}, ringing && view.available, "Accept"),
      call_button(colours, size, kAnswerGreen, kAnswerGreen, white, icon::phone{}, start_call{view.in}, ended && view.available, "Call back"),
      call_button(colours, size, colours.tile, colours.chosen, colours.text, icon::close{}, dismiss_call{}, ended, "Dismiss"))));
}
inline auto call_avatar(const call_view& view, float size) {
  auto avatar = widgets::Avatar<from_avatars>(initials_of(view.who), size, picture_of(view.in.id), gradient_of(view.in.id));
  round_avatar(avatar, size, avatar_of::chat{});
  return SKIFF_NAMED(call_avatar, skiff::compose::styled({.alignSelf = scene::align::kMiddle}, std::move(avatar)));
}
// How it is going, read as it goes: wrapped or cut short as where it is
// shown asks -- said of the text itself, before it is read.
inline auto call_status(const palette& colours, const call_view& view, float size, bool wrap = false, bool elide = false) {
  auto text = nodes::Text(said_of(view), size, colours.dim);
  text.setWrapped(wrap);
  text.setElided(elide);
  return skiff::compose::text_of<call_shown>(call_words{}, std::move(text));
}
// The call in its chat, beneath the header.
inline auto call_panel(const palette& colours, const call_view& view) {
  return SKIFF_NAMED(call_panel, skiff::compose::column(
      skiff::compose::justified(skiff::compose::vbox(10.0f,
          {.fillX = true, .height = 280.0f, .padding = {20.0f, 16.0f, 20.0f, 16.0f},
           .background = colours.sidebar, .border = scene::Border{colours.band, 1.0f}}), nodes::justify::middle{}),
      call_avatar(view, 88.0f),
      skiff::compose::styled({.alignSelf = scene::align::kMiddle}, nodes::Text(view.who, 18.0f, colours.text, true)),
      skiff::compose::styled({.alignSelf = scene::align::kMiddle, .margin = {0.0f, 0.0f, 8.0f, 0.0f}}, call_status(colours, view, 13.0f)),
      call_buttons(colours, 52.0f, view)));
}
// On a phone the call fills the window and takes presses off its controls.
inline auto call_screen(const palette& colours, const call_view& view) {
  return SKIFF_NAMED(call_screen, skiff::compose::onClick(scene::Taken{}, skiff::compose::column(
      skiff::compose::vbox(12.0f, {.place = scene::anchor::kTopLeft, .fill = true,
                                 .padding = {24.0f, 24.0f, 48.0f, 24.0f}, .background = colours.sidebar}),
      skiff::compose::styled({.fillX = true, .height = 1.0f, .grow = scene::axes::kY}, nodes::Box<>(skia::SkColor{0})),
      call_avatar(view, 128.0f),
      skiff::compose::styled({.alignSelf = scene::align::kMiddle}, wrapped(nodes::Text(view.who, 24.0f, colours.text, true))),
      skiff::compose::styled({.alignSelf = scene::align::kMiddle}, call_status(colours, view, 15.0f, true)),
      skiff::compose::styled({.fillX = true, .height = 1.0f, .grow = scene::axes::kY}, nodes::Box<>(skia::SkColor{0})),
      call_buttons(colours, 64.0f, view))));
}
// A compact card above the conversation while the call rings elsewhere.
inline auto call_bar(const palette& colours, const call_view& view) {
  return SKIFF_NAMED(call_bar, skiff::compose::row(
      skiff::compose::hbox(12.0f, {.place = scene::anchor::kTopRight, .x = 12.0f, .y = 12.0f,
                                  .width = 360.0f, .autoSize = scene::axes::kY,
                                  .padding = {12.0f, 14.0f, 12.0f, 14.0f}, .cornerRadius = 12.0f,
                                  .background = colours.popup(), .border = scene::Border{colours.band, 1.0f},
                                  .shadow = scene::Shadow{skia::colorSetARGB(70, 0, 0, 0), 3.0f}}),
      call_avatar(view, 40.0f),
      skiff::compose::column(
          skiff::compose::vbox(2.0f, {.autoSize = scene::axes::kY, .grow = scene::axes::kX, .alignSelf = scene::align::kMiddle}),
          skiff::compose::styled({.fillX = true}, elided(nodes::Text(view.who, 15.0f, colours.text, true))),
          skiff::compose::styled({.fillX = true}, call_status(colours, view, 13.0f, false, true))),
      call_buttons(colours, 40.0f, view)));
}
namespace call_ui {
struct panel {};
struct card {};
struct screen {};
}
inline auto call_content(call_ui::panel, const palette& colours, const call_view& view) { return call_panel(colours, view); }
inline auto call_content(call_ui::card, const palette& colours, const call_view& view) { return call_bar(colours, view); }
inline auto call_content(call_ui::screen, const palette& colours, const call_view& view) { return call_screen(colours, view); }

template <class Which, class View>
[[nodiscard]] auto call_layer(const ui_needs& needs, Which which, View view,
                              scene::Spec spec, bool floats = false) {
  using Content = decltype(call_content(view, *needs.colours, call_view{}));
  return skiff::compose::derived<call_shown>(which, skiff::compose::mount<Content, call_view>(
      [colours = needs.colours, view](const call_view& facts) {
        return std::tuple{call_content(view, *colours, facts)};
      }, spec, floats));
}

// Its layers, over the window: the card, where the call rings or is in
// another chat; the whole window, on a phone.
inline auto extend(contract::window_layers, type_tag<feature>, const ui_needs& needs) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(extend, c::column(c::vbox(0.0f, {.place = scene::anchor::kTopLeft, .fill = true}),
      call_layer(needs, call_card_of{}, call_ui::card{}, {.fill = true}, true),
      call_layer(needs, call_screen_of{}, call_ui::screen{}, {.fill = true}, true)));
}
// Its view in its chat, under the head.
inline auto extend(contract::chat_column, type_tag<feature>, const ui_needs& needs) {
  return call_layer(needs, call_panel_of{}, call_ui::panel{}, {.fillX = true, .autoSize = scene::axes::kY});
}
// Its button in the chat's head: shown where the chat shown can be called,
// as its part keeps it (call_shown::callable).
inline auto extend(contract::chat_header, type_tag<feature>, const palette& colours) {
  return SKIFF_NAMED(extend, skiff::compose::shown_if<call_shown>([](const call_shown& now) { return now.callable; },
      // Hidden until what is shown says otherwise: a head made anew is not
      // read before what is shown next moves.
      skiff::compose::visible(false, skiff::compose::styled({.alignSelf = scene::align::kMiddle},
                                                           icon_button(colours, icon::phone{}, sends<call_chosen>{})))));
}

// Settings' Calls page: Element's two switches, bound to the plugin's own
// settings, in effect from the next call.
inline auto calls_settings_view(const palette& colours) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(calls_settings_view, c::column(
      c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      c::styled({.depth = 1.0f, .background = colours.sidebar},
          page_header<sends<request::settings_home>, sends<request::close_settings>>(colours, "Calls", {}, {}, true, true)),
      c::column(c::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 0.0f, 12.0f, 0.0f}}),
          spaced_title(colours, "CONNECTION"),
          setting_switch<&kept::peer_to_peer>(colours, "Allow peer-to-peer for 1:1 calls"),
          spaced_note(colours, "Off, a call goes through your homeserver's relay (TURN) only: the other side never learns your "
                               "address. It needs the homeserver to give one."),
          setting_switch<&kept::fallback_server>(colours, "Allow fallback call assist server (turn.matrix.org)"),
          spaced_note(colours, "Where your homeserver gives no relay, Matrix.org's server is asked for your address, so that a "
                               "call can find its way through your router. That server learns your IP address."))));
}
inline auto extend(contract::settings_links, type_tag<feature>, const palette& colours) {
  return settings_link(colours, "Calls", icon::phone{}, open_calls_page{});
}
inline auto extend(contract::settings_pages, const ui_needs& n, const calls_page&) { return calls_settings_view(*n.colours); }

} // namespace mux::plugins::calls
