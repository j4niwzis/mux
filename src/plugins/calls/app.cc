// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.calls.app: a call -- one at a time -- made and taken: what its
// protocol said in it (call_signalled) and what its connection says
// (mux.calls.media), brought together, and shown in the window's call bar.
// The calls plugin's part of the program: it owns its state, and reaches the
// rest through the services it is given.
export module mux.plugin.calls.app;

import std;
import splice;
import mux.composition;
import mux.core;
import mux.ui;
import mux.calls.media;
import mux.platform.permissions;
import mux.app.network;
import mux.app.services;
import mux.plugin.calls.types;
import mux.plugin.calls.matrix;

export namespace mux::plugins::calls {

// Keep the variant and its alternatives outside the controller being parsed.
// Clang was reported crashing while instantiating the nested call-state
// variant at the call's state member, before calls_part was complete.
namespace call_state {
  struct starting {};       // the servers asked for
  struct ringing_out {};    // invited, not answered yet
  struct ringing_in {};     // invited by them
  struct connecting {};     // answered: the connection being made
  struct connected {
    std::chrono::steady_clock::time_point since;
  };
}
using call_state_t = spl::variant<call_state::starting, call_state::ringing_out, call_state::ringing_in,
                                call_state::connecting, call_state::connected>;
struct active_call {
  conversation_id in;
  std::string id;
  bool outgoing = false;
  call_state_t state;
  std::string their_party;
  mux::calls::session_description offer;
  // The version it is made in: its invite's, or version 1 for one made here.
  call_version_t version = call_version::current{};
  std::map<std::string, std::vector<mux::calls::ice_candidate>> early;
  std::vector<mux::calls::ice_candidate> candidates_out;
  std::chrono::steady_clock::time_point ring_until = std::chrono::steady_clock::now() + std::chrono::seconds(60);
  std::unique_ptr<mux::calls::media_session<app::wake_window>> media;
};

class part {
 public:
  explicit part(app::services& shared) : s_(&shared) {}

  // -- what the user asks
  // A call made to the chat: the account's servers asked for first; the
  // offer made once they come (servers_came).
  void apply(const start_call& one) {
    if (current_)
      return;
    // A build without calls: said so, and nothing sent -- it would only
    // fail, and the room be told it had.
    if (!mux::calls::kAvailable) {
      s_->notice("Calls aren't in this build",
                              "This mux was built without calls (MUX_CALLS=OFF). Configure it with -DMUX_CALLS=ON to "
                              "make and take voice calls; libdatachannel is taken from the system where it is installed.");
      return;
    }
    ended_.reset();
    current_.emplace(active_call{.in = one.in, .id = new_call_id(), .outgoing = true, .state = call_state::starting{}});
    // The microphone asked for now, where the system asks the user: the
    // sound opens once it is given (mux.calls.media tries again).
    (void)platform::permissions::microphone();
    s_->net->ask(one.in.account, ask_servers{});
    this->show();
  }
  // An ended call put away before its moment is up.
  void apply(const dismiss_call&) {
    if (current_)
      return;
    ended_.reset();
    shown_.reset();
    ui::show(*s_->showing, call_shown{.now = std::nullopt, .callable = callable_});
  }
  void apply(const call_chosen&) {
    if (const auto& chosen = s_->chosen())
      this->apply(start_call{*chosen});
  }
  void apply(const accept_call&) {
    if (!current_ || !holds<call_state::ringing_in>())
      return;
    current_->state = call_state::starting{};
    (void)platform::permissions::microphone();
    s_->net->ask(current_->in.account, ask_servers{});
    this->show();
  }
  void apply(const decline_call&) {
    if (!current_)
      return;
    this->signal(call_said_t{call_said::reject{}});
    this->end("You declined the call");
  }
  void apply(const hang_up&) {
    if (!current_)
      return;
    this->signal(call_said_t{call_said::hangup{call_end_t{call_end::hung_up{}}}});
    this->end("Call ended");
  }
  void apply(const mute_call&) {
    if (!current_ || !current_->media)
      return;
    current_->media->mute(!current_->media->muted());
    this->show();
  }

