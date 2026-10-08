// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.search: finding in a chat -- what is asked, in which chat, the
// messages it is found in, and which of them is shown -- as tdesktop's
// search in a chat.
export module mux.app.search;

import std;
import mux.logic.text;
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
    s_->root().main().show_search_results({}, std::nullopt);
    mux::ui::show(*s_->showing, mux::ui::search_found{});
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
      std::map<std::string, message> all = s_->store->everything(in);
      for (const message& each : in_memory)
        all.insert_or_assign(each.id, each);
      auto found = logic::found_in(all, query);
      std::vector<message> messages;
      messages.reserve(found.size());
      for (const std::string& id : found)
        if (const auto at = all.find(id); at != all.end())
          messages.push_back(at->second);
      return [this, in, query, found = std::move(found), messages = std::move(messages)]() mutable {
        if (!searching_ || searching_->in != in || searching_->query != query)
          return;
        searching_->found = std::move(found);
        searching_->messages = std::move(messages);
        searching_->at.reset();
        this->list();
        this->step(true);
      };
    });
  }
  void apply(const request::search_step& one) { this->step(one.older); }
  // One of the list picked: gone to, as a step to it.
  void apply(const request::search_pick& one) {
    if (!searching_ || one.index >= searching_->found.size())
      return;
    searching_->at = one.index;
    this->show_at();
  }

  // Another chat chosen: finding is in one chat, and is closed.
  void chat_chosen(const conversation_id& which) {
    if (searching_ && searching_->in != which) {
      searching_.reset();
      s_->root().main().show_search(false);
      s_->root().main().show_search_results({}, std::nullopt);
      mux::ui::show(*s_->showing, mux::ui::search_found{});
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
    std::vector<message> messages;  // the found, as messages, for their list
  };
  // The found, listed down the side as tdesktop lists them: who, when, and
  // their words around what was asked.
  void list() {
    const conversation* chat = s_->model->find(searching_->in);
    constexpr auto lower = mux::logic::folded;
    const std::string asked = lower(searching_->query);
    std::vector<ui::search_result> rows;
    rows.reserve(searching_->messages.size());
    for (std::size_t i = 0; i < searching_->messages.size(); ++i) {
      const message& one = searching_->messages[i];
      std::string text = one.body.plain;
      std::ranges::replace(text, '\n', ' ');
      // From a little before what was found, where it is far in.
      if (const auto at = lower(text).find(asked); at != std::string::npos && at > 30) {
        std::size_t from = at - 20;
        while (from > 0 && (static_cast<unsigned char>(text[from]) & 0xC0) == 0x80)
          --from;
        text = "\u2026" + text.substr(from);
      }
      rows.push_back({i, one.sender, chat ? ui::sender_name(*chat, one.sender) : one.sender, one.at, std::move(text)});
    }
    const std::size_t count = rows.size();
    s_->root().main().show_search_results(std::move(rows),
                                          searching_->query.empty() ? std::nullopt : std::optional<std::size_t>(count));
  }
  // The found at `at` gone to, what was asked marked in it; the count set.
  void show_at() {
    auto& screen = s_->root().main();
    if (searching_->at)
      screen.jump_to(searching_->found[*searching_->at], searching_->query);
    mux::ui::show(*s_->showing, mux::ui::search_found{searching_->at, searching_->found.size(), !searching_->query.empty()});
  }

  // To the next found, older or newer, shown and flashed; the count set.
  void step(bool older) {
    if (!searching_)
      return;
    auto& screen = s_->root().main();
    (void)screen;
    searching_->at = logic::stepped(searching_->at, searching_->found.size(), older);
    this->show_at();
  }

  services* s_;
  std::optional<state> searching_;
};

}  // namespace mux::app
