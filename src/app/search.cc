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
    s_->scene->focus(screen.search.field);
  }
  void apply(const request::close_search&) {
    searching_.reset();
    s_->root().main().show_search(false);
  }
  void apply(const request::search_typed& one) {
    if (!searching_)
      return;
    searching_->query = one.text;
    searching_->found = this->find_in(searching_->in, one.text);
    searching_->at.reset();
    this->step(true);
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

  // What is found in a chat: in all it has, those on disk and those in
  // memory.
  std::vector<std::string> find_in(const conversation_id& in, std::string_view query) {
    std::map<std::string, message> all = s_->store->everything(in);
    if (const conversation* chat = s_->model->find(in))
      for (const message& one : chat->timeline)
        all.insert_or_assign(one.id, one);
    return logic::found_in(all, query);
  }

  services* s_;
  std::optional<state> searching_;
};

}  // namespace mux::app