  // -- what accounts said
  void take(const call_signalled& one) {
    spl::visit(spl::overloaded{[&](const call_said::invite& invite) { this->invited(one, invite); },
                                     [&](const call_said::answer& answer) { this->answered(one, answer); },
                                     [&](const call_said::candidates& them) {
                                       if (!this->is_current(one) || one.mine)
                                         return;
                                       if (!current_->their_party.empty() && one.party != current_->their_party) return;
                                       if (current_->media && !holds<call_state::ringing_out>() && !holds<call_state::starting>())
                                         std::ranges::for_each(them.them, [&](const auto& each) { current_->media->add_candidate(each); });
                                       else
                                         current_->early[one.party].insert_range(current_->early[one.party].end(), them.them);
                                     },
                                     // Ended by them: why, as Element says it.
                                     [&](const call_said::hangup& done) {
                                       if (!this->is_current(one))
                                         return;
                                       this->end(spl::visit(
                                           spl::overloaded{
                                               [](call_end::hung_up) { return std::string("Call ended"); },
                                               [](call_end::busy) { return std::string("They're busy"); },
                                               [](call_end::timed_out) { return std::string("No answer"); },
                                               [](call_end::failed) { return std::string("Their connection failed"); },
                                               [](const call_end::other& said) { return "Call ended: " + said.said; }},
                                           done.why));
                                     },
                                     [&](const call_said::reject&) {
                                       if (this->is_current(one))
                                         this->end(one.mine ? "Declined on another device" : "They declined the call");
                                     },
                                     [&](const call_said::select_answer& chosen) {
                                       // Answered by another of the user's sessions: not this one's.
                                       if (this->is_current(one) && !current_->outgoing && !own_party_.empty() && chosen.party != own_party_)
                                         this->end("Answered on another device");
                                     }},
                  one.said);
  }
  void take(const call_servers& one) {
    if (!current_ || current_->in.account != one.account || !holds<call_state::starting>())
      return;
    own_party_ = one.party;
    current_->media = std::make_unique<session>(one.servers, *s_->wake);
    if (current_->outgoing) {
      current_->media->offer();
      current_->state = call_state::ringing_out{};
    } else {
      current_->media->answer(current_->offer);
      add_early_candidates();
      current_->state = call_state::connecting{};
      current_->ring_until = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    }
    this->show();
  }

  // Between events: what the connection said, the invite's time run out,
  // candidates sent a batch at a time.
  void tick() {
    // The chat shown, where it changed: whether it can be called, shown.
    if (const auto& chosen = s_->chosen(); chosen != callable_for_) {
      callable_for_ = chosen;
      this->show_callable();
    }
    // A call ended stays a moment, saying why; then it goes.
    if (ended_ && std::chrono::steady_clock::now() > ended_->until) {
      ended_.reset();
      shown_.reset();
      ui::show(*s_->showing, call_shown{.now = std::nullopt, .callable = callable_});
    }
    if (!current_) {
      if (ended_)
        this->show_view(ended_->view);
      return;
    }
    if (current_->media)
      std::ranges::for_each(current_->media->take(), [&](const mux::calls::said_t& said) { if (current_) this->connection_said(said); });
    if (current_ && !current_->candidates_out.empty())
      this->signal(call_said_t{call_said::candidates{std::exchange(current_->candidates_out, {})}});
    if (current_ && holds<call_state::ringing_in>() && std::chrono::steady_clock::now() > current_->ring_until)
      this->end("Missed call");
    if (current_ && holds<call_state::ringing_out>() && std::chrono::steady_clock::now() > current_->ring_until) {
      this->signal(call_said_t{call_said::hangup{call_end_t{call_end::timed_out{}}}});
      this->end("No answer");
    }
    if (current_ && (holds<call_state::starting>() || holds<call_state::connecting>()) &&
        std::chrono::steady_clock::now() > current_->ring_until) {
      this->signal(call_said_t{call_said::hangup{call_end::failed{}}});
      this->end("Connection timed out");
    }
    // Shown as it is now -- its time going on, the chat shown changed --
    // where that changed what is shown.
    this->show();
  }
  [[nodiscard]] bool in_call() const { return current_.has_value(); }

 private:
  using session = mux::calls::media_session<app::wake_window>;

  template <class State>
  [[nodiscard]] bool holds() const {
    return current_ && spl::visit(spl::overloaded{[](const State&) { return true; }, [](const auto&) { return false; }},
                                     current_->state);
  }
  // A signal of this call's, sent by its chat's account as its protocol
  // says it (mux.plugin.calls.matrix's, for Matrix).
  void signal(call_said_t said) {
    s_->net->ask(current_->in.account, send_signal{current_->in.id, current_->id, current_->version, std::move(said)});
  }
  [[nodiscard]] bool is_current(const call_signalled& one) const { return current_ && current_->in == one.in && current_->id == one.call; }

