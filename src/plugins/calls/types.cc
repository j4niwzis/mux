// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.calls.types -- What the calls plugin names -- a call as it is
// shown, what is asked of it -- and what it says it provides: the window's
// layers (its card, its whole-window view), the chat column (its view in its
// chat), the chat's head (its button), and a part of the program. Light:
// mux.ui sees it.
export module mux.plugin.calls.types;

import std;
import splice;
import knot;
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
  // Whether the chat shown can be called: its protocol calls, and it is two.
  bool callable = false;
  friend bool operator==(const call_shown&, const call_shown&) = default;
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

// Its own settings (contract::settings_kept), as Element's Voice & Video
// has them: whether a call may go straight between the two ends (off,
// through a relay only -- neither end's address told to the other); and
// whether, where the homeserver gives no relay, a public one (Matrix.org's
// STUN) is asked for this one's address.
struct kept {
  bool peer_to_peer = true;
  bool fallback_server = false;
  friend bool operator==(const kept&, const kept&) = default;
  friend consteval auto json_schema(knot::type<kept>) { return knot::schema<kept>(); }
};
consteval bool provides(contract::settings_kept, type_tag<feature>) { return true; }
constexpr type_tag<kept> settings_type(contract::settings_kept, type_tag<feature>) { return {}; }
// Settings' Calls page, and its link.
struct calls_page {
  friend bool operator==(calls_page, calls_page) = default;
};
struct open_calls_page {};
consteval bool provides(contract::settings_links, type_tag<feature>) { return true; }
consteval bool provides(contract::settings_pages, type_tag<feature>) { return true; }
constexpr composition::page_list<calls_page> settings_pages(contract::settings_pages, type_tag<feature>) { return {}; }

}  // namespace mux::plugins::calls
