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
import skiff.nodes.flow;
import skiff.nodes.text;
import skiff.widgets.button;
import mux.core;
import :base;
import :icons;
import :themes;
import :controls;

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
template <class Actions> struct call_arguments {
  ui_needs<Actions> needs;
  auto operator()(const call_view &view) const { return std::tie(needs, view); }
};

// Element's call colours: hanging up and declining red, answering green.
inline constexpr skia::SkColor kHangUpRed = skia::colorSetARGB(255, 0xFF, 0x5B, 0x55);
inline constexpr skia::SkColor kAnswerGreen = skia::colorSetARGB(255, 0x0D, 0xBD, 0x8B);

// The round buttons, the same in the view and in the card: the microphone
// and hanging up during the call; Decline and Accept while it rings here;
// none once it has ended.
template <class Actions> struct call_buttons : skiff::compose::Stacked {
  struct accept_it {
    using Answer = ::mux::ui::request::accept_call;
    ::mux::ui::request::accept_call operator()() { return ::mux::ui::request::accept_call{}; }
  };
  struct decline_it {
    using Answer = ::mux::ui::request::decline_call;
    ::mux::ui::request::decline_call operator()() { return ::mux::ui::request::decline_call{}; }
  };
  struct mute_it {
    using Answer = ::mux::ui::request::mute_call;
    ::mux::ui::request::mute_call operator()() { return ::mux::ui::request::mute_call{}; }
  };
  struct hang_up_it {
    using Answer = ::mux::ui::request::hang_up;
    ::mux::ui::request::hang_up operator()() { return ::mux::ui::request::hang_up{}; }
  };
  // Over: called again, as Element's Call back; or put away.
  struct call_back_it {
    using Answer = ::mux::ui::request::start_call;
    conversation_id in;
    Answer operator()() const { return {in}; }
  };
  struct dismiss_it {
    using Answer = ::mux::ui::request::dismiss_call;
    ::mux::ui::request::dismiss_call operator()() { return ::mux::ui::request::dismiss_call{}; }
  };
  struct parts_t {
    icon_button<mute_it> mute;
    icon_button<decline_it> decline;
    icon_button<hang_up_it> hang_up;
    icon_button<accept_it> accept;
    icon_button<call_back_it> call_back;
    icon_button<dismiss_it> dismiss;
  } parts;
  bool ending_ = false;
  [[nodiscard]] bool wantsTick() const { return ending_; }
  void update(double) {}
  [[nodiscard]] static scene::Spec round(float size, skia::SkColor plate,
                                         skia::SkColor hover) {
    return {.width = size,
            .height = size,
            .cornerRadius = size / 2.0f,
            .background = plate,
            .hoverBackground = hover,
            .focusBackground = hover};
  }
  [[nodiscard]] static palette white_icons(palette colours) {
    colours.text = skia::colorSetARGB(255, 255, 255, 255);
    return colours;
  }
  call_buttons(const palette &colours, float size, const call_view &view)
      : Stacked(skiff::compose::hbox(size / 2.0f,
                                     {.autoSize = scene::axes::kBoth,
                                      .alignSelf = scene::align::kMiddle})),
        parts{.mute = skiff::compose::visible(
                  !rings_here(view) && !has_ended(view),
                  skiff::compose::styled(
                      round(size, colours.tile, colours.chosen),
                      icon_button<mute_it>(colours,
                                           view.muted
                                               ? icon_t{icon::microphone_off{}}
                                               : icon_t{icon::microphone{}},
                                           {}))),
              .decline = skiff::compose::visible(
                  rings_here(view),
                  skiff::compose::styled(
                      round(size, kHangUpRed, kHangUpRed),
                      icon_button<decline_it>(white_icons(colours),
                                              icon::hang_up{}, {}))),
              .hang_up = skiff::compose::visible(
                  !rings_here(view) && !has_ended(view),
                  skiff::compose::styled(
                      round(size, kHangUpRed, kHangUpRed),
                      icon_button<hang_up_it>(white_icons(colours),
                                              icon::hang_up{}, {}))),
              .accept = skiff::compose::visible(
                  rings_here(view) && view.available,
                  skiff::compose::styled(
                      round(size, kAnswerGreen, kAnswerGreen),
                      icon_button<accept_it>(white_icons(colours),
                                             icon::phone{}, {}))),
              .call_back = skiff::compose::visible(
                  has_ended(view) && view.available,
                  skiff::compose::styled(
                      round(size, kAnswerGreen, kAnswerGreen),
                      icon_button<call_back_it>(white_icons(colours),
                                                icon::phone{}, {view.in}))),
              .dismiss = skiff::compose::visible(
                  has_ended(view),
                  skiff::compose::styled(
                      round(size, colours.tile, colours.chosen),
                      icon_button<dismiss_it>(colours, icon::close{}, {})))},
        ending_(has_ended(view)) {}
};

