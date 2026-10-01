// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.reading: what the user has read, and that they are typing. Their
// own reads are always kept -- in the model and on disk, which is what the
// unread counts come from -- and told to the server only where the
// account's privacy lets it; so is typing, at most every twenty seconds
// while it goes on.
export module mux.app.reading;

import std;
import splice;
import mux.core;
import mux.config;
import mux.ui;
import mux.app.network;
import mux.app.store;
import mux.app.requests;
import mux.app.services;
import mux.logic.reading;

export namespace mux::app {

class reading_part {
 public:
  explicit reading_part(services& shared) : s_(&shared) {}

  // A chat read to its newest message from someone else: kept, and sent
  // where the account's privacy lets it.
  void mark_read(const conversation_id& which) {
    if (s_->demo())
      return;
    const conversation* one = s_->model->find(which);
    if (!one)
      return;
    if (const auto id = logic::to_mark_read(*one))
      this->read_to(which, *id);
  }

  // A chat read as far as the user has seen it on screen: true where that
  // moved what is read, and the counts are to be shown again.
  bool mark_seen(const conversation_id& which, std::string_view seen) {
    if (s_->demo())
      return false;
    const conversation* one = s_->model->find(which);
    if (!one)
      return false;
    const auto id = logic::read_up_to_seen(*one, seen);
    if (!id)
      return false;
    this->read_to(which, *id);
    return true;
  }

  // Read up to a message: kept, and sent where the account's privacy lets
  // it.
  void read_to(const conversation_id& which, const std::string& id) {
    s_->model->read_up_to(which, id);
    s_->store->keep_reads(which, *s_->model->find(which));
    if (const auto* account = s_->kept->settings_of(which.account.address);
        account && mux::config::read_receipts_of(*account))
      s_->net->mark_read(which, id);
  }

  // The user typing in the chosen chat, or not: said as the logic of it
  // says, where the account's privacy lets it.
  void apply(const request::typing& one) {
    if (s_->demo())
      return;
    // As the chat, its space, its account or every account says.
    const auto allowed = [&](const conversation_id& in) { return s_->kept->typing_sent(in); };
    auto step = logic::typing_after(typing_, one.on, s_->root().main().chosen, std::chrono::steady_clock::now(), allowed);
    for (const auto& said : step.say)
      splice::visit(splice::overloaded{[&](const logic::typing_said::started& it) { s_->net->typing(it.in, true); },
                            [&](const logic::typing_said::stopped& it) { s_->net->typing(it.in, false); }},
                 said);
    typing_ = step.next;
  }

 private:
  services* s_;
  logic::typing_state typing_;
};

}  // namespace mux::app
