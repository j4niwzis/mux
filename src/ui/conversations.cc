// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:conversations -- The conversations screen: the list, the chat, its info.
export module mux.ui:conversations;

import std;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes.box;
import skiff.nodes.flow;
import skiff.nodes.scroll;
import skiff.nodes.text;
import skiff.widgets.button;
import skiff.widgets.pill;
import skiff.widgets.textarea;
import mux.core;
import mux.config;
import :base;
import :controls;
import :themes;
import :names;
import :chat_list;
import :message;
import :header;
import :info;
import :composer;
import :timeline;

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
using folder_t = splice::variant<folder::all, folder::space, folder::group>;

// A folder's tab over the chat list, as Telegram's: its name, and under
// the one chosen a line in the accent.
template <class Pick>
struct folder_tab : scene::Node {
  Pick pick;
  folder_t which;
  bool chosen = false;
  struct parts_t {
    nodes::Text label;
    // The line under the one chosen, in the accent.
    nodes::Box<> underline{accent_colour};
  } parts;
  folder_tab(std::string name, folder_t what, bool is_chosen, Pick act)
      : pick(std::move(act)), which(std::move(what)), chosen(is_chosen),
        parts{.label = nodes::Text(std::move(name), 13.0f, is_chosen ? accent_colour : dim_colour, true)} {
    auto& [label, underline] = parts;
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
  // Which of the chat's pins the bar shows, by its place among them: the
  // one above the view (the newest until that is worked out).
  std::size_t pinned_step = 0;
  std::optional<conversation_id> pinned_of;
  // The messages a bubble was made for: one made for the first time, while
  // its chat is being read, has just come.
  std::set<std::string> appeared;
  // Which room events a chat shows, kind by kind, as the program's settings
  // say: set by the program before it shows the model; a chat not in it
  // shows them all.
  std::map<conversation_id, room_event_filter> event_filters;
  // Whether a chat's view has a message: said, or a room event its filter
  // shows -- what the reader can see, and so answer.
  [[nodiscard]] bool shown_in(const conversation_id& chat, const message& said) const {
    if (!said.service)
      return true;
    const auto found = event_filters.find(chat);
    return found == event_filters.end() || found->second.shows(said.event_kind);
  }
  // The chats that show who has read up to where, as faces.
  std::set<conversation_id> receipts_in;
  // The chats that show no link previews.
  std::set<conversation_id> previews_off;
  // Rooms the bubbles made name and wait on; and those not joined, for the
  // program to ask their server of (it drains them).
  std::set<std::string> rooms_waiting;  // their pictures
  std::set<std::string> rooms_unfound;  // whether they are there
  std::set<std::string> rooms_wanted;
  // How far a jump's search pages back in each chat, in events; 0 no limit.
  std::map<conversation_id, std::int64_t> jump_limits;
  // How many messages the chat had when the search began paging back.
  std::optional<std::size_t> jump_base;
  // The server's window around it did not bring it: paging back instead.
  bool jump_paging = false;
  // The @ list: the chat's members matching what follows an @ at the end of
  // what is written, as Telegram's; who was picked from it, to be sent as
  // mentions with the message.
  std::vector<member> mention_matches;
  std::size_t mention_lit = 0;
  std::string mention_query;
  struct pick_mention {
    conversations_screen* screen;
    std::size_t index;
    void operator()() const { screen->choose_mention(index); }
  };
  // One of the @ list: the avatar, the name over the ID.
  struct mention_row : nodes::Stack {
    pick_mention act;
    struct parts_t {
      avatar_mark face;
      two_lines texts;
    } parts;
    mention_row(pick_mention what, const member& one)
        : act(what), parts{.face = avatar_mark(one.id, one.name.empty() ? one.id : one.name, 28.0f),
                           .texts = two_lines(one.name.empty() ? one.id : one.name, one.id, 14.0f, 1.0f)} {
      this->setHorizontal();
      this->setGap(10.0f);
      fState.apply({.fillX = true, .height = 44.0f, .padding = {0.0f, 14.0f, 0.0f, 14.0f},
                    .hoverBackground = chosen_colour, .selectedBackground = chosen_colour});
    }
    void set_lit(bool on) { fState.apply({.selected = on}); }
    [[nodiscard]] bool acceptsInput() const { return true; }
    [[nodiscard]] bool hoverChangesAppearance() const { return true; }
    [[nodiscard]] bool onClick(float, float) {
      act();
      return true;
    }
  };
  struct mention_list : nodes::Stack {
    struct parts_t {
      std::vector<mention_row> rows;
    } parts;
    mention_list() { fState.apply({.fillX = true, .autoSize = scene::axes::kY, .background = sidebar_colour}); }
  };
  // The account to list once the model has it: the one shown last, kept.
  // Taken the first time it is there; dropped when an account is chosen.
  std::optional<account_id> wanted;
  bool info_open = false;
  // How wide the chat list and the chat's info are: their own, whatever the
  // window's size, until their edges are dragged.
  float side_width = 300.0f;
  float info_width = 340.0f;

  static constexpr float kMinSidebar = 240.0f;

  // The folder whose chats are listed.
  folder_t folder = folder::all{};
  // The folders the tabs were made for, and the one chosen then.
  std::vector<std::pair<std::string, folder_t>> shown_folders;
  folder_t shown_folder = folder::all{};
  struct pick_folder {
    conversations_screen* screen;
    void operator()(const folder_t& which) const { screen->choose_folder(which); }
  };
  // The @ list, as what is written now asks: what follows the last @ at
  // its end -- one at its start or after a space, with no space after it --
  // matched against the chat's members, by name or ID, as Telegram does.
  void find_mentions() {
    const std::string& text = chat.line.text();
    std::optional<std::string> query;
    if (const auto at = text.rfind('@'); at != std::string::npos && (at == 0 || text[at - 1] == ' ' || text[at - 1] == '\n')) {
      const std::string_view after = std::string_view(text).substr(at + 1);
      if (after.find_first_of(" \n") == std::string_view::npos)
        query = std::string(after);
    }
    const conversation* in = chosen && last_model ? last_model->find(*chosen) : nullptr;
    if (!query || in == nullptr || !is_group(*in)) {
      if (chat.parts.mentions.visible()) {
        chat.parts.mentions.setVisible(false);
        mention_matches.clear();
        this->invalidateLayout();
      }
      mention_query.clear();
      return;
    }
    if (*query == mention_query && chat.parts.mentions.visible())
      return;
    mention_query = *query;
    const auto lower = [](std::string_view in) {
      std::string out(in);
      for (char& c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      return out;
    };
    const std::string wanted = lower(*query);
    mention_matches.clear();
    for (const member& one : in->members) {
      if (chosen && one.id == chosen->account.address)
        continue;
      if (lower(one.name).find(wanted) != std::string::npos || lower(one.id).find(wanted) != std::string::npos)
        mention_matches.push_back(one);
      if (mention_matches.size() == 6)
        break;
    }
    auto& rows = chat.parts.mentions.parts.rows;
    rows.clear();
    for (std::size_t i = 0; i < mention_matches.size(); ++i)
      rows.emplace_back(pick_mention{this, i}, mention_matches[i]);
    mention_lit = 0;
    if (!rows.empty())
      rows.front().set_lit(true);
    chat.parts.mentions.setVisible(!rows.empty());
    this->invalidateLayout();
  }
  // One picked: the @ and what follows it made their name, and they kept
  // to be mentioned when it is sent.
  void choose_mention(std::size_t index) {
    if (index >= mention_matches.size())
      return;
    const member one = mention_matches[index];
    const std::string& text = chat.line.text();
    const auto at = text.rfind('@');
    if (at == std::string::npos)
      return;
    chat.line.put_mention(at, one.name.empty() ? one.id : one.name, one.id);
    mention_query.clear();
    mention_matches.clear();
    chat.parts.mentions.parts.rows.clear();
    chat.parts.mentions.setVisible(false);
    this->invalidateLayout();
  }
  // The keys, while the list is up: Up and Down through it, Enter picks,
  // Esc closes it -- before the input reads Enter as sending.
  void onKey(scene::phase::capture, const scene::key::down& press, scene::Reply& reply) {
    if (!chat.parts.mentions.visible() || mention_matches.empty())
      return;
    namespace keys = scene::keys;
    auto& rows = chat.parts.mentions.parts.rows;
    if (press.key == keys::kUp || press.key == keys::kDown) {
      rows[mention_lit].set_lit(false);
      const std::size_t n = rows.size();
      mention_lit = press.key == keys::kUp ? (mention_lit + n - 1) % n : (mention_lit + 1) % n;
      rows[mention_lit].set_lit(true);
      reply.handle();
    } else if (press.key == keys::kEnter) {
      this->choose_mention(mention_lit);
      reply.handle();
    } else if (press.key == keys::kEscape) {
      chat.parts.mentions.setVisible(false);
      this->invalidateLayout();
      reply.handle();
    }
  }
  void choose_folder(const folder_t& which) {
    folder = which;
    if (last_model)
      this->show(*last_model);
  }

  // The chat list: the drawer's button and the name, then the chats.
  struct side_column : nodes::Stack {
    float wanted = 300.0f;
    struct head_row : nodes::Stack {
      struct parts_t {
        menu_button<Actions> menu;
        nodes::Text name{"mux", 17.0f, text_colour, true};
      } parts;
      explicit head_row(Actions* a) : parts{.menu = menu_button<Actions>(a)} {
        this->setHorizontal();
        this->setGap(10.0f);
        fState.apply({.fillX = true, .height = 52.0f, .padding = {8.0f, 8.0f, 8.0f, 8.0f}});
        parts.name.apply({.alignSelf = scene::align::kMiddle});
      }
    };
    // Search: the chats listed are those whose name or address has what is
    // typed here.
    struct search_box : scene::Node {
      struct parts_t {
        widgets::TextArea<> field{"Search"};
      } parts;
      widgets::TextArea<>& field = parts.field;
      search_box() {
        fState.apply({.fillX = true, .height = 36.0f, .margin = {0.0f, 10.0f, 8.0f, 10.0f}, .cornerRadius = 18.0f, .background = tile_colour, .selectedBackground = chosen_colour});
        field.setSingleLine(true);
        field.setFontSize(14.0f);
        field.apply({.fillX = true, .margin = {2.0f, 14.0f, 0.0f, 14.0f}});
      }
      // Lit while its field has the focus: looked at as the focus moves --
      // the field is marked then -- not at every frame.
      [[nodiscard]] bool wantsTick() const { return field.focused() != fState.selected(); }
      void update(double) {
        if (field.focused() != fState.selected())
          fState.apply({.selected = field.focused()});
      }
    };
    using list_t = nodes::ScrollContainer<nodes::Flow<std::vector<conversation_row<Actions>>>>;
    struct parts_t {
      head_row head;
      search_box search;
      // The folders, where the account has spaces or groups: a line of tabs.
      nodes::Flow<std::vector<folder_tab<pick_folder>>> folders{
          {.direction = nodes::direction::horizontal{}, .spacingX = 2.0f, .spacingY = 2.0f}, {}};
      nodes::Text no_chats{"No chats yet.", 13.0f, dim_colour};
      list_t list{nodes::Flow<std::vector<conversation_row<Actions>>>({.spacingY = 0.0f, .wrap = false}, {})};
    } parts;
    // Its parts by their names, for what reads them: it is never moved.
    head_row& head = parts.head;
    search_box& search = parts.search;
    nodes::Flow<std::vector<folder_tab<pick_folder>>>& folders = parts.folders;
    nodes::Text& no_chats = parts.no_chats;
    list_t& list = parts.list;
    explicit side_column(Actions* a) : parts{.head = head_row(a)} {
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
  };
  // The chat: its header, its messages, and where one writes; or, with no
  // account at all, what to do about it.
  // The pinned bar pressed: to the pinned message -- the bar then shows the
  // one pinned above it, as it always shows the one above the view.
  struct pinned_press {
    conversations_screen* screen;
    std::string id;
    void operator()() const {
      screen->actions->jump_to_message(id);
    }
  };
  struct chat_column : nodes::Stack {
    using header_t = nodes::Memo<typename chat_header<Actions>::view, chat_header<Actions>>;
    using pinned_t = nodes::Memo<pinned_view, pinned_bar<pinned_press>>;
    struct empty_state : nodes::Stack {
      using add_button = widgets::Button<ask<Actions, &Actions::open_new_account>>;
      struct parts_t {
        nodes::Text title{"No accounts yet", 22.0f, text_colour, true};
        nodes::Text note{"Add an XMPP or a Matrix account, and its chats will be here.", 14.0f, dim_colour};
        add_button add;
      } parts;
      explicit empty_state(Actions* a) : parts{.add = add_button("Add account", {a})} {
        this->setGap(12.0f);
        fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {120.0f, 48.0f, 0.0f, 48.0f}});
        parts.note.setWrapped(true);
        parts.note.apply({.fillX = true});
        parts.add.setPrimary(true);
        parts.add.apply({.width = 140.0f, .height = 36.0f});
      }
    };
    // No chat chosen: the wallpaper, and in its middle a small pill saying
    // what to do, as tdesktop's (its service message look).
    struct select_hint : nodes::Stack {
      struct pill : widgets::Pill {
        pill()
            : widgets::Pill("Select a chat to start messaging",
                            {.plate = skia::colorSetARGB(0x66, 0, 0, 0), .size = 13.0f, .height = 26.0f, .padX = 12.0f}) {
          fState.apply({.alignSelf = scene::align::kMiddle});
        }
      };
      struct parts_t {
        pill shown;
      } parts;
      select_hint() {
        fStack.justify = nodes::justify::middle{};
        fState.apply({.fillX = true, .grow = scene::axes::kY});
      }
    };
    struct parts_t {
      // The head, as a function of the chat shown.
      header_t header;
      search_bar<Actions> search;
      // The pinned message, under the head, where the chat has any.
      pinned_t pinned;
      timeline_area<Actions> area;
      mention_list mentions;
      composer_bar<Actions> line;
      empty_state empty;
      select_hint hint;
    } parts;
    // Its parts by their names, for what reads them: it is never moved.
    header_t& header = parts.header;
    search_bar<Actions>& search = parts.search;
    timeline_area<Actions>& area = parts.area;
    composer_bar<Actions>& line = parts.line;
    empty_state& empty = parts.empty;
    select_hint& hint = parts.hint;
    explicit chat_column(Actions* a)
        : parts{.search = search_bar<Actions>(a),
                .area = timeline_area<Actions>(a),
                .line = composer_bar<Actions>(a),
                .empty = empty_state(a)} {
      header.apply({.fillX = true, .height = chat_header<Actions>::kHeight});
      parts.pinned.apply({.fillX = true, .height = pinned_bar<pinned_press>::kHeight});
      parts.pinned.setVisible(false);
      header.show({}, [a](const auto& shown) { return chat_header<Actions>(a, shown); });
      fState.apply({.fillY = true, .grow = scene::axes::kX, .background = chat_colour});
      area.apply({.fillX = true, .grow = scene::axes::kY});
      parts.mentions.setVisible(false);
    }
  };
  using side_edge = drag_edge<resize_sidebar_to<Actions>>;
  using info_edge_t = drag_edge<resize_info_to<Actions>>;
  struct parts_t {
    side_column side;
    side_edge edge;
    chat_column chat;
    info_edge_t info_edge;
    info_panel<Actions> info;
  } parts;
  // Its parts by their names, for what reads them: the screen is never moved.
  side_column& side = parts.side;
  side_edge& edge = parts.edge;
  chat_column& chat = parts.chat;
  info_edge_t& info_edge = parts.info_edge;
  info_panel<Actions>& info = parts.info;

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
    // Ctrl+1 to Ctrl+9: the folder in that place, as tdesktop's.
    static constexpr std::array kFolderKeys{keys::k1, keys::k2, keys::k3, keys::k4, keys::k5,
                                            keys::k6, keys::k7, keys::k8, keys::k9};
    if (const auto digit = std::ranges::find(kFolderKeys, press.key); control && digit != kFolderKeys.end()) {
      const auto& tabs = std::get<0>(side.folders.fChildren);
      if (const auto place = static_cast<std::size_t>(digit - kFolderKeys.begin()); place < tabs.size())
        this->choose_folder(tabs[place].which);
      reply.handle();
      return;
    }
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
      const auto selected = std::ranges::find_if(bubbles, [](message_bubble& one) { return one.parts.body.parts.text.hasSelection(); });
      // Not in a message: what any text shows selected -- View source's.
      if (selected == bubbles.end()) {
        if (!scene::selectedText().empty())
          skiff::scene::setClipboardText(scene::selectedText());
        return;
      }
      skiff::scene::setClipboardText(selected->parts.body.parts.text.selected());
    } else if (press.key == keys::kEscape && !any && line.answering()) {
      actions->cancel_compose();
    } else if ((press.key == keys::kTab && control) ||
               ((press.key == keys::kUp || press.key == keys::kDown) && press.modifiers.template has<scene::modifier::alt>())) {
      // To the next chat in the list, or the one before: Ctrl+Tab and
      // Ctrl+Shift+Tab, Alt+Down and Alt+Up.
      const bool back = press.key == keys::kUp || press.modifiers.template has<scene::modifier::shift>();
      const auto& rows = std::get<0>(std::get<0>(list.fChildren).fChildren);
      const auto at = std::ranges::find(rows, *chosen, &conversation_row<Actions>::id);
      if (at == rows.end() || rows.empty())
        return;
      const auto index = static_cast<std::size_t>(at - rows.begin());
      const std::size_t to = back ? (index == 0 ? rows.size() - 1 : index - 1) : (index + 1) % rows.size();
      actions->choose(rows[to].id);
    } else if (press.key == keys::kPageUp || press.key == keys::kPageDown) {
      // A page of the messages, most of what is in view.
      const float page = timeline.bounds().height() * 0.9f;
      timeline.scrollTo(std::max(0.0f, timeline.current() + (press.key == keys::kPageUp ? -page : page)));
    } else if (press.key == keys::kEnd && control) {
      actions->jump_to_end();
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
      search.parts.field.setText({});
      search.show_found(std::nullopt, 0, false);
    }
    this->invalidateLayout();
  }
  nodes::ScrollContainer<nodes::Flow<std::vector<message_bubble>>>& timeline = chat.area.parts.timeline;
  // The chat whose messages are shown, how many, and how many came while
  // the view was above the newest.
  std::optional<conversation_id> shown_chat;
  std::string shown_last;
  // Where each chat was scrolled to when it was left: it comes back there.
  std::map<conversation_id, float> scrolled;
  int unseen = 0;
  composer_bar<Actions>& line = chat.line;

  explicit conversations_screen(Actions* a)
      : actions(a),
        parts{.side = side_column(a),
              .edge = side_edge(resize_sidebar_to<Actions>{a}),
              .chat = chat_column(a),
              .info_edge = info_edge_t(resize_info_to<Actions>{a}, false),
              .info = info_panel<Actions>(a)} {
    fState.apply({.fill = true});
    this->setHorizontal();
    // The edges take a pixel between the columns, their line, and are
    // wider than that over them to be caught.
    edge.apply({.fillY = true, .width = 7.0f, .margin = {0.0f, -3.0f, 0.0f, -3.0f}});
    info_edge.apply({.fillY = true, .width = 7.0f, .margin = {0.0f, -3.0f, 0.0f, -3.0f}});
    info.apply({.fillY = true, .width = info_width});
    this->show_info();
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

  // Frames wanted while a jump goes on: it is carried out a step a frame --
  // made, paged back to, fetched around, aimed at -- and skiff draws the next
  // frame only for what is still settling. Nothing else here asks for one.
  [[nodiscard]] bool settling() const { return jumping_to.has_value() || aiming.has_value(); }
  // Back to the newest, and nothing unseen. A jump still on its way, or a
  // message still aimed at -- one jumped to a moment ago, held in view while
  // what is above it settles -- let go first: it pulled the view back to
  // itself, and the button had to be pressed twice.
  void jump_to_end() {
    this->stop_jump();
    aiming.reset();
    jump_fragment.reset();
    // To the newest: the stretch made at the end again.
    if (!made.to_end && last_model) {
      made = made_range{};
      this->show_conversation(*last_model);
    }
    timeline.scrollToEnd();
    unseen = 0;
    chat.area.parts.jump.set_unseen(0);
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
  // Slid a little at a time, well before the reader reaches its edge: sixty
  // bubbles made in one frame -- made, measured, laid out -- was a frame of
  // 25 ms at every slide, felt as the scroll growing slower further up.
  static constexpr std::size_t kFirstMade = 80, kMostMade = 240, kMadeStep = 16;
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
  // A message to bring into view, once it is made and laid out -- flashed,
  // unless it is where a chat opened, at what it was read up to.
  std::optional<std::string> jumping_to;
  // The chat the jump is in: one asked with a chat's opening -- a link to
  // a message there -- is kept when the chat is first shown.
  std::optional<conversation_id> jump_chat;
  // The chat's first unread message as it was opened: the bar goes over it,
  // and the view opens with it at its top.
  std::optional<std::string> unread_from;
  // The first unread message among those held: the one after the message
  // read up to that the reader did not send. Where that marker is not known
  // yet -- a chat opened before its read marker is loaded, which the server
  // gives only as a count of unread -- that many messages back from the
  // newest, as the server counts them: said, not done, and not the reader's.
  static std::optional<std::string> first_unread_here(const conversation& one, const auto& all) {
    if (one.unread_here() <= 0)
      return std::nullopt;
    if (one.read_up_to) {
      if (const auto read = std::ranges::find(all, *one.read_up_to, &message::id); read != all.end())
        for (auto it = std::next(read); it != all.end(); ++it)
          if (!it->outgoing)
            return it->id;
      return std::nullopt;
    }
    std::optional<std::string> found;
    std::int64_t left = one.unread_here();
    for (auto it = all.rbegin(); it != all.rend() && left > 0; ++it)
      if (!it->outgoing && !it->service) {
        found = it->id;
        --left;
      }
    return found;
  }
  // Where a chat with unread opens: its first unread held, or else the
  // message read up to, to be fetched with what is around it.
  static std::optional<std::string> first_unread(const conversation& one, const auto& all) {
    if (auto here = first_unread_here(one, all))
      return here;
    if (one.unread_here() > 0 && one.read_up_to)
      return one.read_up_to;
    return std::nullopt;
  }
  bool jump_quiet = false;
  int jump_tries = 0;
  // Frames a jump has been on its way: the loader shows past a few.
  int jump_age = 0;
  // A message jumped to and made, aimed at until it stays where it was
  // aimed: what is above it may still grow as it is made and laid out.
  std::optional<std::string> aiming;
  bool aim_quiet = false;
  int aim_frames = 0;
  float aimed_at = -1.0f;

  // While a jump is on its way or being aimed: the message jumped to. What is
  // loaded to reach it is not looked at, and only the pictures right around
  // it are fetched.
  [[nodiscard]] const std::optional<std::string>& jump_target() const { return jumping_to ? jumping_to : aiming; }

  // The newest message whose end is on screen in the chat shown: how far it
  // has been read. None while a jump is on its way, as what is passed on the
  // way is not read.
  // Every message wholly or partly on screen now, not while a jump is on its
  // way: what the user can be said to see.
  [[nodiscard]] std::vector<std::string> shown_now() {
    std::vector<std::string> out;
    if (jumping_to)
      return out;
    const skia::SkRect view = timeline.bounds();
    for (const message_bubble& row : std::get<0>(std::get<0>(timeline.fChildren).fChildren)) {
      // Laid out as if unscrolled: where it is in the view.
      const skia::SkRect box = timeline.toView(row.bounds());
      if (!row.message_id.empty() && !box.isEmpty() && row.visible() && box.fBottom > view.fTop + 8.0f &&
          box.fTop < view.fBottom - 8.0f)
        out.push_back(row.message_id);
    }
    return out;
  }
  [[nodiscard]] std::optional<std::string> last_seen() {
    if (jumping_to || aiming)
      return std::nullopt;
    const skia::SkRect view = timeline.bounds();
    const auto& entries = std::get<0>(std::get<0>(timeline.fChildren).fChildren);
    for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
      const skia::SkRect box = timeline.toView(it->bounds());
      if (it->message_id.empty() || box.isEmpty())
        continue;
      if (box.fBottom <= view.fBottom + 1.0f && box.fBottom > view.fTop)
        return it->message_id;
    }
    return std::nullopt;
  }
  // The window asked around a message, and the page forward asked: not
  // asked twice.
  std::optional<std::string> context_asked, newer_asked;

  // The part of the message jumped to that a reply quoted: marked in it,
  // and aimed at rather than its top.
  std::optional<std::string> jump_fragment;
  void jump_to(std::string id, std::optional<std::string> fragment) {
    this->jump_to(std::move(id));
    jump_fragment = std::move(fragment);
  }
  // Jumps said, where MUX_TRACE_FRAMES is set: asked, and how each goes.
  static bool trace_jumps() {
    static const bool on = std::getenv("MUX_TRACE_FRAMES") != nullptr;
    return on;
  }
  void jump_to(std::string id) {
    jump_fragment.reset();
    if (trace_jumps())
      std::cerr << "[jump] to " << id << (chosen && last_model ? "" : " (no chat shown: dropped)") << '\n';
    if (!chosen || !last_model)
      return;
    const conversation* one = last_model->find(*chosen);
    if (!one)
      return;
    // Made, loaded or paged back to at the next frames, as update() finds it.
    jumping_to = std::move(id);
    scene::work::mark(fState.fId);  // update() looked at from the next frame, and ticked until it lands
    jump_chat = chosen;
    jump_quiet = false;
    jump_tries = 0;
    jump_base.reset();
    jump_paging = false;
    jump_age = 0;
    aiming.reset();
    context_asked.reset();
  }

  // The pinned bar: of the chat's pins, the one `pinned_step` says -- the
  // newest where none was worked out yet.
  void show_pinned(const conversation* one) {
    {
      auto& bar = chat.parts.pinned;
      if (!one || one->pinned.empty()) {
        bar.setVisible(false);
      } else {
        const std::size_t count = one->pinned.size();
        const std::size_t at = std::min(pinned_step, count - 1);
        const std::string& id = one->pinned[at];
        pinned_view shown{id, count == 1 ? std::string("Pinned message")
                                         : std::format("Pinned message #{} of {}", at + 1, count),
                          "A message"};
        const auto found = std::ranges::find(one->timeline, id, &message::id);
        const message* said = found != one->timeline.end() ? &*found : nullptr;
        if (!said)
          if (const auto aside = one->quoted.find(id); aside != one->quoted.end())
            said = &aside->second;
        if (said) {
          shown.line = said->body.plain.empty() && said->attachment ? std::string("Photo") : said->body.plain;
          std::ranges::replace(shown.line, '\n', ' ');
        }
        bar.setVisible(true);
        bar.show(shown, [this](const pinned_view& view) { return pinned_bar<pinned_press>({this, view.id}, view); });
      }
    }
  }
  // The pin the bar shows, as Telegram's: the newest pinned above the view
  // -- before the first message seen -- else the oldest. A pin not loaded
  // is taken as older than all that is.
  [[nodiscard]] std::size_t pin_above(const conversation& one) {
    const skia::SkRect view = timeline.bounds();
    const auto& entries = std::get<0>(std::get<0>(timeline.fChildren).fChildren);
    const auto first = std::ranges::find_if(entries, [&](const message_bubble& row) {
      const skia::SkRect box = timeline.toView(row.bounds());
      return !row.message_id.empty() && row.visible() && !box.isEmpty() && box.fBottom > view.fTop + 8.0f;
    });
    const auto place = [&](const std::string& id) -> std::ptrdiff_t {
      const auto found = std::ranges::find(one.timeline, id, &message::id);
      return found == one.timeline.end() ? -1 : found - one.timeline.begin();
    };
    const std::ptrdiff_t top = first == entries.end() ? std::numeric_limits<std::ptrdiff_t>::max()
                                                      : place(first->message_id);
    std::optional<std::size_t> best;
    std::ptrdiff_t best_place = -2;
    for (std::size_t i = 0; i < one.pinned.size(); ++i)
      if (const std::ptrdiff_t at = place(one.pinned[i]); at < top && at >= best_place) {
        best = i;
        best_place = at;
      }
    return best.value_or(0);
  }
  // What the pin above the view was last worked out from.
  struct pin_inputs {
    conversation_id chat;
    float at = 0.0f;
    std::size_t held = 0;
    std::size_t pins = 0;
    std::string first;
    friend bool operator==(const pin_inputs&, const pin_inputs&) = default;
  };
  std::optional<pin_inputs> pin_worked_out;
  // The message jumped to no longer looked for: where the view is, it stays.
  void stop_jump() {
    jumping_to.reset();
    jump_base.reset();
    jump_paging = false;
    context_asked.reset();
    jump_tries = 0;
  }
  // Ticked while something is on its way -- a jump, an aim, a glide, rooms
  // named and not yet come -- and else looked at only as what it watches
  // changes: the view scrolled, the composer or the search typed into, the
  // model shown, each of which marks it. It was run at every frame.
  [[nodiscard]] bool wantsTick() const {
    return jumping_to.has_value() || aiming.has_value() || jump_age != 0 || timeline.moving() ||
           !rooms_waiting.empty() || !rooms_unfound.empty();
  }
  void update(double) {
    // A room a bubble names has come -- its picture, or word that it is
    // there: the bubbles made again.
    if (last_model &&
        (std::ranges::any_of(rooms_waiting, [](const std::string& key) { return avatar_images().has(key); }) ||
         std::ranges::any_of(rooms_unfound, [](const std::string& key) { return rooms_found().contains(key); })))
      this->show_conversation(*last_model);
    // The pin the bar shows, as the view moves: the one above it. Not while
    // a jump goes on -- where it lands decides.
    // Worked out again only where the view, the messages or the pins moved:
    // it looks each pin up in the whole timeline.
    if (!jumping_to && !aiming && chosen && last_model)
      if (const conversation* one = last_model->find(*chosen); one && !one->pinned.empty()) {
        const pin_inputs now{*chosen, timeline.current(), one->timeline.size(), one->pinned.size(),
                             one->timeline.empty() ? std::string() : one->timeline.front().id};
        if (now != pin_worked_out) {
          pin_worked_out = now;
          if (const std::size_t want = this->pin_above(*one); want != std::min(pinned_step, one->pinned.size() - 1)) {
            pinned_step = want;
            this->show_pinned(one);
          }
        }
      }
    // A message jumped to: made into a bubble where it is loaded, paged back
    // to where it is not -- page after page, as long as there is history --
    // and once it is laid out, brought into view and flashed.
    if (jumping_to && chosen && last_model) {
      auto& entries = std::get<0>(std::get<0>(timeline.fChildren).fChildren);
      auto it = std::ranges::find(entries, *jumping_to, &message_bubble::message_id);
      const conversation* one = last_model->find(*chosen);
      // A reply to something done in the room -- a join, a rename -- whose
      // line is hidden, as the chat's settings say: landed on the nearest
      // shown after it, else before it, as its place.
      if (it != entries.end() && !it->visible()) {
        auto shown = std::find_if(it, entries.end(), [](const message_bubble& row) { return row.visible(); });
        if (shown == entries.end()) {
          const auto back = std::find_if(std::make_reverse_iterator(it), entries.rend(),
                                         [](const message_bubble& row) { return row.visible(); });
          shown = back == entries.rend() ? entries.end() : std::prev(back.base());
        }
        if (shown != entries.end()) {
          jumping_to = shown->message_id;
          it = shown;
        }
      }
      if (trace_jumps() && jump_tries % 30 == 0)
        std::cerr << "[jump] " << *jumping_to << (it == entries.end() ? " not made" : it->bounds().isEmpty() ? " made, not laid out" : " laid out")
                  << (one && std::ranges::contains(one->timeline, *jumping_to, &message::id) ? ", held" : ", not held") << '\n';
      if (it != entries.end() && !it->bounds().isEmpty()) {
        aiming = std::exchange(jumping_to, std::nullopt);
        aim_quiet = jump_quiet;
        aim_frames = 0;
        aimed_at = -1.0f;
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
        } else if (it != entries.end() && it->visible() && !timeline.moving()) {
          // Made, but not laid out: more than a screen from the view, where
          // the list lays nothing out. The view stepped a screen toward it,
          // until it is laid out and aimed at.
          const auto laid = std::ranges::find_if(entries, [](const message_bubble& row) { return !row.bounds().isEmpty(); });
          if (laid != entries.end()) {
            const bool above = it < laid;
            const float page = timeline.bounds().height();
            timeline.setCurrent(std::max(0.0f, timeline.current() + (above ? -page : page)));
          }
        }
      } else if (is_matrix(chosen->account.speaks) && !jump_paging) {
        // Not here: a window of the history around it, from the server --
        // not all of it from here to there. Where that does not bring it,
        // paged back to, as far as the chat's limit.
        if (context_asked != jumping_to) {
          context_asked = jumping_to;
          actions->load_context(*chosen, *jumping_to);
        } else if (++jump_tries > 240) {
          jump_paging = true;
          jump_tries = 0;
        }
      } else if (history_from && history_asked != history_from) {
        // Paged back, as far as the chat's limit says: past it, given up.
        if (!jump_base)
          jump_base = one->timeline.size();
        const auto limit = jump_limits.find(*chosen);
        const std::int64_t most = limit == jump_limits.end() ? 5000 : limit->second;
        if (most > 0 && static_cast<std::int64_t>(one->timeline.size() - std::min(*jump_base, one->timeline.size())) >= most) {
          this->stop_jump();
        } else {
          history_asked = history_from;
          actions->load_older(*chosen, *history_from);
        }
      } else if (!history_from && ++jump_tries > 120) {
        jumping_to.reset();  // the beginning, and it was not there
      }
    }
    // A message jumped to, aimed at until it stays: in the middle of the
    // view, as tdesktop brings one (its top, where it is taller than the
    // view) -- or, opened at what was read, near the top with the unread
    // below. Aimed again each frame while what is above it moves it; flashed
    // once the list is still, where the flash is seen.
    if (aiming) {
      auto& entries = std::get<0>(std::get<0>(timeline.fChildren).fChildren);
      const auto it = std::ranges::find(entries, *aiming, &message_bubble::message_id);
      if (it == entries.end() || it->bounds().isEmpty() || ++aim_frames > 180) {
        aiming.reset();
      } else {
        const skia::SkRect view = timeline.bounds();
        const skia::SkRect box = timeline.toView(it->bounds());
        const float above = aim_quiet ? (unread_from && *aiming == *unread_from ? 0.0f : 60.0f)
                            : box.height() < view.height() ? (view.height() - box.height()) * 0.5f
                                                           : 0.0f;
        float to = std::max(0.0f, timeline.current() + (box.fTop - view.fTop) - above);
        // Where a reply quoted a part of it: that part marked, and its line
        // brought to the view's upper middle -- the right place of a message
        // taller than the view.
        if (aimed_at < 0.0f && jump_fragment) {
          if (const auto at = it->mark(*jump_fragment)) {
            const auto& text = it->parts.body.parts.text;
            const float line = timeline.toView(text.bounds()).fTop + text.lineTopOf(*at);
            to = std::max(0.0f, timeline.current() + (line - view.fTop) - view.height() * 0.4f);
          }
          jump_fragment.reset();
        }
        // The first aim: flashed at once, where it is -- not once all above
        // it has settled, which can be never, and the flash was lost. Far
        // away -- a window just loaded around it -- put there at once, not
        // glided to through what was loaded; near, glided to.
        if (aimed_at < 0.0f) {
          if (std::abs(to - timeline.current()) > view.height() * 2.0f)
            timeline.setCurrent(to);
          else
            timeline.scrollTo(to);
          aimed_at = to;
          if (!aim_quiet) {
            it->flash.jump(1.0f);
            it->flash.setTarget(0.0f);
            it->markDamaged();
          }
        } else if (std::abs(to - aimed_at) > 1.0f) {
          timeline.setCurrent(to);
          aimed_at = to;
        } else if (!timeline.moving()) {
          aiming.reset();
        }
      }
    }
    // A jump on its way for more than a few frames -- fetched, or paged back
    // to -- shows the loader turning in the middle of the list.
    jump_age = jumping_to ? jump_age + 1 : 0;
    // A jump that has not got there in ten seconds of frames is let go:
    // while one goes on, frames are asked for, and one that could not land
    // -- a message not to be had, a bubble never laid out -- asked forever.
    if (jump_age > 600) {
      if (trace_jumps() && jumping_to)
        std::cerr << "[jump] " << *jumping_to << " given up\n";
      this->stop_jump();
      jump_age = 0;
    }
    if (const bool loading = jump_age > 6; loading != chat.area.parts.loading.visible())
      chat.area.parts.loading.setVisible(loading);
    this->find_mentions();
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
    if (away != chat.area.parts.jump.visible())
      chat.area.parts.jump.setVisible(away);
    // The @ and the heart, stacked over "↓" where it is up.
    if (const conversation* here = chosen && last_model ? last_model->find(*chosen) : nullptr) {
      int slot = chat.area.parts.jump.visible() ? 1 : 0;
      chat.area.parts.mentions.show(here->unread_mentions.size(), slot);
      if (!here->unread_mentions.empty())
        ++slot;
      chat.area.parts.reactions.show(here->unread_reactions.size(), slot);
    }
    if (!away && unseen != 0) {
      unseen = 0;
      chat.area.parts.jump.set_unseen(0);
    }
    // Near the top: the stretch made slides up -- the far ones below let
    // go -- and past all that is loaded, the history is paged back. Near
    // the bottom, where it is not at the end, it slides down.
    if (chosen && last_model && !jumping_to)
      if (const conversation* one = last_model->find(*chosen)) {
        auto [from, to] = this->made_indices(one->timeline);
        // Slid while a screen and a half is still made beyond the view.
        const float ahead = std::max(300.0f, timeline.bounds().height() * 1.5f);
        if (timeline.current() <= ahead && from > 0) {
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
        } else if (!made.to_end && timeline.current() >= timeline.extent() - ahead) {
          to = std::min(one->timeline.size(), to + kMadeStep);
          from = to > kMostMade && to - from > kMostMade ? to - kMostMade : from;
          this->set_made(one->timeline, from, to);
          this->show_conversation(*last_model);
        }
      }
  }

  void show(const model& now) {
    last_model = &now;
    if (wanted && now.accounts().contains(*wanted)) {
      current = std::exchange(wanted, std::nullopt);
    } else if (!current || !now.accounts().contains(*current)) {
      current = now.accounts().empty() ? std::nullopt : std::optional<account_id>(now.accounts().begin()->first);
    }
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
    // Made again only where they changed: made at every change in the model,
    // new tabs were a full walk and the bar laid out and painted again.
    auto& tabs = std::get<0>(side.folders.fChildren);
    if (folders != shown_folders || folder != shown_folder) {
      tabs.clear();
      for (auto& [name, which] : folders)
        tabs.emplace_back(name, which, which == folder, pick_folder{this});
      shown_folders = folders;
      shown_folder = folder;
    }
    side.folders.setVisible(folders.size() > 1);
    // Whether a chat is in the folder chosen. A space is a folder, not a
    // chat: it is never listed.
    const account* in = current ? &now.accounts().at(*current) : nullptr;
    const auto in_folder = [&](const conversation& one) {
      if (one.space)
        return false;
      return splice::visit(splice::overloaded{[](const folder::all&) { return true; },
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
    // Each chat's room events as it shows them: what it hides is not its
    // newest, nor counted.
    const auto events_of = [&](const conversation* one) {
      const auto found = event_filters.find(one->id);
      return found == event_filters.end() ? room_event_filter{} : found->second;
    };
    std::ranges::sort(chats, std::ranges::greater{}, [&](const conversation* one) {
      const message* last = newest(*one, events_of(one));
      return last ? last->at : std::chrono::sys_time<std::chrono::milliseconds>{};
    });
    // The rows, as a function of the chats: those whose chat shows the same
    // are kept as they are.
    const auto is_chosen = [&](const conversation* one) { return chosen && *chosen == one->id; };
    if (nodes::reconcile(
            rows, chats, [](const conversation* one) { return one->id; },
            [](const conversation_row<Actions>& row) { return row.id; },
            [&](const conversation* one) {
              return conversation_row<Actions>(actions, *one, is_chosen(one), muted.contains(one->id), draft_of(one->id),
                                               events_of(one));
            },
            [&](const conversation_row<Actions>& row, const conversation* one) {
              return row.shown ==
                     conversation_row<Actions>::view_of(*one, is_chosen(one), muted.contains(one->id), draft_of(one->id),
                                                        events_of(one));
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
    // Laid out again, repainting only what moves: what changed repaints
    // itself. Invalidated, the whole window was painted at every change in
    // the model -- a hidden event's too.
    fState.relayoutQuietly();
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
    if (pinned_of != chosen) {
      pinned_of = chosen;
      pinned_step = std::numeric_limits<std::size_t>::max();
    }
    this->show_pinned(one);
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
    // A room's event -- a join, an address set -- is a line of its own and
    // ends a run: the message after it has its sender's name again. Where
    // the room's events are hidden, a run goes on past them.
    const auto filter_found = event_filters.find(one->id);
    const room_event_filter filter = filter_found == event_filters.end() ? room_event_filter{} : filter_found->second;
    const auto shows = [&](const message& said) { return !said.service || filter.shows(said.event_kind); };
    const auto neighbour = [&](std::size_t i, bool forward) -> std::optional<std::size_t> {
      for (std::size_t j = i;;) {
        if (forward ? j + 1 >= all.size() : j == 0)
          return std::nullopt;
        j = forward ? j + 1 : j - 1;
        if (shows(all[j]))
          return j;
      }
    };
    const auto same = [&](std::size_t i, std::optional<std::size_t> j) {
      return j && !all[i].service && !all[*j].service && all[*j].sender == all[i].sender &&
             all[*j].outgoing == all[i].outgoing;
    };
    // Who has read up to where, where the chat shows it: each other person
    // on the message their receipt points at -- or, pointing at what is not
    // shown or not here, the nearest shown before it, by its time.
    std::map<std::string, std::vector<std::string>> readers;
    if (receipts_in.contains(one->id)) {
      std::map<std::string, std::size_t> place;
      for (std::size_t i = 0; i < all.size(); ++i)
        place.emplace(all[i].id, i);
      for (const auto& [user, event] : one->read_by) {
        if (user == one->id.account.address)
          continue;
        std::optional<std::size_t> at;
        if (const auto found = place.find(event); found != place.end())
          at = found->second;
        else if (const auto when = one->receipt_times.find(user); when != one->receipt_times.end())
          for (std::size_t j = all.size(); j-- > 0;)
            if (all[j].at <= when->second) {
              at = j;
              break;
            }
        while (at && !shows(all[*at]))
          at = *at == 0 ? std::nullopt : std::optional<std::size_t>(*at - 1);
        if (at)
          readers[all[*at].id].push_back(user);
      }
    }
    const auto readers_of = [&](std::size_t i) {
      const auto found = readers.find(all[i].id);
      return found == readers.end() ? std::vector<std::string>{} : found->second;
    };
    const auto first_of_run = [&](std::size_t i) { return !same(i, neighbour(i, false)); };
    const auto last_of_run = [&](std::size_t i) { return !same(i, neighbour(i, true)); };
    // A chat shown anew: its stretch as it was left, or its newest.
    if (shown_chat != chosen) {
      if (shown_chat)
        made_of[*shown_chat] = made;
      const auto kept = made_of.find(*chosen);
      made = kept == made_of.end() ? made_range{} : kept->second;
    }
    const auto [first_made, last_made] = this->made_indices(all);
    this->set_made(all, first_made, last_made);
    // A message that has just come into the chat being read comes in moving;
    // one made again -- changed, or scrolled back into what is made -- and
    // those of a chat just opened do not. One's own, once the server has it,
    // is the same message under its new id, and does not come in twice.
    const bool same_chat = shown_chat == chosen;
    const auto arrives = [&](std::size_t i) {
      const bool known = !appeared.insert(all[i].id).second;
      const bool acknowledged = all[i].outgoing && splice::visit(splice::overloaded{[](const delivery::sent&) { return true; },
                                                                         [](const auto&) { return false; }},
                                                              all[i].delivery);
      return same_chat && !known && !acknowledged && i + 3 >= all.size();
    };
    // The bubbles, as a function of the messages: those that show the same
    // are kept -- with a selection in them -- and only the new are made.
    if (nodes::reconcile(
            entries, std::views::iota(first_made, last_made),
            [&](std::size_t i) { return all[i].id; }, [](const message_bubble& row) { return row.message_id; },
            [&](std::size_t i) {
              message_bubble made(*one, all[i], first_of_run(i), last_of_run(i), &now, shows(all[i]),
                                  !previews_off.contains(one->id));
              if (unread_from && all[i].id == *unread_from)
                made.mark_unread_start();
              made.show_readers(*one, readers_of(i));
              rooms_wanted.insert(made.rooms_unknown.begin(), made.rooms_unknown.end());
              if (arrives(i))
                made.appear();
              return made;
            },
            [&](const message_bubble& row, std::size_t i) {
              const bool quote_known = !all[i].replies_to || one->quoted.contains(*all[i].replies_to) ||
                                       std::ranges::find(all, *all[i].replies_to, &message::id) != all.end();
              const auto link = first_link_of(all[i]);
              const bool preview_known = link && now.previews.contains(*link);
              return row.said == all[i] && row.first == first_of_run(i) && row.last == last_of_run(i) &&
                     row.quote_known == quote_known && row.events_shown == shows(all[i]) && row.unread_start == (unread_from && all[i].id == *unread_from) &&
                     row.preview_known == preview_known && row.readers_shown == readers_of(i) &&
                     row.previews_shown == !previews_off.contains(one->id) && !row.rooms_came();
            }))
      // Laid out again; painted where rows came, went or moved -- a hidden
      // one coming moves nothing, and paints nothing.
      std::get<0>(timeline.fChildren).fState.relayoutQuietly();
    rooms_waiting.clear();
    rooms_unfound.clear();
    for (const message_bubble& row : entries) {
      rooms_waiting.insert(row.rooms_waiting.begin(), row.rooms_waiting.end());
      rooms_unfound.insert(row.rooms_unknown.begin(), row.rooms_unknown.end());
    }
    // The newest is at the bottom: the view follows it where the reader was
    // there or the chat is new to the view; otherwise what came after the
    // last one seen is counted on the way down.
    const std::string last = all.empty() ? std::string() : all.back().id;
    if (shown_chat != chosen) {
      if (shown_chat)
        scrolled[*shown_chat] = was_at_end ? -1.0f : left_at;
      const auto kept = scrolled.find(*chosen);
      // A jump asked for in another chat is let go; one asked with this
      // chat's opening -- a link to a message in it -- decides where the
      // view goes: not the unread, nor where it was left, first.
      if (jumping_to && jump_chat != chosen)
        jumping_to.reset();
      unread_from.reset();
      if (jumping_to) {
        timeline.scrollToEnd(false);
      } else if (const std::optional<std::string> first = first_unread(*one, all)) {
        // Unread in it: opened at its first unread, at the view's top under
        // tdesktop's bar, and read on from there as it is seen -- where the
        // message read up to is not here, first what is around it.
        timeline.scrollToEnd(false);
        unread_from = first_unread_here(*one, all);
        jumping_to = *first;
        jump_chat = chosen;
        jump_quiet = true;
        jump_tries = 0;
        context_asked.reset();
      } else if (kept == scrolled.end() || kept->second < 0.0f)
        timeline.scrollToEnd(false);  // a chat opened starts at its newest
      else
        timeline.setCurrent(kept->second);
      unseen = 0;
    } else if (was_at_end && !jumping_to && !aiming) {
      // At the newest, it follows what comes -- not while a jump goes
      // elsewhere: a refresh each time the model changes put the view back
      // at the end under a jump on its way up, and it never got there.
      timeline.scrollToEnd();
      unseen = 0;
    } else if (last != shown_last) {
      // What came after the newest shown before: others' messages the view
      // shows. Where that one is not here any more -- its id changed as the
      // server acknowledged it, or it went -- nothing is counted: every
      // message held was, and a few new ones said 64. Nor the reader's own,
      // nor a room event the view hides.
      int after = 0;
      if (const auto was = std::ranges::find(all, shown_last, &message::id); was != all.end())
        for (auto it = std::next(was); it != all.end(); ++it)
          if (!it->outgoing && this->shown_in(*chosen, *it))
            ++after;
      unseen += after;
    }
    chat.area.parts.jump.set_unseen(unseen);
    shown_chat = chosen;
    shown_last = last;
  }
};

}  // namespace mux::ui
