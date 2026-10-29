// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.search.app -- Finding in a chat: what is asked, in which chat,
// the messages it is found in (listed down the side, the program's
// found_shown), and which of them is shown -- as tdesktop's search in a chat.
export module mux.plugin.search.app;

import std;
import mux.composition;
import mux.core;
import mux.logic.text;
import mux.logic.search;
import mux.ui;
import mux.app.store;
import mux.app.workers;
import mux.app.services;
import mux.plugin.search.types;
import mux.plugin.search.ui;

export namespace mux::plugins::search {

struct part {
  app::services* s_ = nullptr;

  // What the bar says, put where it changed; and the program told whether
  // it is open (Esc closes it, the head stands aside for it).
  void put(shown now) {
    s_->searching = now.open;
    if (*s_->showing->look<shown>() != now)
      ui::show(*s_->showing, std::move(now));
  }
  // Another chat chosen: finding is in one chat, and is closed.
  void tick() {
    if (const auto& chosen = s_->chosen(); chosen)
      this->chat_chosen(*chosen);
  }

  void apply(const ui::request::open_search&) {
    if (!s_->chosen())
      return;
    if (!searching_ || searching_->in != *s_->chosen())
      searching_ = state{*s_->chosen(), {}, {}, std::nullopt};
    this->put(shown{.open = true});
    s_->ask_keys(mux::ui::keys::search{});
  }
  void apply(const ui::request::close_search&) {
    searching_.reset();
    mux::ui::show(*s_->showing, mux::ui::found_shown{});
    this->put(shown{});
  }
  void apply(const ui::request::search_typed& one) {
    if (!searching_)
      return;
    searching_->query = one.text;
    // Found on a worker, over the disk and a copy of what is in memory; shown
    // where the same is still asked in the same chat.
    std::vector<message> in_memory;
    if (const conversation* chat = s_->model->find(searching_->in))
      in_memory = chat->timeline;
    s_->work->run([this, in = searching_->in, query = one.text, in_memory = std::move(in_memory)]() -> app::workers::done_t {
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
  void apply(const ui::request::search_step& one) { this->step(one.older); }
  // One of the list picked: gone to, as a step to it.
  void apply(const ui::request::search_pick& one) {
    if (!searching_ || one.index >= searching_->found.size())
      return;
    searching_->at = one.index;
    this->show_at();
  }

  // Another chat chosen: finding is in one chat, and is closed.
  void chat_chosen(const conversation_id& which) {
    if (searching_ && searching_->in != which) {
      searching_.reset();
    mux::ui::show(*s_->showing, mux::ui::found_shown{});
      this->put(shown{});
    }
  }

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
    const auto counted = searching_->query.empty() ? std::nullopt : std::optional<std::size_t>(count);
    mux::ui::show(*s_->showing, mux::ui::found_shown{counted, rows});
  }
  // The found at `at` gone to, what was asked marked in it; the count set.
  void show_at() {
    if (searching_->at)
      s_->show_message_due = app::services::message_asked{searching_->found[*searching_->at], searching_->query};
    this->put(shown{searching_->at, searching_->found.size(), !searching_->query.empty(), true});
  }

  // To the next found, older or newer, shown and flashed; the count set.
  void step(bool older) {
    if (!searching_)
      return;
    auto& screen = s_->root();
    (void)screen;
    searching_->at = logic::stepped(searching_->at, searching_->found.size(), older);
    this->show_at();
  }

  std::optional<state> searching_;
};

// Its part, among the program's (mux.app.program).
inline part extend(contract::program_parts, type_tag<feature>, app::services& shared) { return part{&shared}; }

}  // namespace mux::plugins::search
