// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.reading: what the user has read, and that they are typing. Their
// own reads are always kept -- in the model and on disk, which is what the
// unread counts come from -- and told to the server only where the
// account's privacy lets it; so is typing, at most every twenty seconds
// while it goes on.
export module mux.app.reading;

import std;
import mux.core;
import mux.config;
import mux.ui;
import mux.app.network;
import mux.app.store;
import mux.app.requests;
import mux.app.services;

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
    for (auto it = one->timeline.rbegin(); it != one->timeline.rend(); ++it)
      if (!it->outgoing && !it->id.empty()) {
        if (one->read_up_to == it->id)
          return;
        const std::string id = it->id;
        s_->model->read_up_to(which, id);
        s_->store->keep_reads(which, *s_->model->find(which));
        if (const auto* account = s_->settings_of(which.account.address);
            account && mux::config::read_receipts_of(*account))
          s_->net->mark_read(which, id);
        return;
      }
  }

  // The user typing in the chosen chat, or not: said, where the account's
  // privacy lets it -- 'typing' at most every twenty seconds while it goes
  // on, and 'stopped' when it stops or the chat is left.
  void apply(const request::typing& one) {
    if (s_->demo())
      return;
    const auto& chosen = s_->root().main().chosen;
    const auto now = std::chrono::steady_clock::now();
    const auto allowed = [&](const conversation_id& in) {
      const auto* account = s_->settings_of(in.account.address);
      return account && mux::config::send_typing_of(*account);
    };
    if (typing_in_ && (!one.on || typing_in_ != chosen)) {
      if (allowed(*typing_in_))
        s_->net->typing(*typing_in_, false);
      typing_in_.reset();
    }
    if (one.on && chosen && allowed(*chosen) &&
        (typing_in_ != chosen || now - typing_said_ > std::chrono::seconds(20))) {
      s_->net->typing(*chosen, true);
      typing_in_ = chosen;
      typing_said_ = now;
    }
  }

 private:
  services* s_;
  std::optional<conversation_id> typing_in_;
  std::chrono::steady_clock::time_point typing_said_{};
};

}  // namespace mux::app
