// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.calls: a call -- one at a time -- made and taken: what its
// protocol said in it (change::call_signalled) and what its connection says
// (mux.calls.media), brought together, and shown in the window's call bar.
// A part of the program: it owns its state, and reaches the rest through the
// services it is given.
export module mux.app.calls;

import std;
import splice;
import mux.core;
import mux.ui;
import mux.calls.media;
import mux.platform.permissions;
import mux.app.network;
import mux.app.services;
import mux.app.requests;

export namespace mux::app {

class calls_part {
 public:
  explicit calls_part(services& shared) : s_(&shared) {}
  calls_part(const calls_part&) = delete;
  calls_part& operator=(const calls_part&) = delete;

  // -- what the user asks
  // A call made to the chat: the account's servers asked for first; the
  // offer made once they come (servers_came).
  void apply(const request::start_call& one) {
    if (current_)
      return;
    current_.emplace(call{.in = one.in, .id = new_call_id(), .outgoing = true, .state = state::starting{}});
    // The microphone asked for now, where the system asks the user: the
    // sound opens once it is given (mux.calls.media tries again).
    (void)platform::permissions::microphone();
    s_->net->call_servers(one.in.account);
    this->show();
  }
  void apply(const request::call_chosen&) {
    if (const auto& chosen = s_->root().main().chosen)
      this->apply(request::start_call{*chosen});
  }
  void apply(const request::accept_call&) {
    if (!current_ || !holds<state::ringing_in>())
      return;
    current_->state = state::starting{};
    (void)platform::permissions::microphone();
    s_->net->call_servers(current_->in.account);
    this->show();
  }
  void apply(const request::decline_call&) {
    if (!current_)
      return;
    s_->net->call(current_->in, current_->id, change::call_said_t{change::call_said::reject{}});
    this->end();
  }
  void apply(const request::hang_up&) {
    if (!current_)
      return;
    s_->net->call(current_->in, current_->id,
                  change::call_said_t{change::call_said::hangup{change::call_end_t{change::call_end::hung_up{}}}});
    this->end();
  }
  void apply(const request::mute_call&) {
    if (!current_ || !current_->media)
      return;
    current_->media->mute(!current_->media->muted());
    this->show();
  }

  // -- what accounts said
  void take(const change::call_signalled& one) {
    splice::visit(splice::overloaded{[&](const change::call_said::invite& invite) { this->invited(one, invite); },
                                     [&](const change::call_said::answer& answer) { this->answered(one, answer); },
                                     [&](const change::call_said::candidates& them) {
                                       if (!this->is_current(one) || one.mine)
                                         return;
                                       if (current_->media)
                                         std::ranges::for_each(them.them, [&](const auto& each) { current_->media->add_candidate(each); });
                                       else
                                         current_->early.insert_range(current_->early.end(), them.them);
                                     },
                                     [&](const change::call_said::hangup&) {
                                       if (this->is_current(one))
                                         this->end();
                                     },
                                     [&](const change::call_said::reject&) {
                                       if (this->is_current(one))
                                         this->end();
                                     },
                                     [&](const change::call_said::select_answer& chosen) {
                                       // Answered by another of the user's sessions: not this one's.
                                       if (this->is_current(one) && !current_->outgoing && chosen.party != own_party_)
                                         this->end();
                                     }},
                  one.said);
  }
  void take(const change::call_servers& one) {
    if (!current_ || current_->in.account != one.account || !holds<state::starting>())
      return;
    current_->media = std::make_unique<session>(one.servers, *s_->wake);
    std::ranges::for_each(std::exchange(current_->early, {}), [&](const auto& each) { current_->media->add_candidate(each); });
    if (current_->outgoing) {
      current_->media->offer();
      current_->state = state::ringing_out{};
    } else {
      current_->media->answer(current_->offer);
      current_->state = state::connecting{};
    }
    this->show();
  }

  // Between events: what the connection said, the invite's time run out,
  // candidates sent a batch at a time.
  void tick() {
    if (!current_)
      return;
    if (current_->media)
      std::ranges::for_each(current_->media->take(), [&](const calls::said_t& said) { this->connection_said(said); });
    if (current_ && !current_->candidates_out.empty())
      s_->net->call(current_->in, current_->id,
                    change::call_said_t{change::call_said::candidates{std::exchange(current_->candidates_out, {})}});
    if (current_ && holds<state::ringing_in>() && std::chrono::steady_clock::now() > current_->ring_until)
      this->end();
    if (current_ && holds<state::ringing_out>() && std::chrono::steady_clock::now() > current_->ring_until) {
      s_->net->call(current_->in, current_->id,
                    change::call_said_t{change::call_said::hangup{change::call_end_t{change::call_end::timed_out{}}}});
      this->end();
    }
  }
  [[nodiscard]] bool in_call() const { return current_.has_value(); }

