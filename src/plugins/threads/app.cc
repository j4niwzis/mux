// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.threads.app -- The threads' part of the program: the panel
// opened in place of the chat's info, the room's threads listed by the
// server as it opens; one opened, its answers fetched (and its root, where
// it is not held); an answer sent in the one open -- falling back, for
// clients without threads, to its latest event.
export module mux.plugin.threads.app;

import std;
import skiff.model;
import skiff.compose;
import mux.composition;
import mux.core;
import mux.ui;
import mux.app.network;
import mux.app.services;
import mux.plugin.threads.types;
import mux.plugin.threads.model;
import mux.plugin.threads.ui;      // what it is asked: close_aside, thread_send
import mux.plugin.threads.ui_top;
import mux.plugin.threads.matrix;

export namespace mux::plugins::threads {

// The thread's panel at its top: the page of answers before those held.
using thread_edge = skiff::compose::ListEdge<mux::ui::aside_list_tag>;

struct part {
  app::services* s_ = nullptr;
  std::optional<std::string> answered_in_;
  // The threads whose page of answers is on its way.
  std::vector<std::pair<mux::conversation_id, std::string>> loading_;
  std::tuple<std::optional<mux::conversation_id>, skiff::model::Revision, std::optional<std::string>> seen_;

  // Threads, as Element's panel: opened in place of the chat's info, the
  // room's listed by the server as it opens; one opened, its answers fetched
  // (and its root, where it is not held); an answer sent in the one open --
  // falling back, for clients without threads, to its latest event.
  void apply(const ui::request::toggle_panel&) {
    if (s_->toggle_panel() && s_->chosen() && !s_->demo())
      s_->net->ask(s_->chosen()->account, list_asked{s_->chosen()->id});
    s_->refresh_due = true;
  }
  void apply(const ui::request::open_aside& one) {
    if (!s_->chosen())
      return;
    s_->open_aside(one.root);
    s_->refresh_due = true;
  }
  void apply(const close_aside&) {
    s_->close_aside();
    s_->refresh_due = true;
  }
  // What is written in the thread's field, sent in the thread open -- an
  // answer to what is answered there, where something is -- and the field
  // emptied.
  void apply(const thread_send&) {
    const auto& shown = *s_->showing->look<mux::ui::chat_shown>();
    const std::string text = mux::ui::composer_plain(s_->aside_writing());
    if (!shown.aside || text.empty())
      return;
    // A message of it edited there: the edit sent, not an answer.
    if (shown.aside_edit) {
      const std::string id = *shown.aside_edit;
      auto styles = mux::ui::composer_styles(s_->aside_writing());
      const auto chosen_now = s_->managed();
      const mux::conversation* chat = chosen_now ? s_->model->find(*chosen_now) : nullptr;
      auto mentions = mux::ui::composer_mentions(s_->aside_writing(), chat ? chat->members : std::vector<mux::member>{});
      s_->write_aside({});
      s_->stop_aside_answer();
      if (const auto chosen = s_->managed()) {
        if (s_->demo())
          s_->box->push(mux::change_t{mux::change::message_edited{*chosen, id, mux::body{text, std::nullopt}}});
        else
          s_->net->edit(*chosen, id, text, std::move(styles), std::move(mentions));
      }
      return;
    }
    const auto chosen_now = s_->managed();
    const mux::conversation* chat = chosen_now ? s_->model->find(*chosen_now) : nullptr;
    send_in_thread asked{*shown.aside, text, shown.aside_answer, mux::ui::composer_styles(s_->aside_writing()),
                                      mux::ui::composer_mentions(s_->aside_writing(), chat ? chat->members : std::vector<mux::member>{})};
    s_->write_aside({});
    s_->stop_aside_answer();
    this->apply(asked);
  }
  void apply(const ui::request::aside_answer_cancel&) { s_->stop_aside_answer(); }
  // The thread's field edited: what is written, kept.
  void apply(const ui::request::aside_draft_edited& one) {
    s_->written_last = ui::request::writing::aside{};
    s_->write_aside(one.now);
  }

