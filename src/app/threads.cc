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
import skiff.model;

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
    auto& screen = s_->root();
    if (s_->toggle_threads() && s_->chosen() && !s_->demo())
      s_->net->list_threads(*s_->chosen());
    s_->refresh_due = true;
  }
  void apply(const request::open_thread& one) {
    auto& screen = s_->root();
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
  void apply(const request::close_thread&) {
    s_->close_thread();
    s_->refresh_due = true;
  }
  // What is written in the thread's field, sent in the thread open -- an
  // answer to what is answered there, where something is -- and the field
  // emptied.
  void apply(const request::thread_send&) {
    const auto& shown = *s_->showing->look<mux::ui::chat_shown>();
    const std::string text = mux::ui::composer_plain(s_->thread_writing());
    if (!shown.thread || text.empty())
      return;
    request::send_in_thread asked{*shown.thread, text, shown.thread_answer, mux::ui::composer_styles(s_->thread_writing())};
    s_->write_thread({});
    s_->stop_thread_answer();
    this->apply(asked);
  }
  void apply(const request::thread_answer_cancel&) { s_->stop_thread_answer(); }
  // The thread's field edited: what is written, kept.
  void apply(const request::thread_draft_edited& one) {
    s_->written_last = request::writing::thread{};
    s_->write_thread(one.now);
  }

  // The panel as the chat is: the thread open -- an answer being written
  // let go where it changes -- or the room's threads, each root once, the
  // latest active first: those the server listed, those in view with a
  // thread, and those threads are held of. Worked out again only where the
  // chat moved or the thread open changed.
  void show() {
    const auto& shown = *s_->showing->look<mux::ui::chat_shown>();
    if (shown.thread != answered_in_) {
      answered_in_ = shown.thread;
      s_->stop_thread_answer();
    }
    const mux::conversation* chat = shown.chosen && shown.threads_open ? s_->model->find(*shown.chosen) : nullptr;
    const auto key = std::tuple{chat ? std::optional(chat->id) : std::nullopt, chat ? this->revision_of(chat->id) : 0, shown.thread};
    if (key == seen_)
      return;
    seen_ = key;
    mux::ui::threads_shown now{.listing = !shown.thread};
    if (chat && !shown.thread) {
      std::vector<const mux::message*> roots;
      const auto add = [&](const std::string& id) {
        if (const mux::message* one = mux::ui::held_message(*chat, id); one && !std::ranges::contains(roots, one))
          roots.push_back(one);
      };
      std::ranges::for_each(chat->thread_roots, add);
      for (const mux::message& one : chat->timeline)
        if (one.threaded && one.threaded->count > 0)
          add(one.id);
      std::ranges::for_each(chat->threads | std::views::keys, add);
      const auto latest = [](const mux::message* one) { return one->threaded ? one->threaded->last_at : one->at; };
      std::ranges::sort(roots, std::ranges::greater{}, latest);
      now.roots = roots | std::views::transform([&](const mux::message* one) {
                    return mux::ui::thread_root{one->id, one->sender, mux::ui::sender_name(*chat, one->sender),
                                                mux::ui::flat_line(one->body.plain), mux::ui::thread_meta_of(*chat, *one)};
                  }) | std::ranges::to<std::vector>();
    }
    if (*s_->showing->look<mux::ui::threads_shown>() != now)
      mux::ui::show(*s_->showing, std::move(now));
  }

  void apply(const request::send_in_thread& one) {
    const auto chosen = s_->managed();
    const mux::conversation* chat = chosen ? s_->model->find(*chosen) : nullptr;
    if (!chat || s_->demo())
      return;
    std::string latest = one.root;
    if (const auto found = chat->threads.find(one.root); found != chat->threads.end() && !found->second.empty())
      latest = found->second.back().id;
    s_->net->send_in_thread(*chosen, one.text, one.root, latest, one.reply_to, one.styles);
  }

 private:
  [[nodiscard]] skiff::model::Revision revision_of(const mux::conversation_id& chat) const {
    const auto* in = s_->model->accounts().findItem(chat.account);
    const auto* one = in ? in->fValue.conversations.findItem(chat.id) : nullptr;
    return one ? one->fRevision : 0;
  }

  services* s_;
  std::optional<std::string> answered_in_;
  std::tuple<std::optional<mux::conversation_id>, skiff::model::Revision, std::optional<std::string>> seen_;
};

}  // namespace mux::app
