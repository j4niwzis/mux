// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:call_bar -- A call, over the window: who, where it is -- ringing,
// connecting, how long it has gone on -- and what can be done: Accept and
// Decline while it rings here, Mute and Hang up the rest of the time.
export module mux.ui:call_bar;

import std;
import splice;
import skia;
import skiff.scene;
import skiff.nodes.flow;
import skiff.nodes.text;
import skiff.widgets.button;
import mux.core;
import :base;
import :themes;

export namespace mux::ui {

// Where a call is, as the bar says it.
namespace call_phase {
struct ringing_in {};
struct ringing_out {};
struct connecting {};
struct connected {
  std::chrono::steady_clock::time_point since;
};
}  // namespace call_phase
using call_phase_t = splice::variant<call_phase::ringing_in, call_phase::ringing_out, call_phase::connecting, call_phase::connected>;
struct call_view {
  std::string who;
  call_phase_t phase;
  bool muted = false;
  bool encrypted = false;  // its signalling end-to-end encrypted: the room is
  bool available = true;   // calls in this build
};

[[nodiscard]] inline std::string said_of(const call_view& view) {
  const std::string where = splice::visit(
      splice::overloaded{[](call_phase::ringing_in) { return std::string("Incoming call"); },
                         [](call_phase::ringing_out) { return std::string("Calling…"); },
                         [](call_phase::connecting) { return std::string("Connecting…"); },
                         [](const call_phase::connected& now) {
                           const auto gone = std::chrono::duration_cast<std::chrono::seconds>(
                               std::chrono::steady_clock::now() - now.since);
                           return std::format("{}:{:02}", gone.count() / 60, gone.count() % 60);
                         }},
      view.phase);
  return view.encrypted ? where : where + " · not end-to-end encrypted";
}

template <class Actions>
struct call_bar : nodes::Stack {
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
  struct parts_t {
    nodes::Text who;
    nodes::Text said;
    widgets::Button<accept_it> accept;
    widgets::Button<decline_it> decline;
    widgets::Button<mute_it> mute;
    widgets::Button<hang_up_it> hang_up;
  } parts;

  call_bar(const ui_needs<Actions>& n, const call_view& view)
      : parts{.who = nodes::Text(view.who, 15.0f, n.colours->text),
              .said = nodes::Text(said_of(view), 13.0f, n.colours->dim),
              .accept = widgets::Button<accept_it>(n.colours->widgets, "Accept", {n.actions}),
              .decline = widgets::Button<decline_it>(n.colours->widgets, "Decline", {n.actions}),
              .mute = widgets::Button<mute_it>(n.colours->widgets, "Mute", {n.actions}),
              .hang_up = widgets::Button<hang_up_it>(n.colours->widgets, "Hang up", {n.actions})} {
    this->setGap(6.0f);
    fState.apply({.place = scene::anchor::kTopCentre, .y = 12.0f, .width = 340.0f, .autoSize = scene::axes::kY,
                  .padding = {10.0f, 12.0f, 10.0f, 12.0f}, .cornerRadius = 12.0f, .background = n.colours->popup(),
                  .border = scene::Border{n.colours->band, 1.0f},
                  .shadow = scene::Shadow{skia::colorSetARGB(70, 0, 0, 0), 3.0f}});
    auto& [who, said, ... buttons] = parts;
    who.apply({.fillX = true});
    said.apply({.fillX = true});
    ((buttons.apply({.fillX = true, .height = 30.0f})), ...);
    this->show(view);
  }
  // As the call is now: what it says, and which buttons it has.
  void show(const call_view& view) {
    parts.who.setText(view.who);
    parts.said.setText(said_of(view));
    const bool ringing_here =
        splice::visit(splice::overloaded{[](call_phase::ringing_in) { return true; }, [](const auto&) { return false; }}, view.phase);
    parts.accept.setVisible(ringing_here && view.available);
    parts.decline.setVisible(ringing_here);
    parts.mute.setVisible(!ringing_here);
    parts.mute.setLabel(view.muted ? "Unmute" : "Mute");
    parts.hang_up.setVisible(!ringing_here);
    this->invalidateLayout();
    this->markDamaged();
  }
};

}  // namespace mux::ui
