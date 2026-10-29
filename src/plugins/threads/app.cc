// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.threads.app -- The threads' part of the program: the panel
// opened in place of the chat's info, the room's threads listed by the
// server as it opens; one opened, its answers fetched (and its root, where
// it is not held); an answer sent in the one open -- falling back, for
// clients without threads, to its latest event.
export module mux.plugin.threads.app;

import std;
import skiff.model;
import mux.composition;
import mux.core;
import mux.ui;
import mux.app.network;
import mux.app.services;
import mux.plugin.threads.types;
import mux.plugin.threads.ui_top;

export namespace mux::plugins::threads {

struct part {
  app::services* s_ = nullptr;
  std::optional<std::string> answered_in_;
  std::tuple<std::optional<mux::conversation_id>, skiff::model::Revision, std::optional<std::string>> seen_;

  // Threads, as Element's panel: opened in place of the chat's info, the
  // room's listed by the server as it opens; one opened, its answers fetched
  // (and its root, where it is not held); an answer sent in the one open --
  // falling back, for clients without threads, to its latest event.
  void apply(const ui::request::toggle_threads&) {
    if (s_->toggle_threads() && s_->chosen() && !s_->demo())
      s_->net->list_threads(*s_->chosen());
    s_->refresh_due = true;
  }
  void apply(const ui::request::open_thread& one) {
    if (!s_->chosen())
      return;
    s_->open_thread(one.root);
    if (!s_->demo()) {
      s_->net->load_thread(*s_->chosen(), one.root);
      if (const mux::conversation* chat = s_->model->find(*s_->chosen());
          chat && !chat->quoted.contains(one.root) &&
          std::ranges::find(chat->timeline, one.root, &mux::message::id) == chat->timeline.end())
        s_->net->fetch_quoted(*s_->chosen(), one.root);
    }
    s_->refresh_due = true;
  }
  void apply(const ui::request::close_thread&) {
    s_->close_thread();
    s_->refresh_due = true;
  }
  // What is written in the thread's field, sent in the thread open -- an
  // answer to what is answered there, where something is -- and the field
  // emptied.
  void apply(const ui::request::thread_send&) {
    const auto& shown = *s_->showing->look<mux::ui::chat_shown>();
    const std::string text = mux::ui::composer_plain(s_->thread_writing());
    if (!shown.thread || text.empty())
      return;
    // A message of it edited there: the edit sent, not an answer.
    if (shown.thread_edit) {
      const std::string id = *shown.thread_edit;
      auto styles = mux::ui::composer_styles(s_->thread_writing());
      s_->write_thread({});
      s_->stop_thread_answer();
      if (const auto chosen = s_->managed()) {
        if (s_->demo())
          s_->box->push(mux::change_t{mux::change::message_edited{*chosen, id, mux::body{text, std::nullopt}}});
        else
          s_->net->edit(*chosen, id, text, std::move(styles));
      }
      return;
    }
    ui::request::send_in_thread asked{*shown.thread, text, shown.thread_answer, mux::ui::composer_styles(s_->thread_writing())};
    s_->write_thread({});
    s_->stop_thread_answer();
    this->apply(asked);
  }
  void apply(const ui::request::thread_answer_cancel&) { s_->stop_thread_answer(); }
  // The thread's field edited: what is written, kept.
  void apply(const ui::request::thread_draft_edited& one) {
    s_->written_last = ui::request::writing::thread{};
    s_->write_thread(one.now);
  }

  // The panel as the chat is: the thread open -- an answer being written
  // let go where it changes -- or the room's threads, each root once, the
  // latest active first: those the server listed, those in view with a
  // thread, and those threads are held of. Worked out again only where the
  // chat moved or the thread open changed.
  void tick() {
    const auto& chat_now = *s_->showing->look<mux::ui::chat_shown>();
    if (chat_now.thread != answered_in_) {
      answered_in_ = chat_now.thread;
      s_->stop_thread_answer();
    }
    const mux::conversation* chat = chat_now.chosen && chat_now.threads_open ? s_->model->find(*chat_now.chosen) : nullptr;
    const auto key = std::tuple{chat ? std::optional(chat->id) : std::nullopt, chat ? this->revision_of(chat->id) : 0, chat_now.thread};
    if (key == seen_)
      return;
    seen_ = key;
    threads::shown now{.listing = !chat_now.thread};
    if (chat && !chat_now.thread) {
      std::vector<const mux::message*> roots;
      const auto add = [&](const std::string& id) {
        if (const mux::message* one = mux::ui::held_message(*chat, id); one && !std::ranges::contains(roots, one))
          roots.push_back(one);
      };
      for (const std::string& id : chat->thread_roots)
        add(id);
      for (const mux::message& one : chat->timeline)
        if (one.threaded && one.threaded->count > 0)
          add(one.id);
      for (const std::string& id : chat->threads | std::views::keys)
        add(id);
      const auto latest = [](const mux::message* one) { return one->threaded ? one->threaded->last_at : one->at; };
      std::ranges::sort(roots, std::ranges::greater{}, latest);
      now.roots = roots | std::views::transform([&](const mux::message* one) {
                    return thread_root{one->id, one->sender, mux::ui::sender_name(*chat, one->sender),
                                                flat_line(one->body.plain), thread_meta_of(*chat, *one)};
                  }) | std::ranges::to<std::vector>();
    }
    if (*s_->showing->look<threads::shown>() != now)
      mux::ui::show(*s_->showing, std::move(now));
  }

  void apply(const ui::request::send_in_thread& one) {
    const auto chosen = s_->managed();
    const mux::conversation* chat = chosen ? s_->model->find(*chosen) : nullptr;
    if (!chat || s_->demo())
      return;
    std::string latest = one.root;
    if (const auto found = chat->threads.find(one.root); found != chat->threads.end() && !found->second.empty())
      latest = found->second.back().id;
    s_->net->send_in_thread(*chosen, one.text, one.root, latest, one.reply_to, one.styles);
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
