// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:info_threads -- A room's threads, in the info's place.
export module mux.ui:info_threads;

import std;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes.box;
import skiff.nodes.flow;
import skiff.nodes.icon;
import skiff.nodes.image;
import skiff.nodes.scroll;
import skiff.nodes.text;
import skiff.widgets.pill;
import skiff.widgets.button;
import skiff.widgets.sliderbar;
import skiff.widgets.textbox;
import skiff.widgets.textarea;
import mux.core;
import mux.config;
import mux.logic.links;
import mux.protocols;
import :base;
import :icons;
import :avatars;
import :controls;
import :themes;
import :names;
import :forms;
import :composer;
import :message;
import :html;
import :timeline;  // a message's menu, for the reactions list's bubbles
import :info_common;
import :info_cards;
import :info_reactions;
import :info_new_chats;
import :info_looks;

export namespace mux::ui {
// Threads, as Element's panel has them: in place of the chat's info, the
// room's threads -- each root, who wrote it and what, how many answers and
// when the latest came -- and one opened: its root, its answers, and a
// field to answer in it.
template <class Actions>
struct threads_panel : nodes::Stack, outbox {
  std::optional<std::string> open;  // the thread open, else the list
  std::vector<message> shown;       // what the open thread shows now
  std::optional<std::string> answering;  // an answer in it, answered
  // The chat it is of, as last shown: names and powers for its menus.
  const model* seen_model = nullptr;
  std::optional<conversation_id> seen_chat;
  struct close_it : outbox {
    void operator()() { this->emit(::mux::ui::request::toggle_threads{}); }
  };
  struct back_it : outbox {
    void operator()() { this->emit(::mux::ui::request::close_thread{}); }
  };
  struct sent {
    threads_panel* panel;
    void operator()(std::string_view) const { panel->send(); }
  };
  struct send_press {
    threads_panel* panel;
    void operator()() const { panel->send(); }
  };
  struct stop_answer {
    threads_panel* panel;
    void operator()() const { panel->stop_answering(); }
  };
  // A thread in the list: its root's author and words, how many answers and
  // the latest's time; pressed, opened.
  // The colours it is made in, for the rows it makes later.
  const palette* colours_ = nullptr;
  struct thread_row : nodes::Stack, outbox {
    std::string root;
    struct lines_t : nodes::Stack {
      struct parts_t {
        nodes::Text name;
        nodes::Text said;
        nodes::Text meta;
      } parts;
      lines_t(const palette& colours, std::string who, std::string words, std::string meta)
          : parts{.name = nodes::Text(std::move(who), 13.0f, colours.accent, true),
                  .said = nodes::Text(std::move(words), 13.0f, colours.text),
                  .meta = nodes::Text(std::move(meta), 12.0f, colours.dim)} {
        this->setGap(2.0f);
        fState.apply({.autoSize = scene::axes::kY, .grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
        for (nodes::Text* each : {&parts.name, &parts.said, &parts.meta}) {
          each->setElided(true);
          each->apply({.fillX = true});
        }
      }
    };
    struct parts_t {
      avatar_mark face;
      lines_t lines;
    } parts;
    thread_row(const palette& colours, const conversation& chat, const message& said)
        : root(said.id),
          parts{.face = avatar_mark(said.sender, sender_name(chat, said.sender), 36.0f),
                .lines = lines_t(colours, sender_name(chat, said.sender), flat(said.body.plain), meta_of(chat, said))} {
      this->setHorizontal();
      this->setGap(10.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {8.0f, 12.0f, 8.0f, 12.0f}, .cornerRadius = 8.0f,
                    .hoverBackground = colours.chosen});
      parts.face.apply({.alignSelf = scene::align::kStart});
    }
    [[nodiscard]] static std::string flat(std::string text) {
      std::ranges::replace(text, '\n', ' ');
      return text;
    }
    // "3 replies · 12:34", the latest's author and words after it.
    [[nodiscard]] static std::string meta_of(const conversation& chat, const message& said) {
      const thread_summary summary = said.threaded.value_or(thread_summary{});
      std::string out = std::format("{} {}", summary.count, summary.count == 1 ? "reply" : "replies");
      if (!summary.last_id.empty())
        out += std::format(" · {} · {}: {}", clock_of(summary.last_at), sender_name(chat, summary.last_sender),
                           flat(summary.last_text));
      return out;
    }
    [[nodiscard]] bool acceptsInput() const { return true; }
    [[nodiscard]] bool hoverChangesAppearance() const { return true; }
    [[nodiscard]] bool onClick(float, float) {
      this->emit(::mux::ui::request::open_thread{root});
      return true;
    }
  };
  using head_t = page_header<back_it, close_it>;
  // The chat's own composer, writing into the thread: its ✕ lets go of the
  // answer, Enter and the arrow send there, its paperclip and emoji too.
  struct in_thread {
    using cancel = stop_answer;
    using submit = sent;
    using attach = sends<::mux::ui::request::attach_in_thread>;
    using emoji = sends<::mux::ui::request::toggle_thread_emoji>;
    using send = send_press;
    static constexpr std::string_view placeholder = "Reply in thread…";
  };
  using rows_t = nodes::Flow<std::vector<thread_row>>;
  struct parts_t {
    head_t head;
    nodes::Box<> divider;
    nodes::Text empty;
    nodes::ScrollContainer<rows_t> list{rows_t({.spacingY = 2.0f, .wrap = false}, {})};
    // The thread open: the chat's own timeline, its root and answers in it
    // -- one renderer for both: runs, readers, quotes, presses,
    // menus, swipes, pictures, all as the chat has them.
    timeline_area<Actions> answers;
    composer_bar<Actions, in_thread> line;
  } parts;
  threads_panel(const ui_needs<Actions>& n)
      : colours_(n.colours),
        parts{.head = head_t(*n.colours, "Threads", {}, {}, false, true),
              .divider = nodes::Box<>(n.colours->band),
              .empty = nodes::Text("No threads here yet.", 13.0f, n.colours->dim),
              .answers = timeline_area<Actions>(n),
              .line = composer_bar<Actions, in_thread>(n, {this}, {this}, {}, {}, {this})} {
    fState.apply({.fillY = true, .background = n.colours->sidebar});
    parts.divider.apply({.fillX = true, .height = 1.0f});
    parts.empty.apply({.margin = {16.0f, 16.0f, 0.0f, 16.0f}});
    for (auto* list : std::initializer_list<scene::Node*>{&parts.list, &parts.answers})
      list->apply({.fillX = true, .grow = scene::axes::kY});
    std::get<0>(parts.list.fChildren).apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {4.0f, 4.0f, 4.0f, 4.0f}});
    // The chat's buttons over its timeline are the chat's: not here.
    for (scene::Node* button : std::initializer_list<scene::Node*>{&parts.answers.parts.jump, &parts.answers.parts.back,
                                                                   &parts.answers.parts.mentions, &parts.answers.parts.reactions,
                                                                   &parts.answers.parts.loading})
      button->setVisible(false);
    this->setVisible(false);
  }
  // Brought up to date with the chat: its threads listed, or the one open
  // shown -- made again only where what it shows changed.
  // The chat whose threads it lists, and the model it reads them from:
  // told as the chat is chosen, then read again by the chats binding as
  // the chats move -- while it is open.
  const model* now_ = nullptr;
  std::optional<conversation_id> chosen_;
  void choose(const model& now, std::optional<conversation_id> chosen) {
    now_ = &now;
    chosen_ = std::move(chosen);
    this->show_chosen();
  }
  void refresh(const chats_model&) { this->show_chosen(); }
  void show_chosen() {
    if (!this->visible() || now_ == nullptr || !chosen_)
      return;
    if (const conversation* one = now_->find(*chosen_))
      this->show(*one, now_);
  }
  void show(const conversation& chat, const model* now) {
    seen_model = now;
    seen_chat = chat.id;
    parts.head.parts.back.setVisible(open.has_value());
    parts.head.parts.title.setText(open ? "Thread" : "Threads");
    parts.list.setVisible(!open);
    parts.answers.setVisible(open.has_value());
    parts.line.setVisible(open.has_value());
    const auto root_of = [&](const std::string& id) { return held_message(chat, id); };
    if (!open) {
      // The roots: those the server listed, those in view with a thread,
      // and those threads are held of -- each once, the latest active first.
      std::vector<const message*> roots;
      const auto add = [&](const std::string& id) {
        if (const message* one = root_of(id); one && std::ranges::find(roots, one) == roots.end())
          roots.push_back(one);
      };
      for (const std::string& id : chat.thread_roots)
        add(id);
      for (const message& one : chat.timeline)
        if (one.threaded && one.threaded->count > 0)
          add(one.id);
      for (const auto& [id, answers] : chat.threads)
        add(id);
      const auto latest = [](const message* one) { return one->threaded ? one->threaded->last_at : one->at; };
      std::ranges::sort(roots, [&](const message* a, const message* b) { return latest(a) > latest(b); });
      auto& rows = std::get<0>(std::get<0>(parts.list.fChildren).fChildren);
      rows.clear();
      for (const message* one : roots)
        rows.emplace_back(*colours_, chat, *one);
      parts.empty.setVisible(roots.empty());
      parts.list.invalidateLayout();
      shown.clear();
      return;
    }
    parts.empty.setVisible(false);
    std::vector<message> now_shown;
    if (const message* root = root_of(*open))
      now_shown.push_back(*root);
    if (const auto found = chat.threads.find(*open); found != chat.threads.end())
      now_shown.insert(now_shown.end(), found->second.begin(), found->second.end());
    if (now_shown == shown)
      return;
    const bool grew = now_shown.size() > shown.size();
    shown = std::move(now_shown);
    parts.answers.seen_model = now;
    parts.answers.seen_chat = chat.id;
    std::set<std::string> rooms;
    parts.answers.show_messages(chat, shown, 0, shown.size(), *now, shown_how{}, [](std::size_t) { return false; }, rooms);
    parts.answers.invalidateLayout();
    if (grew)
      parts.answers.parts.timeline.scrollToEnd(false);
  }
  [[nodiscard]] const conversation* chat_of() const { return seen_model && seen_chat ? seen_model->find(*seen_chat) : nullptr; }
  // Scrolled to an answer of the thread open, and flashed: a quote of it
  // pressed. False where it is not one of its.
  bool scroll_to(const std::string& id) {
    auto& bubbles = parts.answers.bubbles();
    const auto found = std::ranges::find(bubbles, id, &message_bubble<Actions>::message_id);
    if (found == bubbles.end())
      return false;
    parts.answers.parts.timeline.scrollTo(found->bounds().fTop - 8.0f);
    found->flash.jump(1.0f);
    found->flash.setTarget(0.0f);
    found->markDamaged();
    return true;
  }
  // A message of the thread open answered from its menu, as tdesktop's
  // "Reply to <name>" over the field: in the thread, not the chat.
  void answer(std::string id, compose_context said) {
    answering = std::move(id);
    parts.line.show_context(std::move(said));
    this->invalidateLayout();
  }
  void stop_answering() {
    answering.reset();
    parts.line.show_context(std::nullopt);
    this->invalidateLayout();
  }
  // What is written, sent in the thread open -- an answer to what is
  // answered, where something is.
  void send() {
    const std::string text = parts.line.plain();
    if (!open || text.empty())
      return;
    this->emit(::mux::ui::request::send_in_thread{*open, text, answering});
    parts.line.clear();
    this->stop_answering();
  }
};

}  // namespace mux::ui
