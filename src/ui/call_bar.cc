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
  std::int64_t seconds = 0;  // how long it has gone on
  friend bool operator==(const connected&, const connected&) = default;
};
struct ended {
  std::string why;
  friend bool operator==(const ended&, const ended&) = default;
};
}  // namespace call_phase
using call_phase_t =
    spl::variant<call_phase::ringing_in, call_phase::ringing_out, call_phase::connecting, call_phase::connected, call_phase::ended>;
struct call_view {
  conversation_id in;
  std::string who;
  call_phase_t phase;
  bool muted = false;
  bool encrypted = false;  // its signalling end-to-end encrypted: the room is
  bool available = true;   // calls in this build
  bool in_view = false;    // its chat is the one shown
  bool whole = false;      // a phone's window: the call over all of it
  friend bool operator==(const call_view&, const call_view&) = default;
};

[[nodiscard]] inline bool rings_here(const call_view& view) {
  return spl::visit(spl::overloaded{[](call_phase::ringing_in) { return true; }, [](const auto&) { return false; }},
                       view.phase);
}
[[nodiscard]] inline bool has_ended(const call_view& view) {
  return spl::visit(spl::overloaded{[](const call_phase::ended&) { return true; }, [](const auto&) { return false; }},
                       view.phase);
}
[[nodiscard]] inline std::string said_of(const call_view& view) {
  const std::string where = spl::visit(
      spl::overloaded{[](call_phase::ringing_in) { return std::string("Incoming voice call"); },
                         [](call_phase::ringing_out) { return std::string("Calling…"); },
                         [](call_phase::connecting) { return std::string("Connecting…"); },
                         [](const call_phase::connected& now) {
                           return std::format("{}:{:02}", now.seconds / 60, now.seconds % 60);
                         },
                         [](const call_phase::ended& done) { return done.why; }},
      view.phase);
  if (has_ended(view))
    return where;
  if (!view.available)
    return where + " · calls aren't in this build";
  return view.encrypted ? where : where + " · not end-to-end encrypted";
}

// Element's call colours: hanging up and declining red, answering green.
inline constexpr skia::SkColor kHangUpRed = skia::colorSetARGB(255, 0xFF, 0x5B, 0x55);
inline constexpr skia::SkColor kAnswerGreen = skia::colorSetARGB(255, 0x0D, 0xBD, 0x8B);

