// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.replies.app -- The replies' part of the program: a message
// answered -- from its menu, a swipe, a reaction's row -- the field said to
// answer it, with who wrote it and a line of it; in the thread open, where
// the message is its root or in it; Quote & Reply, the text selected put in
// the field quoted.
export module mux.plugin.replies.app;

import std;
import splice;
import skiff.scene;
import mux.composition;
import mux.core;
import mux.ui;
import mux.logic.messages;
import mux.app.services;
import mux.app.jumps;
import mux.plugin.replies.types;

export namespace mux::plugins::replies {

struct part {
  app::services* s_ = nullptr;

  // From the menu: the message it is up for. (Taken by value: answering
  // closes the menu, and its facts with it.)
  void apply(const reply&) {
    if (const auto& open = *s_->showing->look<std::optional<ui::menu_facts>>())
      this->answer(open->id, open->text);
  }
  // A swipe on a message, or a reaction's row: the message it names.
  void apply(const ui::request::reply_to& one) { this->answer(one.id, one.text); }
  // Ctrl+Up where nothing is answered: the last message that can be; answering
  // one, the one above it; Ctrl+Down the one below, and past the newest the
  // answer let go. What can be answered is what the view shows -- not what
  // its filter leaves out. The one answered scrolled to and flashed.
  template <class Asked>
  void apply(const ui::request::answer_step& one, const Asked& asked) {
    const conversation* chat = s_->chosen() ? s_->model->find(*s_->chosen()) : nullptr;
    if (!chat)
      return;
    const auto answerable = chat->timeline | std::views::filter([&](const message& each) {
                              return !each.id.empty() && ui::answerable(each) && app::chat_shows(*s_, *chat, each);
                            }) | std::views::transform([](const message& each) { return &each; }) | std::ranges::to<std::vector>();
    if (answerable.empty())
      return;
    const std::optional<std::string> now = spl::visit(
        spl::overloaded{[](const ui::compose::reply& r) { return std::optional<std::string>(r.id); },
                        [](const auto&) { return std::optional<std::string>(); }},
        s_->showing->look<ui::chat_compose>()->now);
    const auto at = now ? std::ranges::find(answerable, *now, &message::id) : answerable.end();
    const message* next = nullptr;
    if (at == answerable.end())
      next = one.older ? answerable.back() : nullptr;
    else if (one.older)
      next = at == answerable.begin() ? *at : *(at - 1);
    else if (at + 1 != answerable.end())
      next = *(at + 1);
    if (!next) {
      asked(ui::request::cancel_compose{});
      return;
    }
    this->answer(next->id, next->body.plain);
    s_->show_message_due = app::services::message_asked{next->id};
  }
  // The text selected in the message, quoted in what answers it.
  void apply(const quote_reply&) {
    const auto& open = *s_->showing->look<std::optional<ui::menu_facts>>();
    if (!open)
      return;
    const auto selected = open->selection ? open->copied_fragment : std::nullopt;
    const std::string plain = open->selection ? open->copied : std::string();
    const std::string id = open->id;
    this->answer(id, open->text);
    if (plain.empty())
      return;
    const auto fragment = selected.value_or(skiff::scene::clipboardFragment(plain, {}));
    const auto quote = [&](ui::composer_doc& now) { ui::prepend_quote(now, fragment); };
    // Into what answers it: the thread's draft, where it is answered there.
    if (s_->showing->look<ui::chat_shown>()->thread_answer == id) {
      s_->edit_thread_writing(quote);
      return;
    }
    s_->edit_chat_writing(quote);
  }

 private:
  // The field said to answer a message: who wrote it, a line of it -- its
  // mentions by name, as the quote in the bubble shows them. Nothing where
  // it cannot be answered (deleted, or not on the server).
  void answer(std::string id, std::string text) {
    ui::show<ui::menu_facts>(*s_->showing, std::nullopt);
    std::string title = "Reply";
    const message* said = nullptr;
    std::string line;
    if (const auto& chosen = s_->chosen())
      if (const conversation* chat = s_->model->find(*chosen))
        if (const message* it = ui::held_message(*chat, id)) {
          if (!ui::answerable(*it))
            return;
          said = it;
          title = "Reply to " + ui::sender_name(*chat, it->sender);
          if (!it->body.plain.empty())
            line = ui::quote_line_of(*it, *chat, s_->model);
        }
    if (line.empty())
      line = logic::reply_line(said, text);
    // A thread's root or answer, its thread open: answered there.
    // The field that answers it given the keys -- on a phone, its keyboard
    // up at once, as Telegram's: what answers is typed next.
    if (said && s_->thread_open() == (said->thread ? *said->thread : said->id)) {
      s_->answer_in_thread(id, ui::compose_context{ui::icon::reply{}, std::move(title), std::move(line)});
      s_->ask_keys(ui::keys::thread{});
      return;
    }
    ui::show(*s_->showing, ui::chat_compose{ui::compose::reply{id}, ui::compose_context{ui::icon::reply{}, std::move(title), std::move(line)}});
    s_->ask_keys(ui::keys::chat{});
  }
};

// Its part, among the program's (mux.app.program).
inline part extend(contract::program_parts, type_tag<feature>, app::services& shared) { return part{&shared}; }

}  // namespace mux::plugins::replies
