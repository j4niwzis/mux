// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.calls.types -- What the calls plugin names -- a call as it is
// shown, what is asked of it -- and what it says it provides: the window's
// layers (its card, its whole-window view), the chat column (its view in its
// chat), the chat's head (its button), and a part of the program. Light:
// mux.ui sees it.
export module mux.plugin.calls.types;

import std;
import splice;
import mux.core;
import mux.composition;

export namespace mux::plugins::calls {

struct feature {
  static constexpr std::string_view name = "calls";
};

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
  bool available = true;   // calls' connection in this build
  bool in_view = false;    // its chat is the one shown
  bool whole = false;      // a phone's window: the call over all of it
  friend bool operator==(const call_view&, const call_view&) = default;
};
// What is shown of a call: none, or the one.
struct call_shown {
  std::optional<call_view> now;
};

// Asked of the program: a call made to a chat, to the one chosen; taken,
// declined, hung up; the microphone muted or not; an ended call put away.
struct start_call {
  conversation_id in;
};
struct call_chosen {};
struct accept_call {};
struct decline_call {};
struct hang_up {};
struct mute_call {};
struct dismiss_call {};

consteval bool provides(contract::window_layers, type_tag<feature>) { return true; }
constexpr type_tag<call_shown> facts_type(contract::window_layers, type_tag<feature>) { return {}; }
consteval bool provides(contract::chat_column, type_tag<feature>) { return true; }
consteval bool provides(contract::chat_header, type_tag<feature>) { return true; }
consteval bool provides(contract::program_parts, type_tag<feature>) { return true; }

}  // namespace mux::plugins::calls
