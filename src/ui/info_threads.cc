// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:info_threads -- A room's threads, in the info's place.
export module mux.ui:info_threads;

import std;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.bind;
import skiff.compose;
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
import mux.ui.base;
import mux.ui.messages;
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
struct threads_panel : skiff::compose::Stacked {
  // Child references and handlers require a fixed address.
  threads_panel(const threads_panel&) = delete;
  threads_panel& operator=(const threads_panel&) = delete;
  threads_panel(threads_panel&&) = delete;
  threads_panel& operator=(threads_panel&&) = delete;

  std::optional<std::string> open;  // the thread open, else the list
  // The thread shown, and its chat's revision then: shown again only where
  // either moved.
  std::optional<std::string> seen_open;
  skiff::model::Revision seen_revision = 0;
  bool seen_list = false;
  std::optional<std::string> answering;  // an answer in it, answered
  // The chat it is of, as last shown: names and powers for its menus.
  const model* seen_model = nullptr;
  std::optional<conversation_id> seen_chat;
  struct close_it {
    using Answer = ::mux::ui::request::toggle_threads;
    ::mux::ui::request::toggle_threads operator()() { return ::mux::ui::request::toggle_threads{}; }
  };
  struct back_it {
    using Answer = ::mux::ui::request::close_thread;
    ::mux::ui::request::close_thread operator()() { return ::mux::ui::request::close_thread{}; }
  };
  struct sent {
    threads_panel* panel;
    using Answer = std::optional<::mux::ui::request::send_in_thread>;
    Answer operator()(std::string_view) const { return panel->send(); }
  };
  struct send_press {
    threads_panel* panel;
    using Answer = std::optional<::mux::ui::request::send_in_thread>;
    Answer operator()() const { return panel->send(); }
  };
  struct stop_answer {
    threads_panel* panel;
    void operator()() const { panel->stop_answering(); }
  };
  // A thread in the list: its root's author and words, how many answers and
  // the latest's time; pressed, opened.
  // The colours it is made in, for the rows it makes later.
  const palette* colours_ = nullptr;
  struct thread_row : skiff::compose::Stacked {
    // What its handlers ask for, returned.
    using Answer = ::mux::ui::request::open_thread;
    std::string root;
    struct lines_t : skiff::compose::Stacked {
      struct parts_t {
        nodes::Text name;
        nodes::Text said;
        nodes::Text meta;
      } parts;
      lines_t(const palette &colours, std::string who, std::string words,
              std::string meta)
          : Stacked(skiff::compose::vbox(2.0f,
                                         {.autoSize = scene::axes::kY,
                                          .grow = scene::axes::kX,
                                          .alignSelf = scene::align::kMiddle})),
            parts{.name =
                      nodes::Text(std::move(who), 13.0f, colours.accent, true),
                  .said = nodes::Text(std::move(words), 13.0f, colours.text),
                  .meta = nodes::Text(std::move(meta), 12.0f, colours.dim)} {
        for (nodes::Text* each : {&parts.name, &parts.said, &parts.meta}) {
          each->setElided(true);
          each->apply({.fillX = true});
        }
      }
    };
    struct parts_t {
      legacy::avatar_mark face;
      lines_t lines;
    } parts;
    thread_row(const palette &colours, const conversation &chat,
               const message &said)
        : Stacked(skiff::compose::hbox(10.0f,
                                       {.fillX = true,
                                        .autoSize = scene::axes::kY,
                                        .padding = {8.0f, 12.0f, 8.0f, 12.0f},
                                        .cornerRadius = 8.0f,
                                        .hoverBackground = colours.chosen})),
          root(said.id),
          parts{.face = skiff::compose::styled(
                    {.alignSelf = scene::align::kStart},
                    legacy::avatar_mark(avatar_of::person{}, said.sender, sender_name(chat, said.sender),
                                36.0f)),
                .lines = lines_t(colours, sender_name(chat, said.sender),
                                 flat(said.body.plain), meta_of(chat, said))} {}
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
    std::optional<Answer> onClick(float, float) {
      return ::mux::ui::request::open_thread{root};
    }
  };
  using head_t = page_header_t<back_it, close_it>;
  // The chat's own composer, writing into the thread: its ✕ lets go of the
  // answer, Enter and the arrow send there, its paperclip and emoji too.
  struct in_thread {
    using cancel = stop_answer;
    using submit = sent;
    using attach = sends<::mux::ui::request::attach_in_thread>;
    using emoji = sends<::mux::ui::request::toggle_thread_emoji>;
    using send = send_press;
    static constexpr std::string_view placeholder = "Reply in thread…";
    using reads_chat = std::false_type;
  };
  using rows_t = nodes::Flow<std::vector<thread_row>>;
  struct parts_t {
    head_t head;
    nodes::Box<> divider;
    nodes::Text empty;
    nodes::ScrollContainer<rows_t> list{rows_t({.spacingY = 2.0f, .wrap = false}, {})};
    // The thread open: its root and answers as the chat's own list has its
    // messages -- the same rows, runs, readers, quotes, presses, menus,
    // swipes -- told by the program (thread_told) which thread, and where
    // to aim, and answering it what it did.
    decltype(timeline_list<thread_told, thread_list_tag>()) answers;
    composer_bar_t<in_thread> line;
  } parts;
  threads_panel(const ui_needs &n)
      : Stacked(skiff::compose::vbox(
            0.0f, {.fillY = true, .background = n.colours->sidebar})),
        colours_(n.colours),
        parts{.head = page_header<back_it, close_it>(*n.colours, "Threads", {}, {}, false, true),
              .divider = skiff::compose::styled({.fillX = true, .height = 1.0f},
                                                nodes::Box<>(n.colours->band)),
              .empty = skiff::compose::styled(
                  {.margin = {16.0f, 16.0f, 0.0f, 16.0f}},
                  nodes::Text("No threads here yet.", 13.0f, n.colours->dim)),
              .answers = timeline_list<thread_told, thread_list_tag>(),
              .line = composer_bar<in_thread>(n, {this}, {this}, {},
                                                       {}, {this})} {

    for (auto* list : std::initializer_list<scene::Node*>{&parts.list, &parts.answers})
      list->apply({.fillX = true, .grow = scene::axes::kY});
    std::get<0>(parts.list.fChildren).apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {4.0f, 4.0f, 4.0f, 4.0f}});
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
  void refresh(const chats_model&) {
    if (this->chat_moved())
      this->show_chosen();
  }
  // Whether what it shows is out of date: its chat changed since, or the
  // thread open is another, or it went from the list to a thread or back.
  [[nodiscard]] bool chat_moved() const {
    if (now_ == nullptr || !chosen_)
      return false;
    return revision_of(*now_, *chosen_) != seen_revision || seen_open != open || seen_list != !open.has_value();
  }
  [[nodiscard]] static skiff::model::Revision revision_of(const model& now, const conversation_id& chat) {
    const auto* in = now.accounts().findItem(chat.account);
    const auto* one = in ? in->fValue.conversations.findItem(chat.id) : nullptr;
    return one ? one->fRevision : 0;
  }
  // What the runtime binding walks call it by (outside a release build):
  // its refresh, made where it is made, not where a table of it is.
  skiff::bind::Refreshes<1> fRefreshes = skiff::bind::refreshes(skiff::bind::refreshFrom<threads_panel, chats_model>());
  void show_chosen() {
    if (!this->visible() || now_ == nullptr || !chosen_)
      return;
    if (const conversation* one = now_->find(*chosen_))
      this->show(*one, now_);
  }
  void show(const conversation& chat, const model* now) {
    seen_model = now;
    seen_chat = chat.id;
    seen_revision = revision_of(*now, chat.id);
    seen_open = open;
    seen_list = !open.has_value();
    std::get<0>(parts.head.fParts).setVisible(open.has_value());
    std::get<1>(parts.head.fParts).setText(open ? "Thread" : "Threads");
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
      return;
    }
    parts.empty.setVisible(false);
  }
  // A message of the thread open answered from its menu, as tdesktop's
  // "Reply to <name>" over the field: in the thread, not the chat.
  void answer(std::string id, compose_context said) {
    answering = std::move(id);
    show_composer_context(parts.line, std::move(said));
    this->invalidateLayout();
  }
  void stop_answering() {
    answering.reset();
    show_composer_context(parts.line, std::nullopt);
    this->invalidateLayout();
  }
  // What is written, sent in the thread open -- an answer to what is
  // answered, where something is.
  std::optional<::mux::ui::request::send_in_thread> send() {
    std::optional<::mux::ui::request::send_in_thread> asked;
    const std::string text = composer_plain(parts.line);
    if (!open || text.empty())
      return asked;
    asked = ::mux::ui::request::send_in_thread{*open, text, answering, composer_styles(parts.line)};
    clear_composer(parts.line);
    this->stop_answering();
    return asked;
  }
};

}  // namespace mux::ui

// Outside a release build each child is walked through its table, and a
// stack lays itself out through its own (Stack::layoutChildren): so a unit
// that lays out or walks one of these would make its table, and the walks
// of everything under it, there -- the chats screen's took this unit past
// ten minutes. Their tables are made once, each in a walks unit of this
// module (src/ui/walks_*.cc); here, and wherever they are walked, a call.
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::threads_panel> = true;  // walks_info.cc
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::threads_panel>() noexcept;