 private:
  using session = calls::media_session<wake_window>;
  // Where a call is.
  struct state {
    struct starting {};      // the servers asked for
    struct ringing_out {};   // invited, not answered yet
    struct ringing_in {};    // invited by them
    struct connecting {};    // answered: the connection being made
    struct connected {
      std::chrono::steady_clock::time_point since;
    };
  };
  using state_t = splice::variant<state::starting, state::ringing_out, state::ringing_in, state::connecting, state::connected>;
  struct call {
    conversation_id in;
    std::string id;
    bool outgoing = false;
    state_t state;
    std::string their_party;
    calls::session_description offer;  // theirs, where they called
    std::vector<calls::ice_candidate> early;           // theirs, come before the connection
    std::vector<calls::ice_candidate> candidates_out;  // ours, not sent yet
    std::chrono::steady_clock::time_point ring_until = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    std::unique_ptr<session> media;
  };

  template <class State>
  [[nodiscard]] bool holds() const {
    return current_ && splice::visit(splice::overloaded{[](const State&) { return true; }, [](const auto&) { return false; }},
                                     current_->state);
  }
  [[nodiscard]] bool is_current(const change::call_signalled& one) const { return current_ && current_->id == one.call; }

  void invited(const change::call_signalled& one, const change::call_said::invite& invite) {
    if (one.mine)
      return;
    // Busy: one call at a time.
    if (current_) {
      if (current_->id != one.call)
        s_->net->call(one.in, one.call,
                      change::call_said_t{change::call_said::hangup{change::call_end_t{change::call_end::busy{}}}});
      return;
    }
    // Come too late: its time run out already.
    if (std::chrono::system_clock::now() > one.at + invite.lifetime)
      return;
    current_.emplace(call{.in = one.in, .id = one.call, .outgoing = false, .state = state::ringing_in{},
                          .their_party = one.party, .offer = invite.offer});
    current_->ring_until = std::chrono::steady_clock::now() +
                           std::chrono::duration_cast<std::chrono::steady_clock::duration>(invite.lifetime);
    this->show();
  }
  void answered(const change::call_signalled& one, const change::call_said::answer& answer) {
    if (!this->is_current(one))
      return;
    // Answered by the user elsewhere, where they were called: not here.
    if (one.mine) {
      if (!current_->outgoing)
        this->end();
      return;
    }
    if (!current_->outgoing || !holds<state::ringing_out>() || !current_->media)
      return;
    current_->their_party = one.party;
    current_->media->answered(answer.it);
    s_->net->call(current_->in, current_->id, change::call_said_t{change::call_said::select_answer{one.party}});
    current_->state = state::connecting{};
    this->show();
  }
  void connection_said(const calls::said_t& said) {
    splice::visit(splice::overloaded{[&](const calls::said::description& ours) {
                                       splice::visit(splice::overloaded{[&](calls::sdp_kind::offer) {
                                                                          s_->net->call(current_->in, current_->id,
                                                                                        change::call_said_t{change::call_said::invite{ours.it, std::chrono::milliseconds(60000)}});
                                                                        },
                                                                        [&](calls::sdp_kind::answer) {
                                                                          s_->net->call(current_->in, current_->id,
                                                                                        change::call_said_t{change::call_said::answer{ours.it}});
                                                                        }},
                                                     ours.it.kind);
                                     },
                                     [&](const calls::said::candidate& ours) { current_->candidates_out.push_back(ours.it); },
                                     [&](calls::said::connected) {
                                       current_->state = state::connected{std::chrono::steady_clock::now()};
                                       this->show();
                                     },
                                     [&](calls::said::failed) {
                                       s_->net->call(current_->in, current_->id,
                                                     change::call_said_t{change::call_said::hangup{change::call_end_t{change::call_end::failed{}}}});
                                       this->end();
                                     },
                                     [&](calls::said::ended) { this->end(); }},
                  said);
  }
  void end() {
    current_.reset();
    s_->root().hide_call();
  }
  // The call bar, as the call is now.
  void show() {
    if (!current_)
      return;
    const mux::conversation* chat = s_->model->find(current_->in);
    const std::string who = chat ? mux::ui::display_name(*chat) : current_->in.id;
    const auto phase = splice::visit(
        splice::overloaded{[](state::starting) -> mux::ui::call_phase_t { return mux::ui::call_phase::connecting{}; },
                           [](state::ringing_out) -> mux::ui::call_phase_t { return mux::ui::call_phase::ringing_out{}; },
                           [](state::ringing_in) -> mux::ui::call_phase_t { return mux::ui::call_phase::ringing_in{}; },
                           [](state::connecting) -> mux::ui::call_phase_t { return mux::ui::call_phase::connecting{}; },
                           [](const state::connected& now) -> mux::ui::call_phase_t {
                             return mux::ui::call_phase::connected{now.since};
                           }},
        current_->state);
    s_->root().show_call(mux::ui::call_view{.who = who,
                                            .phase = phase,
                                            .muted = current_->media && current_->media->muted(),
                                            .encrypted = chat && chat->encrypted,
                                            .available = calls::kAvailable});
  }
  [[nodiscard]] static std::string new_call_id() {
    std::random_device entropy;
    return std::format("mux{:08x}{:08x}", entropy(), entropy());
  }

  services* s_;
  std::optional<call> current_;
  std::string own_party_;
};

}  // namespace mux::app