// The round buttons, the same in the view and in the card: the microphone
// and hanging up during the call; Decline and Accept while it rings here;
// none once it has ended.
template <class Actions>
struct call_buttons : nodes::Stack {
  struct accept_it {
    Actions* actions;
    void operator()() const { actions->accept_call(); }
  };
  struct decline_it {
    Actions* actions;
    void operator()() const { actions->decline_call(); }
  };
  struct mute_it {
    Actions* actions;
    void operator()() const { actions->mute_call(); }
  };
  struct hang_up_it {
    Actions* actions;
    void operator()() const { actions->hang_up(); }
  };
  // Over: called again, as Element's Call back; or put away.
  struct call_back_it {
    call_buttons* buttons;
    void operator()() const {
      if (buttons->in_)
        buttons->actions_->start_call(*buttons->in_);
    }
  };
  struct dismiss_it {
    Actions* actions;
    void operator()() const { actions->dismiss_call(); }
  };
  struct parts_t {
    icon_button<mute_it> mute;
    icon_button<decline_it> decline;
    icon_button<hang_up_it> hang_up;
    icon_button<accept_it> accept;
    icon_button<call_back_it> call_back;
    icon_button<dismiss_it> dismiss;
  } parts;
  const palette* colours_;
  Actions* actions_;
  std::optional<conversation_id> in_;
  // Ended, it goes by itself in a moment: frames asked for until then, so
  // the program sees the moment come.
  bool ending_ = false;
  [[nodiscard]] bool wantsTick() const { return ending_; }
  void update(double) {}
  call_buttons(const palette& colours, Actions* a, float size)
      : parts{.mute = icon_button<mute_it>(colours, icon::microphone{}, {a}),
              .decline = icon_button<decline_it>(colours, icon::hang_up{}, {a}),
              .hang_up = icon_button<hang_up_it>(colours, icon::hang_up{}, {a}),
              .accept = icon_button<accept_it>(colours, icon::phone{}, {a}),
              .call_back = icon_button<call_back_it>(colours, icon::phone{}, {this}),
              .dismiss = icon_button<dismiss_it>(colours, icon::close{}, {a})},
        colours_(&colours),
        actions_(a) {
    this->setHorizontal();
    this->setGap(size / 2.0f);
    fState.apply({.autoSize = scene::axes::kBoth, .alignSelf = scene::align::kMiddle});
    auto& [mute, decline, hang_up, accept, call_back, dismiss] = parts;
    const auto round = [&](auto& button, skia::SkColor plate, skia::SkColor hover) {
      button.apply({.width = size, .height = size, .cornerRadius = size / 2.0f, .background = plate, .hoverBackground = hover,
                    .focusBackground = hover});
    };
    round(mute, colours.tile, colours.chosen);
    round(decline, kHangUpRed, kHangUpRed);
    round(hang_up, kHangUpRed, kHangUpRed);
    round(accept, kAnswerGreen, kAnswerGreen);
    round(call_back, kAnswerGreen, kAnswerGreen);
    round(dismiss, colours.tile, colours.chosen);
    for (auto* white : {&decline.parts.mark, &hang_up.parts.mark, &accept.parts.mark, &call_back.parts.mark})
      white->setColour(skia::colorSetARGB(255, 255, 255, 255));
  }
  void show(const call_view& view) {
    auto& [mute, decline, hang_up, accept, call_back, dismiss] = parts;
    const bool ringing = rings_here(view);
    const bool ended = has_ended(view);
    in_ = view.in;
    ending_ = ended;
    call_back.setVisible(ended && view.available);
    dismiss.setVisible(ended);
    mute.setVisible(!ringing && !ended);
    mute.parts.mark.setShape(view.muted ? shape_of(icon::microphone_off{}) : shape_of(icon::microphone{}));
    hang_up.setVisible(!ringing && !ended);
    decline.setVisible(ringing);
    accept.setVisible(ringing && view.available);
  }
};

// The call, in its chat: under the head, as Element's call view.
template <class Actions>
struct call_panel : nodes::Stack {
  struct parts_t {
    avatar_mark face;
    nodes::Text who;
    nodes::Text said;
    call_buttons<Actions> buttons;
  } parts;
  call_panel(const ui_needs<Actions>& n, const call_view& view)
      : parts{.face = avatar_mark(view.in.id, view.who, 88.0f),
              .who = nodes::Text(view.who, 18.0f, n.colours->text, true),
              .said = nodes::Text(said_of(view), 13.0f, n.colours->dim),
              .buttons = call_buttons<Actions>(*n.colours, n.actions, 52.0f)} {
    this->setGap(10.0f);
    fStack.justify = nodes::justify::middle{};
    fState.apply({.fillX = true, .height = 280.0f, .padding = {20.0f, 16.0f, 20.0f, 16.0f}, .background = n.colours->sidebar,
                  .border = scene::Border{n.colours->band, 1.0f}});
    parts.who.apply({.alignSelf = scene::align::kMiddle});
    parts.said.apply({.alignSelf = scene::align::kMiddle, .margin = {0.0f, 0.0f, 8.0f, 0.0f}});
    this->show(view);
  }
  void show(const call_view& view) {
    parts.face.show(view.in.id, view.who);
    parts.who.setText(view.who);
    parts.said.setText(said_of(view));
    parts.buttons.show(view);
    this->invalidateLayout();
    this->markDamaged();
  }
};