  // The panel as the chat is: the thread open -- an answer being written
  // let go where it changes -- or the room's threads, each root once, the
  // latest active first: those the server listed, those in view with a
  // thread, and those threads are held of. Worked out again only where the
  // chat moved or the thread open changed.
  void tick() {
    const auto& chat_now = *s_->showing->look<mux::ui::chat_shown>();
    if (chat_now.aside != answered_in_) {
      answered_in_ = chat_now.aside;
      s_->stop_aside_answer();
      // A thread opened, from wherever it was: its newest page of answers
      // fetched, and its root where it is not held.
      if (chat_now.aside && chat_now.chosen)
        this->opened(*chat_now.chosen, *chat_now.aside);
    }
    const mux::conversation* chat = chat_now.chosen && chat_now.panel_open ? s_->model->find(*chat_now.chosen) : nullptr;
    const auto key = std::tuple{chat ? std::optional(chat->id) : std::nullopt, chat ? this->revision_of(chat->id) : 0, chat_now.aside};
    if (key == seen_)
      return;
    seen_ = key;
    threads::shown now{.listing = !chat_now.aside};
    if (chat && !chat_now.aside) {
      std::vector<const mux::message*> roots;
      const auto add = [&](const std::string& id) {
        if (const mux::message* one = mux::ui::held_message(*chat, id); one && !std::ranges::contains(roots, one))
          roots.push_back(one);
      };
      const auto& mine = std::get<threads::chat_part<mux::message>>(chat->plugins);
      for (const std::string& id : mine.roots)
        add(id);
      for (const mux::message& one : chat->timeline)
        if (threads::kept_about(mine, one))
          add(one.id);
      for (const std::string& id : mine.answers | std::views::keys)
        add(id);
      const auto latest = [](const mux::message* one) {
        const auto& threaded = threads::part_of(*one).threaded;
        return threaded ? threaded->last_at : one->at;
      };
      std::ranges::sort(roots, std::ranges::greater{}, latest);
      now.roots = roots | std::views::transform([&](const mux::message* one) {
                    return thread_root{one->id, one->sender, mux::ui::sender_name(*chat, one->sender),
                                                flat_line(one->body.plain), thread_meta_of(*chat, *one)};
                  }) | std::ranges::to<std::vector>();
    }
    if (*s_->showing->look<threads::shown>() != now)
      mux::ui::show(*s_->showing, std::move(now));
  }

  void apply(const send_in_thread& one) {
    const auto chosen = s_->managed();
    const mux::conversation* chat = chosen ? s_->model->find(*chosen) : nullptr;
    if (!chat || s_->demo())
      return;
    s_->net->send(*chosen, one.text, one.reply_to, one.mentions, one.styles, mux::placed_aside(*chat, one.root));
  }

  void opened(const mux::conversation_id& in, const std::string& root) {
    if (s_->demo())
      return;
    this->load(in, root, true);
    if (const mux::conversation* chat = s_->model->find(in); chat && !mux::ui::held_message(*chat, root))
      s_->net->fetch_quoted(in, root);
  }
  // A page of a thread's answers asked for: its newest (fresh), or the one
  // before those held -- none once all are, nor while one is on its way.
  void load(const mux::conversation_id& in, const std::string& root, bool fresh) {
    const mux::conversation* chat = s_->model->find(in);
    if (!chat || s_->demo())
      return;
    const auto& pages = std::get<chat_part<mux::message>>(chat->plugins).pages;
    const auto at = pages.find(root);
    if ((!fresh && at != pages.end() && !at->second) || std::ranges::contains(loading_, std::pair{in, root}))
      return;
    loading_.emplace_back(in, root);
    s_->net->ask(in.account, answers_asked{in.id, root, fresh || at == pages.end() ? std::nullopt : at->second});
  }
  void take(const paged& one) { std::erase(loading_, std::pair{one.in, one.root}); }
  void apply(const thread_edge& one) {
    const auto& shown = *s_->showing->look<mux::ui::chat_shown>();
    if (one.older && shown.chosen && shown.aside)
      this->load(*shown.chosen, *shown.aside, false);
  }

  [[nodiscard]] skiff::model::Revision revision_of(const mux::conversation_id& chat) const {
    const auto* in = s_->model->accounts().findItem(chat.account);
    const auto* one = in ? in->fValue.conversations.findItem(chat.id) : nullptr;
    return one ? one->fRevision : 0;
  }

};

// Its part, among the program's (mux.app.program).
inline part extend(contract::program_parts, type_tag<feature>, app::services& shared) { return part{&shared}; }

}  // namespace mux::plugins::threads
