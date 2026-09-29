// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:conversations -- The conversations screen: the list, the chat, its info.
export module mux.ui:conversations;

import std;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes;
import skiff.widgets;
import mux.core;
import mux.config;
export import :timeline;

export namespace mux::ui {

// What the chat list shows: all the chats, those of a Matrix space, or
// those of an XMPP roster group.
namespace folder {
struct all {
  friend bool operator==(all, all) = default;
};
struct space {
  std::string room;
  friend bool operator==(const space&, const space&) = default;
};
struct group {
  std::string name;
  friend bool operator==(const group&, const group&) = default;
};
}  // namespace folder
using folder_t = std::variant<folder::all, folder::space, folder::group>;

// A folder's tab over the chat list, as Telegram's: its name, and under
// the one chosen a line in the accent.
template <class Pick>
struct folder_tab : scene::Node {
  Pick pick;
  folder_t which;
  bool chosen = false;
  nodes::Text label;
  folder_tab(std::string name, folder_t what, bool is_chosen, Pick act)
      : pick(std::move(act)), which(std::move(what)), chosen(is_chosen),
        label(std::move(name), 13.0f, is_chosen ? accent_colour : dim_colour, true) {
    fState.apply({.height = 32.0f,
                  .autoSize = scene::axes::kX,
                  .padding = {0.0f, 10.0f, 0.0f, 10.0f},
                  .cornerRadius = 6.0f,
                  .hoverBackground = chosen_colour,
                  .focusBackground = chosen_colour});
    underline.apply({.place = scene::anchor::kBottomLeft, .fillX = true, .height = 3.0f, .cornerRadius = 1.5f});
    underline.setVisible(is_chosen);
    label.setMaxWidth(160.0f);
    label.setElided(true);
    label.apply({.anchor = scene::anchor::kCentreLeft, .origin = scene::anchor::kCentreLeft});
  }
  // The line under the one chosen, in the accent.
  nodes::Box<> underline{accent_colour};
  void forEachChild(auto&& f) {
    f(label);
    f(underline);
  }
  [[nodiscard]] bool acceptsInput() const { return true; }
  [[nodiscard]] bool hoverChangesAppearance() const { return true; }
  [[nodiscard]] bool focusChangesAppearance() const { return true; }
  [[nodiscard]] bool onClick(float, float) {
    pick(which);
    return true;
  }
};

template <class Actions>
struct conversations_screen : nodes::Stack {
  Actions* actions = nullptr;
  std::optional<conversation_id> chosen;
  // The account whose chats are listed.
  std::optional<account_id> current;
  bool info_open = false;
  // How wide the chat list and the chat's info are: their own, whatever the
  // window's size, until their edges are dragged.
  float side_width = 300.0f;
  float info_width = 340.0f;

  static constexpr float kMinSidebar = 240.0f;

  // The folder whose chats are listed.
  folder_t folder = folder::all{};
  struct pick_folder {
    conversations_screen* screen;
    void operator()(const folder_t& which) const { screen->choose_folder(which); }
  };
  void choose_folder(const folder_t& which) {
    folder = which;
    if (last_model)
      this->show(*last_model);
  }