// The call on a phone -- a window narrow and taller than wide, mux's
// single column -- as Element's phone apps show it: the whole window,
// the other's picture large in its upper middle with their name and where
// the call is under it, and the buttons along the bottom, larger.
template <class Actions>
struct call_screen : nodes::Stack {
  struct parts_t {
    nodes::Box<> above;
    avatar_mark face;
    nodes::Text who;
    nodes::Text said;
    nodes::Box<> below;
    call_buttons<Actions> buttons;
  } parts;
  call_screen(const ui_needs<Actions>& n, const call_view& view)
      : parts{.above = nodes::Box<>(skia::SkColor{0}),
              .face = avatar_mark(view.in.id, view.who, 128.0f),
              .who = nodes::Text(view.who, 24.0f, n.colours->text, true),
              .said = nodes::Text(said_of(view), 15.0f, n.colours->dim),
              .below = nodes::Box<>(skia::SkColor{0}),
              .buttons = call_buttons<Actions>(*n.colours, n.actions, 64.0f)} {
    this->setGap(12.0f);
    fState.apply({.place = scene::anchor::kTopLeft, .fill = true, .padding = {24.0f, 24.0f, 48.0f, 24.0f},
                  .background = n.colours->sidebar});
    auto& [above, face, who, said, below, buttons] = parts;
    above.apply({.fillX = true, .height = 1.0f, .grow = scene::axes::kY});
    below.apply({.fillX = true, .height = 1.0f, .grow = scene::axes::kY});
    for (nodes::Text* each : {&who, &said}) {
      each->setWrapped(true);
      each->apply({.alignSelf = scene::align::kMiddle});
    }
    this->show(view);
  }
  void show(const call_view& view) {
    parts.face.show(view.in.id, view.who);
    parts.who.setText(view.who);
    parts.said.setText(said_of(view));
    parts.buttons.show(view);
    this->invalidateLayout();
    this->markDamaged();
  }
  // Over the whole window: nothing under it is pressed.
  [[nodiscard]] bool acceptsInput() const { return true; }
};

// The call anywhere else, and ringing here: a card at the top of the
// window, as Element's toast.
template <class Actions>
struct call_bar : nodes::Stack {
  struct texts : nodes::Stack {
    struct parts_t {
      nodes::Text who;
      nodes::Text said;
    } parts;
    texts(const palette& colours, const call_view& view)
        : parts{.who = nodes::Text(view.who, 15.0f, colours.text, true), .said = nodes::Text(said_of(view), 13.0f, colours.dim)} {
      this->setGap(2.0f);
      fState.apply({.autoSize = scene::axes::kY, .grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
      for (nodes::Text* each : {&parts.who, &parts.said}) {
        each->setElided(true);
        each->apply({.fillX = true});
      }
    }
  };
  struct parts_t {
    avatar_mark face;
    texts lines;
    call_buttons<Actions> buttons;
  } parts;

  call_bar(const ui_needs<Actions>& n, const call_view& view)
      : parts{.face = avatar_mark(view.in.id, view.who, 40.0f),
              .lines = texts(*n.colours, view),
              .buttons = call_buttons<Actions>(*n.colours, n.actions, 40.0f)} {
    this->setHorizontal();
    this->setGap(12.0f);
    fState.apply({.place = scene::anchor::kTopRight, .x = 12.0f, .y = 12.0f, .width = 360.0f, .autoSize = scene::axes::kY,
                  .padding = {12.0f, 14.0f, 12.0f, 14.0f}, .cornerRadius = 12.0f, .background = n.colours->popup(),
                  .border = scene::Border{n.colours->band, 1.0f},
                  .shadow = scene::Shadow{skia::colorSetARGB(70, 0, 0, 0), 3.0f}});
    this->show(view);
  }
  // As the call is now.
  void show(const call_view& view) {
    parts.face.show(view.in.id, view.who);
    parts.lines.parts.who.setText(view.who);
    parts.lines.parts.said.setText(said_of(view));
    parts.buttons.show(view);
    this->invalidateLayout();
    this->markDamaged();
  }
};

}  // namespace mux::ui