  void invited(const call_signalled& one, const call_said::invite& invite) {
    if (one.mine)
      return;
    // Busy: one call at a time.
    if (current_) {
      if (current_->id != one.call)
        s_->net->ask(one.in.account, send_signal{one.in.id, one.call, invite.version, call_said_t{call_said::hangup{call_end_t{call_end::busy{}}}}});
      return;
    }
    // Come too late: its time run out already.
    if (std::chrono::system_clock::now() > one.at + invite.lifetime)
      return;
    ended_.reset();
    current_.emplace(active_call{.in = one.in, .id = one.call, .outgoing = false, .state = call_state::ringing_in{},
                          .their_party = one.party, .offer = invite.offer, .version = invite.version});
    current_->ring_until = std::chrono::steady_clock::now() +
                           std::chrono::duration_cast<std::chrono::steady_clock::duration>(invite.lifetime);
    this->show();
  }
  void answered(const call_signalled& one, const call_said::answer& answer) {
    if (!this->is_current(one))
      return;
    // Answered by the user elsewhere, where they were called: not here.
    if (one.mine) {
      if (!current_->outgoing)
        this->end("Answered on another device");
      return;
    }
    if (!current_->outgoing || !holds<call_state::ringing_out>() || !current_->media)
      return;
    current_->their_party = one.party;
    current_->media->answered(answer.it);
    add_early_candidates();
    this->signal(call_said_t{call_said::select_answer{one.party}});
    current_->state = call_state::connecting{};
    current_->ring_until = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    this->show();
  }
  void add_early_candidates() {
    auto queued = std::exchange(current_->early, {});
    if (const auto found = queued.find(current_->their_party); found != queued.end())
      std::ranges::for_each(found->second, [&](const auto& candidate) { current_->media->add_candidate(candidate); });
  }
  void connection_said(const mux::calls::said_t& said) {
    spl::visit(spl::overloaded{[&](const mux::calls::said::description& ours) {
                                       spl::visit(spl::overloaded{[&](mux::calls::sdp_kind::offer) {
                                                                          this->signal(call_said_t{call_said::invite{ours.it, std::chrono::milliseconds(60000)}});
                                                                        },
                                                                        [&](mux::calls::sdp_kind::answer) {
                                                                          this->signal(call_said_t{call_said::answer{ours.it}});
                                                                        }},
                                                     ours.it.kind);
                                     },
                                     [&](const mux::calls::said::candidate& ours) { current_->candidates_out.push_back(ours.it); },
                                     [&](mux::calls::said::connected) {
                                       current_->state = call_state::connected{std::chrono::steady_clock::now()};
                                       this->show();
                                     },
                                     [&](mux::calls::said::failed) {
                                       this->signal(call_said_t{call_said::hangup{call_end_t{call_end::failed{}}}});
                                       // Where the two are on different networks, the
                                       // account's server has to offer a relay (TURN).
                                       this->end("Connection failed: no route between you, or no relay from your server");
                                     },
                                     [&](mux::calls::said::ended) { this->end("Call ended"); }},
                  said);
  }
  // The call over, and why: shown a moment, as Element's view says it.
  void end(std::string why) {
    if (current_) {
      auto view = this->view_of(*current_);
      view.phase = call_phase::ended{std::move(why)};
      ended_.emplace(ended_call{std::move(view), std::chrono::steady_clock::now() + std::chrono::seconds(4)});
    }
    current_.reset();
    if (ended_)
      this->show_view(ended_->view);
    else
      ui::show(*s_->showing, call_shown{.now = std::nullopt, .callable = callable_});
  }
  [[nodiscard]] call_view view_of(const active_call& now) const {
    const mux::conversation* chat = s_->model->find(now.in);
    const auto phase = spl::visit(
        spl::overloaded{[](call_state::starting) -> call_phase_t { return call_phase::connecting{}; },
                           [](call_state::ringing_out) -> call_phase_t { return call_phase::ringing_out{}; },
                           [](call_state::ringing_in) -> call_phase_t { return call_phase::ringing_in{}; },
                           [](call_state::connecting) -> call_phase_t { return call_phase::connecting{}; },
                           [](const call_state::connected& since) -> call_phase_t {
                             return call_phase::connected{
                                 std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - since.since)
                                     .count()};
                           }},
        now.state);
    return {.in = now.in,
            .who = chat ? mux::ui::display_name(*chat) : now.in.id,
            .phase = phase,
            .muted = now.media && now.media->muted(),
            .encrypted = chat && chat->encrypted,
            .available = mux::calls::kAvailable};
  }
  // The call, as it is now: in its chat where that is shown, else the card.
  void show() {
    if (current_)
      this->show_view(this->view_of(*current_));
  }
  // Pushed to the window only where it changed: its seconds, the chat shown.
  void show_view(call_view view) {
    view.in_view = s_->chosen() == view.in;
    view.whole = s_->single();
    if (shown_ == view)
      return;
    shown_ = view;
    ui::show(*s_->showing, call_shown{.now = view, .callable = callable_});
  }
  [[nodiscard]] static std::string new_call_id() {
    std::random_device entropy;
    return std::format("mux{:08x}{:08x}", entropy(), entropy());
  }

  struct ended_call {
    call_view view;
    std::chrono::steady_clock::time_point until;
  };
  app::services* s_;
  std::optional<active_call> current_;
  std::optional<ended_call> ended_;
  std::optional<call_view> shown_;
  // Whether the chat shown can be called -- what its head's button is shown
  // by -- and for which chat that was worked out.
  bool callable_ = false;
  std::optional<conversation_id> callable_for_;
  void show_callable() {
    const mux::conversation* one = callable_for_ ? s_->model->find(*callable_for_) : nullptr;
    const auto count = one ? std::max<std::int64_t>(static_cast<std::int64_t>(one->members.size()), one->member_count) : 0;
    callable_ = one && ui::ops_of(s_->ui, one->id.account).calls && count <= 2;
    ui::show(*s_->showing, call_shown{.now = shown_, .callable = callable_});
  }
  std::string own_party_;
};

// Its part, among the program's (mux.app.program): it takes what the
// accounts say of calls, and is ticked between events.
inline part extend(contract::program_parts, type_tag<feature>, app::services& shared) { return part{shared}; }

}  // namespace mux::plugins::calls
