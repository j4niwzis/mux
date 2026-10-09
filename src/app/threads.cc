// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.threads: threads, as Element's panel -- opened in place of the
// chat's info, the room's listed by the server as it opens; one opened, its
// answers fetched (and its root, where it is not held); an answer sent in
// the one open -- falling back, for clients without threads, to its latest
// event. A part of the program: it reaches the rest through the services.
export module mux.app.threads;

import std;
import mux.core;
import mux.ui;
import mux.ui.proto;
import mux.app.network;
import mux.app.services;
import mux.app.requests;

export namespace mux::app {

class threads_part {
 public:
  explicit threads_part(services& shared) : s_(&shared) {}
  threads_part(const threads_part&) = delete;
  threads_part& operator=(const threads_part&) = delete;

  // Threads, as Element's panel: opened in place of the chat's info, the
  // room's listed by the server as it opens; one opened, its answers fetched
  // (and its root, where it is not held); an answer sent in the one open --
  // falling back, for clients without threads, to its latest event.
  void apply(const request::toggle_threads&) {
    auto& screen = s_->root().main();
    if (s_->toggle_threads() && screen.chosen && !s_->demo())
      s_->net->list_threads(*screen.chosen);
    s_->refresh_due = true;
  }
  void apply(const request::open_thread& one) {
    auto& screen = s_->root().main();
    if (!screen.chosen)
      return;
    s_->open_thread(one.root);
    if (!s_->demo()) {
      s_->net->load_thread(*screen.chosen, one.root);
      if (const mux::conversation* chat = s_->model->find(*screen.chosen);
          chat && !chat->quoted.contains(one.root) &&
          std::ranges::find(chat->timeline, one.root, &mux::message::id) == chat->timeline.end())
        s_->net->fetch_quoted(*screen.chosen, one.root);
    }
    s_->refresh_due = true;
  }
  void apply(const request::close_thread&) {
    s_->close_thread();
    s_->refresh_due = true;
  }
  void apply(const request::send_in_thread& one) {
    const auto chosen = s_->managed();
    const mux::conversation* chat = chosen ? s_->model->find(*chosen) : nullptr;
    if (!chat || s_->demo())
      return;
    std::string latest = one.root;
    if (const auto found = chat->threads.find(one.root); found != chat->threads.end() && !found->second.empty())
      latest = found->second.back().id;
    s_->net->send_in_thread(*chosen, one.text, one.root, latest, one.reply_to);
  }

 private:
  services* s_;
};

}  // namespace mux::app