  // The chat list: the drawer's button and the name, then the chats.
  struct side_column : nodes::Stack {
    float wanted = 300.0f;
    struct head_row : nodes::Stack {
      menu_button<Actions> menu;
      nodes::Text name{"mux", 17.0f, text_colour, true};
      explicit head_row(Actions* a) : menu(a) {
        this->setHorizontal();
        this->setGap(10.0f);
        fState.apply({.fillX = true, .height = 52.0f, .padding = {8.0f, 8.0f, 8.0f, 8.0f}});
        name.apply({.alignSelf = scene::align::kMiddle});
      }
      void forEachChild(auto&& f) {
        f(menu);
        f(name);
      }
    } head;
    // Search: the chats listed are those whose name or address has what is
    // typed here.
    struct search_box : scene::Node {
      widgets::TextArea<> field{"Search"};
      search_box() {
        fState.apply({.fillX = true, .height = 36.0f, .margin = {0.0f, 10.0f, 8.0f, 10.0f}, .cornerRadius = 18.0f, .background = tile_colour, .selectedBackground = chosen_colour});
        field.setSingleLine(true);
        field.setFontSize(14.0f);
        field.apply({.fillX = true, .margin = {2.0f, 14.0f, 0.0f, 14.0f}});
      }
      void forEachChild(auto&& f) { f(field); }
      // Lit while its field has the focus.
      void update(double) {
        if (field.focused() != fState.selected())
          fState.apply({.selected = field.focused()});
      }
    } search;
    // The folders, where the account has spaces or groups: a line of tabs.
    nodes::Flow<std::vector<folder_tab<pick_folder>>> folders{
        {.direction = nodes::direction::horizontal{}, .spacingX = 2.0f, .spacingY = 2.0f}, {}};
    nodes::Text no_chats{"No chats yet.", 13.0f, dim_colour};
    nodes::ScrollContainer<nodes::Flow<std::vector<conversation_row<Actions>>>> list{
        nodes::Flow<std::vector<conversation_row<Actions>>>({.spacingY = 0.0f, .wrap = false}, {})};
    explicit side_column(Actions* a) : head(a) {
      fState.apply({.fillY = true, .background = sidebar_colour});
      no_chats.apply({.margin = {12.0f, 16.0f, 0.0f, 16.0f}});
      folders.apply({.fillX = true, .autoSize = scene::axes::kY, .margin = {0.0f, 8.0f, 6.0f, 8.0f}});
      list.apply({.fillX = true, .grow = scene::axes::kY});
      std::get<0>(list.fChildren).apply({.fillX = true, .autoSize = scene::axes::kY});
    }
    // Its own width, as far as the window has room for it.
    void measure(const skia::SkRect& parent) {
      fState.fWidth = std::clamp(wanted, std::min(kMinSidebar, parent.width()),
                                 std::max(kMinSidebar, parent.width() * 0.6f));
    }
    void forEachChild(auto&& f) {
      f(head);
      f(search);
      f(folders);
      f(no_chats);
      f(list);
    }
  } side;
  drag_edge<resize_sidebar_to<Actions>> edge;
  // The chat: its header, its messages, and where one writes; or, with no
  // account at all, what to do about it.
  struct chat_column : nodes::Stack {
    // The head, as a function of the chat shown.
    nodes::Memo<typename chat_header<Actions>::view, chat_header<Actions>> header;
    search_bar<Actions> search;
    timeline_area<Actions> area;
    composer_bar<Actions> line;
    struct empty_state : nodes::Stack {
      nodes::Text title{"No accounts yet", 22.0f, text_colour, true};
      nodes::Text note{"Add an XMPP or a Matrix account, and its chats will be here.", 14.0f, dim_colour};
      widgets::Button<ask<Actions, &Actions::open_new_account>> add;
      explicit empty_state(Actions* a) : add("Add account", {a}) {
        this->setGap(12.0f);
        fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {120.0f, 48.0f, 0.0f, 48.0f}});
        note.setWrapped(true);
        note.apply({.fillX = true});
        add.setPrimary(true);
        add.apply({.width = 140.0f, .height = 36.0f});
      }
      void forEachChild(auto&& f) {
        f(title);
        f(note);
        f(add);
      }
    } empty;
    // No chat chosen: the wallpaper, and in its middle a small pill saying
    // what to do, as tdesktop's (its service message look).
    struct select_hint : nodes::Stack {
      struct pill : widgets::Pill {
        pill()
            : widgets::Pill("Select a chat to start messaging",
                            {.plate = skia::colorSetARGB(0x66, 0, 0, 0), .size = 13.0f, .height = 26.0f, .padX = 12.0f}) {
          fState.apply({.alignSelf = scene::align::kMiddle});
        }
      } shown;
      select_hint() {
        fStack.justify = nodes::justify::middle{};
        fState.apply({.fillX = true, .grow = scene::axes::kY});
      }
      void forEachChild(auto&& f) { f(shown); }
    } hint;
    explicit chat_column(Actions* a) : search(a), area(a), line(a), empty(a) {
      header.apply({.fillX = true, .height = chat_header<Actions>::kHeight});
      header.show({}, [a](const auto& shown) { return chat_header<Actions>(a, shown); });
      fState.apply({.fillY = true, .grow = scene::axes::kX, .background = chat_colour});
      area.apply({.fillX = true, .grow = scene::axes::kY});
    }
    void forEachChild(auto&& f) {
      f(header);
      f(search);
      f(area);
      f(line);
      f(empty);
      f(hint);
    }
    // The theme's wallpaper, as its colour, behind the messages.
  } chat;
  drag_edge<resize_info_to<Actions>> info_edge;
  info_panel<Actions> info;

  // The old names, for what is kept in the parts.
  nodes::ScrollContainer<nodes::Flow<std::vector<conversation_row<Actions>>>>& list = side.list;
  nodes::Text& no_chats = side.no_chats;
  nodes::Memo<typename chat_header<Actions>::view, chat_header<Actions>>& header = chat.header;
  search_bar<Actions>& search = chat.search;
  // The keys of a chat, as tdesktop's -- what the input leaves to it:
  // Ctrl+F finds in it; Up in an empty input edits the last message sent;
  // Ctrl+Up answers the last message, and each Ctrl+Up after it the one
  // above, Ctrl+Down back down; Ctrl+C copies what is selected in the
  // messages; Esc lets an answer or an edit go.
  using Node::onKey;
  void onKey(scene::phase::bubble, const scene::key::down& press, scene::Reply& reply) {
    namespace keys = scene::keys;
    const bool control = press.modifiers.template has<scene::modifier::control>();
    const bool any = control || press.modifiers.template has<scene::modifier::shift>() ||
                     press.modifiers.template has<scene::modifier::alt>();
    if (!chosen)
      return;
    if (press.key == keys::kF && control) {
      actions->open_search();
    } else if (press.key == keys::kUp && control) {
      actions->reply_step(true);
    } else if (press.key == keys::kDown && control) {
      actions->reply_step(false);
    } else if (press.key == keys::kUp && !any && line.text().empty()) {
      actions->edit_last();
    } else if (press.key == keys::kC && control) {
      auto& bubbles = std::get<0>(std::get<0>(timeline.fChildren).fChildren);
      const auto selected = std::ranges::find_if(bubbles, [](message_bubble& one) { return one.body.text.hasSelection(); });
      if (selected == bubbles.end())
        return;
      skiff::scene::setClipboardText(selected->body.text.selected());
    } else if (press.key == keys::kEscape && !any && line.answering()) {
      actions->cancel_compose();
    } else {
      return;
    }
    reply.handle();
  }
  // The search bar in place of the head, or the head back.
  void show_search(bool shown) {
    search.setVisible(shown);
    header.setVisible(!shown);
    if (!shown) {
      search.field.setText({});
      search.show_found(std::nullopt, 0, false);
    }
    this->invalidateLayout();
  }
  nodes::ScrollContainer<nodes::Flow<std::vector<message_bubble>>>& timeline = chat.area.timeline;
  // The chat whose messages are shown, how many, and how many came while
  // the view was above the newest.
  std::optional<conversation_id> shown_chat;
  std::string shown_last;
  // Where each chat was scrolled to when it was left: it comes back there.
  std::map<conversation_id, float> scrolled;
  int unseen = 0;
  composer_bar<Actions>& line = chat.line;

  explicit conversations_screen(Actions* a)
      : actions(a), side(a), edge({a}), chat(a), info_edge({a}, false), info(a) {
    fState.apply({.fill = true});
    this->setHorizontal();
    // The edges take a pixel between the columns, their line, and are
    // wider than that over them to be caught.
    edge.apply({.fillY = true, .width = 7.0f, .margin = {0.0f, -3.0f, 0.0f, -3.0f}});
    info_edge.apply({.fillY = true, .width = 7.0f, .margin = {0.0f, -3.0f, 0.0f, -3.0f}});
    info.apply({.fillY = true, .width = info_width});
    this->show_info();
  }

  void forEachChild(auto&& f) {
    f(side);
    f(edge);
    f(chat);
    f(info_edge);
    f(info);
  }

  // The chat list as wide as `x`, where its edge was dragged to.
  void resize_sidebar(float x) {
    side_width = x - fState.contentBox().fLeft;
    side.wanted = side_width;
    side.invalidateLayout();
  }

  // The chat's info as wide as from `x` to the window's right.
  void resize_info(float x) {
    info_width = std::clamp(fState.contentBox().fRight - x, 260.0f,
                            std::max(260.0f, fState.contentBox().width() - side_width - 300.0f));
    info.apply({.width = info_width});
  }

  // The chat's info beside it where it is open and a chat is chosen.
  void show_info() {
    const bool shown = info_open && chosen.has_value();
    info.setVisible(shown);
    info_edge.setVisible(shown);
    side.wanted = side_width;
    info.apply({.width = info_width});
  }

  void toggle_info() {
    info_open = !info_open;
    this->show_info();
  }



  // The model as it is now: the current account's chats, newest first, and
  // the chosen one.
  // The chats muted, as the program keeps them.
  std::set<conversation_id> muted;
  // What is left written in each chat, as the program keeps it.
  std::map<conversation_id, std::string> drafts;
  [[nodiscard]] std::string draft_of(const conversation_id& id) const {
    const auto found = drafts.find(id);
    return found == drafts.end() ? std::string() : found->second;
  }
  // Where the chosen chat pages back from, and where it was last asked to:
  // scrolled to its top, the older messages are asked for, once for each.
  std::optional<std::string> history_from;
  std::optional<std::string> history_asked;

  [[nodiscard]] bool settling() const { return false; }
  // Back to the newest, and nothing unseen.
  void jump_to_end() {
    // To the newest: the stretch made at the end again.
    if (!made.to_end && last_model) {
      made = made_range{};
      this->show_conversation(*last_model);
    }
    timeline.scrollToEnd();
    unseen = 0;
    chat.area.jump.set_unseen(0);
  }

  // What was searched for last, and the model last shown: typing into the
  // search filters the list again.
  std::string searched;
  const model* last_model = nullptr;

  bool was_typing = false;
  std::string typed_last;
  // Which of the chat's messages are made into bubbles: a stretch of them,
  // at most a few hundred, that slides with the reader -- more made above
  // and the far ones below let go as they scroll up, the other way down --
  // and at the end, follows what comes. Known by the ids at its ends, so
  // history coming in above does not move it. What is made is what the
  // frames walk: a chat of any length costs a few hundred bubbles.
  struct made_range {
    std::optional<std::string> from, to;
    bool to_end = true;
  };
  made_range made;
  std::map<conversation_id, made_range> made_of;
  static constexpr std::size_t kFirstMade = 80, kMostMade = 240, kMadeStep = 60;
  [[nodiscard]] std::pair<std::size_t, std::size_t> made_indices(const std::vector<message>& all) const {
    const auto index_of = [&](const std::optional<std::string>& id) -> std::optional<std::size_t> {
      if (!id)
        return std::nullopt;
      const auto it = std::ranges::find(all, *id, &message::id);
      return it == all.end() ? std::nullopt : std::optional<std::size_t>(static_cast<std::size_t>(it - all.begin()));
    };
    std::size_t to = all.size();
    if (!made.to_end)
      if (const auto at = index_of(made.to))
        to = *at + 1;
    std::size_t from = to > kFirstMade ? to - kFirstMade : 0;
    if (const auto at = index_of(made.from); at && *at <= to)
      from = *at;
    if (to - from > kMostMade)
      from = to - kMostMade;
    return {from, to};
  }
  void set_made(const std::vector<message>& all, std::size_t from, std::size_t to) {
    to = std::min(to, all.size());
    from = std::min(from, to);
    made.from = from < all.size() ? std::optional<std::string>(all[from].id) : std::nullopt;
    made.to = to > 0 ? std::optional<std::string>(all[to - 1].id) : std::nullopt;
    made.to_end = to == all.size();
  }
  // A message to bring into view, once it is made and laid out.
  std::optional<std::string> jumping_to;
  int jump_tries = 0;
  // The window asked around a message, and the page forward asked: not
  // asked twice.
  std::optional<std::string> context_asked, newer_asked;

  void jump_to(std::string id) {
    if (!chosen || !last_model)
      return;
    const conversation* one = last_model->find(*chosen);
    if (!one)
      return;
    // Made, loaded or paged back to at the next frames, as update() finds it.
    jumping_to = std::move(id);
    jump_tries = 0;
    context_asked.reset();
  }

  void update(double) {
    // A message jumped to: made into a bubble where it is loaded, paged back
    // to where it is not -- page after page, as long as there is history --
    // and once it is laid out, brought into view and flashed.
    if (jumping_to && chosen && last_model) {
      auto& entries = std::get<0>(std::get<0>(timeline.fChildren).fChildren);
      const auto it = std::ranges::find(entries, *jumping_to, &message_bubble::message_id);
      const conversation* one = last_model->find(*chosen);
      if (it != entries.end() && !it->bounds().isEmpty()) {
        const float to = timeline.current() + (it->bounds().fTop - timeline.bounds().fTop) - 60.0f;
        timeline.scrollTo(std::max(0.0f, to));
        it->body.flash.jump(1.0f);
        it->body.flash.setTarget(0.0f);
        it->body.markDamaged();
        jumping_to.reset();
      } else if (one == nullptr) {
        jumping_to.reset();
      } else if (const auto found = std::ranges::find(one->timeline, *jumping_to, &message::id);
                 found != one->timeline.end()) {
        // Made around it, where it is not made already.
        const auto at = static_cast<std::size_t>(found - one->timeline.begin());
        const auto [from, to] = this->made_indices(one->timeline);
        if (at < from || at >= to) {
          this->set_made(one->timeline, at > 40 ? at - 40 : 0, at + 40);
          this->show_conversation(*last_model);
        }
      } else if (is_matrix(chosen->account.speaks)) {
        // Not here: a window of the history around it, from the server --
        // not all of it from here to there.
        if (context_asked != jumping_to) {
          context_asked = jumping_to;
          actions->load_context(*chosen, *jumping_to);
        } else if (++jump_tries > 600) {
          jumping_to.reset();  // it did not come
        }
      } else if (history_from && history_asked != history_from) {
        history_asked = history_from;
        actions->load_older(*chosen, *history_from);
      } else if (!history_from && ++jump_tries > 120) {
        jumping_to.reset();  // the beginning, and it was not there
      }
    }
    // What is in the composer: typing while there is text in it.
    if (const bool has_text = !line.text().empty(); has_text != was_typing || (has_text && line.text() != typed_last)) {
      was_typing = has_text;
      typed_last = line.text();
      actions->typing(has_text);
    }
    if (side.search.field.text() != searched && last_model) {
      searched = side.search.field.text();
      this->show(*last_model);
    }
    // Away from the newest: scrolled up, or in a window of the history.
    const conversation* shown_one = chosen && last_model ? last_model->find(*chosen) : nullptr;
    const bool away = !timeline.atEnd(40.0f) || (shown_one && shown_one->detached);
    if (away != chat.area.jump.visible())
      chat.area.jump.setVisible(away);
    if (!away && unseen != 0) {
      unseen = 0;
      chat.area.jump.set_unseen(0);
    }
    // Near the top: the stretch made slides up -- the far ones below let
    // go -- and past all that is loaded, the history is paged back. Near
    // the bottom, where it is not at the end, it slides down.
    if (chosen && last_model && !jumping_to)
      if (const conversation* one = last_model->find(*chosen)) {
        auto [from, to] = this->made_indices(one->timeline);
        if (timeline.current() <= 300.0f && from > 0) {
          from = from > kMadeStep ? from - kMadeStep : 0;
          to = std::min(to, from + kMostMade);
          this->set_made(one->timeline, from, to);
          this->show_conversation(*last_model);
        } else if (timeline.current() <= 4.0f && from == 0 && history_from && history_asked != history_from) {
          history_asked = history_from;
          actions->load_older(*chosen, *history_from);
        } else if (made.to_end && one->detached && one->future_from && newer_asked != one->future_from &&
                   timeline.current() >= timeline.extent() - 300.0f) {
          // At the end of a window: paged forward, toward the newest.
          newer_asked = one->future_from;
          actions->load_newer(*chosen, *one->future_from);
        } else if (!made.to_end && timeline.current() >= timeline.extent() - 300.0f) {
          to = std::min(one->timeline.size(), to + kMadeStep);
          from = to > kMostMade && to - from > kMostMade ? to - kMostMade : from;
          this->set_made(one->timeline, from, to);
          this->show_conversation(*last_model);
        }
      }
  }

  void show(const model& now) {
    last_model = &now;
    if (!current || !now.accounts().contains(*current))
      current = now.accounts().empty() ? std::nullopt : std::optional<account_id>(now.accounts().begin()->first);
    auto& rows = std::get<0>(std::get<0>(list.fChildren).fChildren);
    std::vector<const conversation*> chats;
    // What is searched for, in any case: in a name or an address.
    const auto lower = [](std::string text) {
      for (char& c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      return text;
    };
    const std::string wanted = lower(side.search.field.text());
    // The folders the account has: its spaces, then its groups.
    std::vector<std::pair<std::string, folder_t>> folders{{"All", folder::all{}}};
    if (current) {
      std::set<std::string> groups;
      for (const auto& [key, one] : now.accounts().at(*current).conversations) {
        if (one.space)
          folders.emplace_back(display_name(one), folder::space{one.id.id});
        groups.insert(one.groups.begin(), one.groups.end());
      }
      for (const std::string& name : groups)
        folders.emplace_back(name, folder::group{name});
    }
    if (std::ranges::find(folders, folder, &std::pair<std::string, folder_t>::second) == folders.end())
      folder = folder::all{};
    auto& tabs = std::get<0>(side.folders.fChildren);
    tabs.clear();
    for (auto& [name, which] : folders)
      tabs.emplace_back(name, which, which == folder, pick_folder{this});
    side.folders.setVisible(folders.size() > 1);
    // Whether a chat is in the folder chosen. A space is a folder, not a
    // chat: it is never listed.
    const account* in = current ? &now.accounts().at(*current) : nullptr;
    const auto in_folder = [&](const conversation& one) {
      if (one.space)
        return false;
      return std::visit(overloaded{[](const folder::all&) { return true; },
                                   [&](const folder::space& s) {
                                     const auto found = in->conversations.find(s.room);
                                     return found != in->conversations.end() &&
                                            std::ranges::contains(found->second.children, one.id.id);
                                   },
                                   [&](const folder::group& g) { return std::ranges::contains(one.groups, g.name); }},
                        folder);
    };
    if (in)
      for (const auto& [key, one] : in->conversations)
        if (in_folder(one) &&
            (wanted.empty() || lower(display_name(one)).contains(wanted) || lower(one.id.id).contains(wanted)))
          chats.push_back(&one);
    std::ranges::sort(chats, std::ranges::greater{}, [](const conversation* one) {
      const message* last = newest(*one);
      return last ? last->at : std::chrono::sys_time<std::chrono::milliseconds>{};
    });
    // The rows, as a function of the chats: those whose chat shows the same
    // are kept as they are.
    const auto is_chosen = [&](const conversation* one) { return chosen && *chosen == one->id; };
    if (nodes::reconcile(
            rows, chats, [](const conversation* one) { return one->id; },
            [](const conversation_row<Actions>& row) { return row.id; },
            [&](const conversation* one) {
              return conversation_row<Actions>(actions, *one, is_chosen(one), muted.contains(one->id), draft_of(one->id));
            },
            [&](const conversation_row<Actions>& row, const conversation* one) {
              return row.shown ==
                     conversation_row<Actions>::view_of(*one, is_chosen(one), muted.contains(one->id), draft_of(one->id));
            }))
      list.invalidateLayout();
    const bool none = now.accounts().empty();
    // The messages' area, not only its list: hidden, it no longer takes the
    // column's height and pushes what is said instead to the bottom.
    // A chat open: its head, messages and composer. None chosen: the hint.
    const bool open = !none && chosen.has_value();
    for (scene::Node* shown : std::initializer_list<scene::Node*>{&header, &chat.area, &line})
      shown->setVisible(open);
    chat.hint.setVisible(!none && !chosen.has_value());
    no_chats.setVisible(!none && chats.empty());
    chat.empty.setVisible(none);
    this->show_info();
    this->invalidateLayout();
    this->show_conversation(now);
  }

  void show_conversation(const model& now) {
    // Whether the reader was at the newest: then the view follows it; and
    // where the view was, for the chat being left.
    const bool was_at_end = timeline.atEnd(40.0f);
    const float left_at = timeline.current();
    auto& entries = std::get<0>(std::get<0>(timeline.fChildren).fChildren);
    const conversation* one = chosen ? now.find(*chosen) : nullptr;
    header.show(chat_header<Actions>::view_of(one, now),
                [this](const auto& shown) { return chat_header<Actions>(actions, shown); });
    history_from = one ? one->history_from : std::nullopt;
    this->show_info();
    if (!one) {
      entries.clear();
      return;
    }
    info.show(*one, now, muted.contains(one->id));
    chat.area.seen_model = &now;
    chat.area.seen_chat = one->id;
    const auto& all = one->timeline;
    // Where each message is in its sender's run: the first has the name,
    // the last the avatar.
    const auto same = [&](std::size_t i, std::size_t j) {
      return j < all.size() && all[j].sender == all[i].sender && all[j].outgoing == all[i].outgoing;
    };
    const auto first_of_run = [&](std::size_t i) { return i == 0 || !same(i, i - 1); };
    const auto last_of_run = [&](std::size_t i) { return !same(i, i + 1); };
    // A chat shown anew: its stretch as it was left, or its newest.
    if (shown_chat != chosen) {
      if (shown_chat)
        made_of[*shown_chat] = made;
      const auto kept = made_of.find(*chosen);
      made = kept == made_of.end() ? made_range{} : kept->second;
    }
    const auto [first_made, last_made] = this->made_indices(all);
    this->set_made(all, first_made, last_made);
    // The bubbles, as a function of the messages: those that show the same
    // are kept -- with a selection in them -- and only the new are made.
    if (nodes::reconcile(
            entries, std::views::iota(first_made, last_made),
            [&](std::size_t i) { return all[i].id; }, [](const message_bubble& row) { return row.message_id; },
            [&](std::size_t i) { return message_bubble(*one, all[i], first_of_run(i), last_of_run(i), &now); },
            [&](const message_bubble& row, std::size_t i) {
              return row.said == all[i] && row.first == first_of_run(i) && row.last == last_of_run(i);
            }))
      timeline.invalidateLayout();
    // The newest is at the bottom: the view follows it where the reader was
    // there or the chat is new to the view; otherwise what came after the
    // last one seen is counted on the way down.
    const std::string last = all.empty() ? std::string() : all.back().id;
    if (shown_chat != chosen) {
      if (shown_chat)
        scrolled[*shown_chat] = was_at_end ? -1.0f : left_at;
      const auto kept = scrolled.find(*chosen);
      if (kept == scrolled.end() || kept->second < 0.0f)
        timeline.scrollToEnd(false);  // a chat opened starts at its newest
      else
        timeline.setCurrent(kept->second);
      unseen = 0;
    } else if (was_at_end) {
      timeline.scrollToEnd();
      unseen = 0;
    } else if (last != shown_last) {
      int after = 0;
      for (auto it = all.rbegin(); it != all.rend() && it->id != shown_last; ++it)
        ++after;
      unseen += after;
    }
    chat.area.jump.set_unseen(unseen);
    shown_chat = chosen;
    shown_last = last;
  }
};

}  // namespace mux::ui