// The call, in its chat: under the head, as Element's call view.
template <class Actions> struct call_panel : skiff::compose::Stacked {
  struct parts_t {
    avatar_mark face;
    nodes::Text who;
    call_status_t said;
    call_buttons<Actions> buttons;
  } parts;
  call_panel(const ui_needs<Actions> &n, const call_view &view)
      : Stacked(skiff::compose::justified(
            skiff::compose::vbox(
                10.0f, {.fillX = true,
                        .height = 280.0f,
                        .padding = {20.0f, 16.0f, 20.0f, 16.0f},
                        .background = n.colours->sidebar,
                        .border = scene::Border{n.colours->band, 1.0f}}),
            nodes::justify::middle{})),
        parts{.face = avatar_mark(view.in.id, view.who, 88.0f),
              .who = skiff::compose::styled(
                  {.alignSelf = scene::align::kMiddle},
                  nodes::Text(view.who, 18.0f, n.colours->text, true)),
              .said = skiff::compose::styled(
                  {.alignSelf = scene::align::kMiddle,
                   .margin = {0.0f, 0.0f, 8.0f, 0.0f}},
                  skiff::compose::text_of<call_shown>(
                      call_words{},
                      nodes::Text(said_of(view), 13.0f, n.colours->dim))),
              .buttons = call_buttons<Actions>(*n.colours, 52.0f, view)} {}
};

// The call on a phone -- a window narrow and taller than wide, mux's
// single column -- as Element's phone apps show it: the whole window,
// the other's picture large in its upper middle with their name and where
// the call is under it, and the buttons along the bottom, larger.
template <class Actions> struct call_screen : skiff::compose::Stacked {
  struct parts_t {
    nodes::Box<> above;
    avatar_mark face;
    nodes::Text who;
    call_status_t said;
    nodes::Box<> below;
    call_buttons<Actions> buttons;
  } parts;
  call_screen(const ui_needs<Actions> &n, const call_view &view)
      : Stacked(skiff::compose::vbox(12.0f,
                                     {.place = scene::anchor::kTopLeft,
                                      .fill = true,
                                      .padding = {24.0f, 24.0f, 48.0f, 24.0f},
                                      .background = n.colours->sidebar})),
        parts{.above = skiff::compose::styled(
                  {.fillX = true, .height = 1.0f, .grow = scene::axes::kY},
                  nodes::Box<>(skia::SkColor{0})),
              .face = avatar_mark(view.in.id, view.who, 128.0f),
              .who = skiff::compose::styled(
                  {.alignSelf = scene::align::kMiddle},
                  wrapped(nodes::Text(view.who, 24.0f, n.colours->text, true))),
              .said = skiff::compose::styled(
                  {.alignSelf = scene::align::kMiddle},
                  skiff::compose::text_of<call_shown>(
                      call_words{}, wrapped(nodes::Text(said_of(view), 15.0f,
                                                        n.colours->dim)))),
              .below = skiff::compose::styled(
                  {.fillX = true, .height = 1.0f, .grow = scene::axes::kY},
                  nodes::Box<>(skia::SkColor{0})),
              .buttons = call_buttons<Actions>(*n.colours, 64.0f, view)} {}

  // Over the whole window: nothing under it is pressed.
  [[nodiscard]] bool acceptsInput() const { return true; }
};

// The call anywhere else, and ringing here: a card at the top of the
// window, as Element's toast.
template <class Actions> struct call_bar : skiff::compose::Stacked {
  struct texts : skiff::compose::Stacked {
    struct parts_t {
      nodes::Text who;
      call_status_t said;
    } parts;
    texts(const palette &colours, const call_view &view)
        : Stacked(
              skiff::compose::vbox(2.0f, {.autoSize = scene::axes::kY,
                                          .grow = scene::axes::kX,
                                          .alignSelf = scene::align::kMiddle})),
          parts{.who = skiff::compose::styled(
                    {.fillX = true},
                    elided(nodes::Text(view.who, 15.0f, colours.text, true))),
                .said = skiff::compose::styled(
                    {.fillX = true},
                    skiff::compose::text_of<call_shown>(
                        call_words{}, elided(nodes::Text(said_of(view), 13.0f,
                                                         colours.dim))))} {}
  };
  struct parts_t {
    avatar_mark face;
    texts lines;
    call_buttons<Actions> buttons;
  } parts;

  call_bar(const ui_needs<Actions> &n, const call_view &view)
      : Stacked(skiff::compose::hbox(
            12.0f,
            {.place = scene::anchor::kTopRight,
             .x = 12.0f,
             .y = 12.0f,
             .width = 360.0f,
             .autoSize = scene::axes::kY,
             .padding = {12.0f, 14.0f, 12.0f, 14.0f},
             .cornerRadius = 12.0f,
             .background = n.colours->popup(),
             .border = scene::Border{n.colours->band, 1.0f},
             .shadow = scene::Shadow{skia::colorSetARGB(70, 0, 0, 0), 3.0f}})),
        parts{.face = avatar_mark(view.in.id, view.who, 40.0f),
              .lines = texts(*n.colours, view),
              .buttons = call_buttons<Actions>(*n.colours, 40.0f, view)} {}
};

template <class Content, class Actions, class Which>
[[nodiscard]] auto call_layer(const ui_needs<Actions> &needs, Which which,
                              scene::Spec spec, bool floats = false) {
  return skiff::compose::derived<call_shown>(
      which, skiff::compose::mount<Content, call_view>(
                 call_arguments<Actions>{needs}, spec, floats));
}
template <class Actions>
using call_panel_t = decltype(call_layer<call_panel<Actions>>(
    std::declval<const ui_needs<Actions> &>(), call_panel_of{}, scene::Spec{}));

} // namespace mux::ui
