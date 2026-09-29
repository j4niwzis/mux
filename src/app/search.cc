// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.search: finding in a chat -- what is asked, in which chat, the
// messages it is found in, and which of them is shown -- as tdesktop's
// search in a chat.
export module mux.app.search;

import std;
import mux.core;
import mux.ui;
import mux.app.store;
import mux.app.requests;
import mux.app.services;
import mux.app.workers;
import mux.logic.search;

export namespace mux::app {

class search_part {
 public:
  explicit search_part(services& shared) : s_(&shared) {}

  void apply(const request::open_search&) {
    auto& screen = s_->root().main();
    if (!screen.chosen)
      return;
    if (!searching_ || searching_->in != *screen.chosen)
      searching_ = state{*screen.chosen, {}, {}, std::nullopt};
    screen.show_search(true);
    s_->scene->focus(screen.search.parts.field);
  }
  void apply(const request::close_search&) {
    searching_.reset();
    s_->root().main().show_search(false);
  }
  void apply(const request::search_typed& one) {
    if (!searching_)
      return;
    searching_->query = one.text;
    // Found on a worker, over the disk and a copy of what is in memory; shown
    // where the same is still asked in the same chat.
    std::vector<message> in_memory;
    if (const conversation* chat = s_->model->find(searching_->in))
      in_memory = chat->timeline;
    s_->work->run([this, in = searching_->in, query = one.text, in_memory = std::move(in_memory)]() -> workers::done_t {
      std::map<std::string, message> all = message_store::everything(in);
      for (const message& each : in_memory)
        all.insert_or_assign(each.id, each);
      auto found = logic::found_in(all, query);
      return [this, in, query, found = std::move(found)]() mutable {
        if (!searching_ || searching_->in != in || searching_->query != query)
          return;
        searching_->found = std::move(found);
        searching_->at.reset();
        this->step(true);
      };
    });
  }
  void apply(const request::search_step& one) { this->step(one.older); }

  // Another chat chosen: finding is in one chat, and is closed.
  void chat_chosen(const conversation_id& which) {
    if (searching_ && searching_->in != which) {
      searching_.reset();
      s_->root().main().show_search(false);
    }
  }

 private:
  // What is asked, in which chat, the messages it is in (newest first), and
  // which of them is shown.
  struct state {
    conversation_id in;
    std::string query;
    std::vector<std::string> found;
    std::optional<std::size_t> at;
  };

  // To the next found, older or newer, shown and flashed; the count set.
  void step(bool older) {
    if (!searching_)
      return;
    auto& screen = s_->root().main();
    searching_->at = logic::stepped(searching_->at, searching_->found.size(), older);
    if (searching_->at)
      screen.jump_to(searching_->found[*searching_->at]);
    screen.search.show_found(searching_->at, searching_->found.size(), !searching_->query.empty());
  }

  services* s_;
  std::optional<state> searching_;
};

}  // namespace mux::app
