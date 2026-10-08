// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:call_bar -- A call, as Element shows one: in the chat it is in, a
// view of its own under the head -- the other's picture, their name, where
// the call is, and round buttons: the microphone, and hanging up; anywhere
// else, and while it rings here, a card at the top of the window, as
// Element's call toast -- Decline and Accept while it rings. A call ended
// stays a moment, saying why.
export module mux.ui:call_bar;

import std;
import splice;
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
import :base;
import :icons;
import :themes;
import :avatars;

export namespace mux::ui {

// Where a call is, as it is shown.
namespace call_phase {
struct ringing_in {
  friend bool operator==(ringing_in, ringing_in) = default;
};
struct ringing_out {
  friend bool operator==(ringing_out, ringing_out) = default;
};
struct connecting {
  friend bool operator==(connecting, connecting) = default;
};
struct connected {
  std::int64_t seconds = 0; // how long it has gone on
  friend bool operator==(const connected&, const connected&) = default;
};
struct ended {
  std::string why;
  friend bool operator==(const ended&, const ended&) = default;
};
} // namespace call_phase
using call_phase_t =
    spl::variant<call_phase::ringing_in, call_phase::ringing_out, call_phase::connecting, call_phase::connected, call_phase::ended>;
struct call_view {
  conversation_id in;
  std::string who;
  call_phase_t phase;
  bool muted = false;
  bool encrypted = false; // its signalling end-to-end encrypted: the room is
  bool available = true;  // calls in this build
  bool in_view = false;   // its chat is the one shown
  bool whole = false;     // a phone's window: the call over all of it
  friend bool operator==(const call_view&, const call_view&) = default;
};

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

struct call_shown {
  std::optional<call_view> now;
};
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
  return skiff::compose::keep_ticking(ended, skiff::compose::row(
      skiff::compose::hbox(size / 2.0f, {.autoSize = scene::axes::kBoth, .alignSelf = scene::align::kMiddle}),
      call_button(colours, size, colours.tile, colours.chosen, colours.text,
                  view.muted ? icon_t{icon::microphone_off{}} : icon_t{icon::microphone{}},
                  request::mute_call{}, !ringing && !ended, view.muted ? "Unmute" : "Mute"),
      call_button(colours, size, kHangUpRed, kHangUpRed, white, icon::hang_up{}, request::decline_call{}, ringing, "Decline"),
      call_button(colours, size, kHangUpRed, kHangUpRed, white, icon::hang_up{}, request::hang_up{}, !ringing && !ended, "Hang up"),
      call_button(colours, size, kAnswerGreen, kAnswerGreen, white, icon::phone{}, request::accept_call{}, ringing && view.available, "Accept"),
      call_button(colours, size, kAnswerGreen, kAnswerGreen, white, icon::phone{}, request::start_call{view.in}, ended && view.available, "Call back"),
      call_button(colours, size, colours.tile, colours.chosen, colours.text, icon::close{}, request::dismiss_call{}, ended, "Dismiss")));
}
inline auto call_avatar(const call_view& view, float size) {
  return skiff::compose::styled({.alignSelf = scene::align::kMiddle},
      widgets::Avatar<from_avatars>(initials_of(view.who), size, picture_of(view.in.id), gradient_of(view.in.id)));
}
inline auto call_status(const palette& colours, const call_view& view, float size) {
  return skiff::compose::text_of<call_shown>(call_words{}, nodes::Text(said_of(view), size, colours.dim));
}
// The call in its chat, beneath the header.
inline auto call_panel(const palette& colours, const call_view& view) {
  return skiff::compose::column(
      skiff::compose::justified(skiff::compose::vbox(10.0f,
          {.fillX = true, .height = 280.0f, .padding = {20.0f, 16.0f, 20.0f, 16.0f},
           .background = colours.sidebar, .border = scene::Border{colours.band, 1.0f}}), nodes::justify::middle{}),
      call_avatar(view, 88.0f),
      skiff::compose::styled({.alignSelf = scene::align::kMiddle}, nodes::Text(view.who, 18.0f, colours.text, true)),
      skiff::compose::styled({.alignSelf = scene::align::kMiddle, .margin = {0.0f, 0.0f, 8.0f, 0.0f}}, call_status(colours, view, 13.0f)),
      call_buttons(colours, 52.0f, view));
}
// On a phone the call fills the window and takes presses off its controls.
inline auto call_screen(const palette& colours, const call_view& view) {
  return skiff::compose::onClick(scene::Taken{}, skiff::compose::column(
      skiff::compose::vbox(12.0f, {.place = scene::anchor::kTopLeft, .fill = true,
                                 .padding = {24.0f, 24.0f, 48.0f, 24.0f}, .background = colours.sidebar}),
      skiff::compose::styled({.fillX = true, .height = 1.0f, .grow = scene::axes::kY}, nodes::Box<>(skia::SkColor{0})),
      call_avatar(view, 128.0f),
      skiff::compose::styled({.alignSelf = scene::align::kMiddle}, wrapped(nodes::Text(view.who, 24.0f, colours.text, true))),
      skiff::compose::styled({.alignSelf = scene::align::kMiddle}, wrapped(call_status(colours, view, 15.0f))),
      skiff::compose::styled({.fillX = true, .height = 1.0f, .grow = scene::axes::kY}, nodes::Box<>(skia::SkColor{0})),
      call_buttons(colours, 64.0f, view)));
}
// A compact card above the conversation while the call rings elsewhere.
inline auto call_bar(const palette& colours, const call_view& view) {
  return skiff::compose::row(
      skiff::compose::hbox(12.0f, {.place = scene::anchor::kTopRight, .x = 12.0f, .y = 12.0f,
                                  .width = 360.0f, .autoSize = scene::axes::kY,
                                  .padding = {12.0f, 14.0f, 12.0f, 14.0f}, .cornerRadius = 12.0f,
                                  .background = colours.popup(), .border = scene::Border{colours.band, 1.0f},
                                  .shadow = scene::Shadow{skia::colorSetARGB(70, 0, 0, 0), 3.0f}}),
      call_avatar(view, 40.0f),
      skiff::compose::column(
          skiff::compose::vbox(2.0f, {.autoSize = scene::axes::kY, .grow = scene::axes::kX, .alignSelf = scene::align::kMiddle}),
          skiff::compose::styled({.fillX = true}, elided(nodes::Text(view.who, 15.0f, colours.text, true))),
          skiff::compose::styled({.fillX = true}, elided(call_status(colours, view, 13.0f)))),
      call_buttons(colours, 40.0f, view));
}
namespace call_ui {
struct panel {};
struct card {};
struct screen {};
}
inline auto call_content(call_ui::panel, const palette& colours, const call_view& view) { return call_panel(colours, view); }
inline auto call_content(call_ui::card, const palette& colours, const call_view& view) { return call_bar(colours, view); }
inline auto call_content(call_ui::screen, const palette& colours, const call_view& view) { return call_screen(colours, view); }

template <class Actions, class Which, class View>
[[nodiscard]] auto call_layer(const ui_needs<Actions>& needs, Which which, View view,
                              scene::Spec spec, bool floats = false) {
  using Content = decltype(call_content(view, *needs.colours, call_view{}));
  return skiff::compose::derived<call_shown>(which, skiff::compose::mount<Content, call_view>(
      [colours = needs.colours, view](const call_view& facts) {
        return std::tuple{call_content(view, *colours, facts)};
      }, spec, floats));
}
template <class Actions>
using call_panel_t = decltype(call_layer(std::declval<const ui_needs<Actions>&>(),
    call_panel_of{}, call_ui::panel{}, scene::Spec{}));

} // namespace mux::ui
