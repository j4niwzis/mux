// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:conversations_screen -- The conversations screen: the list, the chat, its info.
export module mux.ui:conversations_screen;

import std;
import mux.logic.text;
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
import mux.protocols;
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
import :call_bar;

export import :conversations_side;

export namespace mux::ui {

namespace head_view_defaults {
template <class Actions>
constexpr proto::sticker_view_list<> head_views(const auto&, type_tag<Actions>) {
  return {};
}
template <class Actions>
constexpr std::nullopt_t make_head_view(const auto&, const conversation&, type_tag<Actions>) {
  return std::nullopt;
}
}  // namespace head_view_defaults
template <class State, class Actions>
constexpr auto head_views_for(const State& state, type_tag<Actions> tag) {
  using head_view_defaults::head_views;
  return head_views(state, tag);
}
namespace composer_view_defaults {
template <class Actions>
constexpr proto::sticker_view_list<> composer_views(const auto&, type_tag<Actions>) {
  return {};
}
template <class Actions>
constexpr std::nullopt_t make_composer_view(const auto&, const conversation&, type_tag<Actions>) {
  return std::nullopt;
}
}  // namespace composer_view_defaults
template <class State, class Actions>
constexpr auto composer_views_for(const State& state, type_tag<Actions> tag) {
  using composer_view_defaults::composer_views;
  return composer_views(state, tag);
}

template <class Actions>
struct conversations_screen : nodes::Stack {
  Actions* actions = nullptr;
  // What it was handed, for the rows it makes.
  ui_needs<Actions> needs_;
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
  // Chats listed in another account's list than their own: each
  // account's, the chats moved out of their own, and the strip each shows
  // where it is listed elsewhere -- none where it is off. As the program
  // keeps them.
  std::map<account_id, std::vector<conversation_id>> listed_in;
  std::set<conversation_id> moved_out;
  std::map<conversation_id, skia::SkColor> strips;
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
  struct mention_row : pressable<nodes::Stack> {
    pick_mention act;
    struct parts_t {
      avatar_mark face;
      two_lines texts;
    } parts;
    mention_row(const palette& colours, pick_mention what, const member& one)
        : act(what), parts{.face = avatar_mark(one.id, one.name.empty() ? one.id : one.name, 28.0f),
                           .texts = two_lines(colours, one.name.empty() ? one.id : one.name, one.id, 14.0f, 1.0f)} {
      this->setHorizontal();
      this->setGap(10.0f);
      fState.apply({.fillX = true, .height = 44.0f, .padding = {0.0f, 14.0f, 0.0f, 14.0f},
                    .hoverBackground = colours.chosen, .selectedBackground = colours.chosen});
    }
    void set_lit(bool on) { fState.apply({.selected = on}); }
  };
  struct mention_list : nodes::Stack {
    struct parts_t {
      std::vector<mention_row> rows;
    } parts;
    explicit mention_list(const palette& colours) {
      fState.apply({.fillX = true, .autoSize = scene::axes::kY, .background = colours.sidebar});
    }
  };
  // The account to list once the model has it: the one shown last, kept.
  // Taken the first time it is there; dropped when an account is chosen.
  std::optional<account_id> wanted;
  bool info_open = false;
  // How wide the chat list and the chat's info are: their own, whatever the
  // window's size, until their edges are dragged.
  float side_width = 300.0f;
  float info_width = 340.0f;


  // The folder whose chats are listed.
  folder_t folder = folder::all{};
  // The folders the tabs were made for, and the one chosen then.
  std::vector<std::pair<std::string, folder_t>> shown_folders;
  folder_t shown_folder = folder::all{};
  // The space bars, as the program says: at all, the top one, and where
  // each item is put; and what they were made for, not made again unchanged.
  bool spaces_on = true;
  bool top_bar_on = true;
  // The spaces shown as forums, and the one open in the list -- its rooms
  // listed, as tdesktop lists a forum's topics.
  std::set<conversation_id> forums;
  // Spaces whose rooms Home leaves out, each as it chose.
  std::set<conversation_id> hidden_from_home;
  std::optional<std::string> forum_open;
  // A forum gone to by Alt+Up or Alt+Down: lit as chosen, not yet opened --
  // Alt+Right opens it, as Alt+Left leaves one open.
  std::optional<conversation_id> pointed;
  // A chat shown as a forum: a space the user shows so, or a forum of its
  // protocol's own (proto::native_forum).
  [[nodiscard]] bool shown_as_forum(const conversation& one) const {
    return one.space && (forums.contains(one.id) || proto::native_forum(protocol_state_of(*needs_.shared, one.id.account), one));
  }
  [[nodiscard]] bool is_forum(const conversation_id& id) const {
    const conversation* one = last_model ? last_model->find(id) : nullptr;
    return one && this->shown_as_forum(*one);
  }
  // Home without what spaces hold, but direct messages -- and without those
  // too, where that is chosen as well.
  bool home_hides_spaced = false;
  bool home_hides_direct = false;
  // The messages a search in the chat found, in the chats' place while it is
  // asked; none, the chats again.
  void show_search_results(std::vector<search_result> results, std::optional<std::size_t> count) {
    auto& rows = std::get<0>(std::get<0>(side.found.fChildren).fChildren);
    rows.clear();
    rows.reserve(results.size());
    for (const search_result& one : results)
      rows.emplace_back(*needs_.colours, actions, one);
    const bool on = count.has_value();
    side.found_title.setText(!count ? std::string() : *count == 0 ? std::string("No messages found")
                                    : *count == 1                ? std::string("1 message found")
                                                                 : std::format("{} messages found", *count));
    side.found_title.setVisible(on);
    side.found.setVisible(on);
    side.list.setVisible(!on);
    side.found.scrollToStart();
    side.invalidateLayout();
    side.markDamaged();
  }
  // The server's answers for what is searched, where nothing joined matched
  // it: kept while they are for what is typed now.
  void found_rooms_elsewhere(const std::string& query, const std::vector<directory_room>& rooms) {
    if (query.empty() || query != asked_elsewhere)
      return;
    rooms_elsewhere = rooms;
    rooms_came = true;
    this->show_elsewhere();
  }
  void found_people_elsewhere(const std::string& query, const std::vector<found_person>& people) {
    if (query.empty() || query != asked_elsewhere)
      return;
    people_elsewhere = people;
    people_came = true;
    for (const found_person& one : people)
      if (one.avatar && !one.avatar->empty())
        listed_avatars().emplace_back(one.id, *one.avatar);
    this->show_elsewhere();
  }
  void show_elsewhere() {
    auto& shown = std::get<0>(side.elsewhere.fChildren);
    auto& rooms = std::get<0>(shown.parts.rooms.fChildren);
    rooms.clear();
    for (const directory_room& one : rooms_elsewhere | std::views::take(30))
      rooms.emplace_back(actions, *needs_.colours, one, std::string());
    auto& people = std::get<0>(shown.parts.people.fChildren);
    people.clear();
    for (const found_person& one : people_elsewhere | std::views::take(30))
      people.emplace_back(actions, *needs_.colours, one);
    shown.parts.rooms_title.setVisible(!rooms.empty());
    shown.parts.people_title.setVisible(!people.empty());
    const bool waiting = !rooms_came || !people_came;
    shown.parts.status.setText(waiting ? "Searching the server\u2026" : "No rooms or people found.");
    shown.parts.status.setVisible(rooms.empty() && people.empty());
    side.elsewhere.scrollToStart();
    side.elsewhere.invalidateLayout();
    side.invalidateLayout();
    side.markDamaged();
  }
  void open_forum(std::string room) {
    forum_open = std::move(room);
    this->slide_list(1.0f);
    if (last_model)
      this->show(*last_model, false);
  }
  void close_forum() {
    forum_open.reset();
    this->slide_list(-1.0f);
    if (last_model)
      this->show(*last_model, false);
  }
  // The list sliding in as what it lists changes -- another space, a forum
  // opened or left: from the side it comes from, faded in; only drawn
  // moved, nothing laid out again for it.
  skiff::paint::Tween list_in{1.0f, 200.0f, skiff::paint::movement::subtle{}};
  // How many of the chats listed are made into rows: the first few
  // screens, more as the reader scrolls near the last made. A space of
  // hundreds of chats was hundreds of rows made in the frame it was chosen
  // in -- the slide waited on them.
  static constexpr std::size_t kChatsFirst = 40, kChatsStep = 40;
  std::size_t chats_made = kChatsFirst;
  std::size_t chats_listed = 0;
  // Rows that left the list -- another space chosen -- kept by their chat,
  // already laid out and recorded: coming back showing the same, a row is
  // taken back as it was, not made again. A few hundred at most.
  static constexpr std::size_t kRowsKept = 300;
  std::map<conversation_id, conversation_row<Actions>> rows_kept;
  float list_from = 1.0f;
  // Begun two frames on: the frame the list is made in -- its rows made,
  // laid out, their avatars scaled -- took its time out of the slide's
  // first frames, and the slide jumped, then went on.
  int slide_wait = 0;
  void slide_list(float from) {
    chats_made = kChatsFirst;
    list_from = from;
    list_in.jump(0.0f);
    slide_wait = 2;
    this->place_list();
    scene::work::mark(fState.fId);
  }
  // Slid only, not faded: a faded list played each row back through a layer
  // of its own, at every frame of the slide -- it crawled.
  void place_list() {
    const float value = list_in.value();
    for (scene::Node* each : std::initializer_list<scene::Node*>{&side.list, &side.forum_head})
      each->apply({.shiftX = (1.0f - value) * 32.0f * list_from});
  }
  std::vector<config::space_placed> space_places;
  std::vector<std::string> shown_bars;
  // A phone's way (the user's, #13548): a window narrow and taller than it is
  // wide shows one thing at a time, all of it across -- the chats, a chat,
  // or its info or threads -- not side by side; and how wide it is.
  bool single = false;
  float single_width = 0.0f;
  // Where a swipe across the chat began, single: left to right, back to
  // the chats.
  std::optional<skia::SkPoint> swipe_from;
  // The row a swipe across the chats began on: right to left, muted or not,
  // as Telegram's quick action.
  std::optional<conversation_id> swipe_row;
  // The spaces along the top bigger, for a finger: a long press on them.
  bool big_spaces = false;
  // The panel docked under the field, as high as it is: the chat standing
  // over it, its field just above it.
  float docked_applied = 0.0f;
  void follow_docked() {
    if (needs_.shared->docked_panel_height == docked_applied)
      return;
    docked_applied = needs_.shared->docked_panel_height;
    // Zero too when the panel goes, or the window grows out of a phone's:
    // the space under the field given back.
    chat.apply({.padding = {0.0f, 0.0f, docked_applied, 0.0f}});
    this->markDamaged();
  }
  // Single, the one shown -- the chats (0), the chat (1), its info or
  // threads (2) -- and it sliding in as it changes, as a phone's: from the
  // right going in, from the left coming back (the user's, #13662).
  int pane_shown = -1;
  float pane_from = 0.0f;
  skiff::paint::Tween pane_in{1.0f, 240.0f, skiff::paint::movement::sweeping{}};
  void place_pane() {
    const float shift = single ? (1.0f - pane_in.value()) * single_width * pane_from : 0.0f;
    for (scene::Node* each : std::initializer_list<scene::Node*>{&side, &chat, &info, &parts.threads})
      each->apply({.shiftX = shift});
  }
  // What the head was last made from: made again with its back arrow, or
  // without, as the window goes single or not.
  typename chat_header<Actions>::view head_shown;
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
    constexpr auto lower = mux::logic::folded;
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
      rows.emplace_back(*needs_.colours, pick_mention{this, i}, mention_matches[i]);
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
    namespace keys = scene::keys;
    // Alt+Right: into the forum gone to; Alt+Left: out of the one open, to
    // its row. Taken before the field: it moves its caret on Left and Right
    // whatever the modifiers, and took Alt+Left from under this.
    if (press.modifiers.template has<scene::modifier::alt>() && press.key == keys::kRight && pointed) {
      const conversation_id into = *std::exchange(pointed, std::nullopt);
      actions->choose(into);
      reply.handle();
      return;
    }
    if (press.modifiers.template has<scene::modifier::alt>() && press.key == keys::kLeft && forum_open && current) {
      pointed = conversation_id{*current, *forum_open};
      this->close_forum();
      reply.handle();
      return;
    }
    if (!chat.parts.mentions.visible() || mention_matches.empty())
      return;
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
    if (which != folder)
      this->slide_list(1.0f);
    folder = which;
    if (last_model)
      this->show(*last_model, false);
  }

  // The chat list: the drawer's button and the name, then the chats.
  // The side: its space bars' view and its column, picking folders here.
  using icons_t = space_icons<pick_folder>;
  using top_view = ui::top_view<pick_folder>;
  using side_column = ui::side_column<Actions, pick_folder>;
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
  // A banner's button pressed: its protocol's request, asked.
  struct banner_press {
    Actions* actions;
    const std::optional<proto::any_request_t>* asks;
    void operator()() const {
      if (*asks)
        splice::visit(splice::overloaded{[](proto::part::no_request) {}, [&](const auto& one) { actions->ask_for(one); }}, **asks);
    }
  };
  // A node of the chat's protocol's own over the composer (a Telegram bot's
  // keyboard): listed by composer_views(state, type_tag<Actions>), made for
  // a chat by make_composer_view, found by ADL; none by default.
  template <class List>
  struct view_nodes;
  template <class... Vs>
  struct view_nodes<proto::sticker_view_list<Vs...>> {
    using type = type_list<Vs...>;
  };
  template <class>
  struct protocol_composer_nodes;
  template <class... Tags>
  struct protocol_composer_nodes<protocol_list<Tags...>> {
    using type = typename joined<
        type_list<>, typename view_nodes<decltype(composer_views_for(::mux::state_of<Tags>{}, type_tag<Actions>{}))>::type...>::type;
  };
  using composer_view_t = typename variant_of_types<
      typename joined<type_list<nodes::Text>, typename protocol_composer_nodes<protocols>::type>::type>::type;
  // And under the chat's header, over its messages (an IRC channel's topic
  // and modes, a Telegram channel's join button): head_views(state,
  // type_tag<Actions>), made by make_head_view.
  template <class>
  struct protocol_head_nodes;
  template <class... Tags>
  struct protocol_head_nodes<protocol_list<Tags...>> {
    using type = typename joined<
        type_list<>, typename view_nodes<decltype(head_views_for(::mux::state_of<Tags>{}, type_tag<Actions>{}))>::type...>::type;
  };
  using head_view_t = typename variant_of_types<
      typename joined<type_list<nodes::Text>, typename protocol_head_nodes<protocols>::type>::type>::type;
  struct head_view_holder : nodes::Stack {
    struct parts_t {
      head_view_t shown;
    } parts;
    template <class View>
    explicit head_view_holder(View made) : parts{.shown = head_view_t(std::move(made))} {
      fState.apply({.fillX = true, .autoSize = scene::axes::kY});
    }
  };
  struct composer_view_holder : nodes::Stack {
    struct parts_t {
      composer_view_t shown;
    } parts;
    template <class View>
    explicit composer_view_holder(View made) : parts{.shown = composer_view_t(std::move(made))} {
      fState.apply({.fillX = true, .autoSize = scene::axes::kY});
    }
  };
  struct chat_column : nodes::Stack {
    // What the banner's button asks, while it is shown.
    std::optional<proto::any_request_t> banner_asks;
    using header_t = nodes::Memo<typename chat_header<Actions>::view, chat_header<Actions>>;
    using pinned_t = nodes::Memo<pinned_view, pinned_bar<pinned_press>>;
    struct empty_state : nodes::Stack {
      using add_button = widgets::Button<ask<Actions, &Actions::open_new_account>>;
      struct parts_t {
        nodes::Text title;
        nodes::Text note;
        add_button add;
      } parts;
      empty_state(const palette& colours, Actions* a)
          : parts{.title = nodes::Text("No accounts yet", 22.0f, colours.text, true),
                  .note = nodes::Text("Add an XMPP or a Matrix account, and its chats will be here.", 14.0f, colours.dim),
                  .add = add_button(colours.widgets, "Add account", {a})} {
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
      selection_bar<Actions> selection;
      // The pinned message, under the head, where the chat has any.
      pinned_t pinned;
      // A call in this chat, as Element's call view: under the head.
      std::optional<call_panel<Actions>> call;
      // Its protocol's own node under the head (make_head_view).
      std::optional<head_view_holder> their_head;
      timeline_area<Actions> area;
      mention_list mentions;
      // Over the composer: what the chat's protocol says there -- Matrix's
      // warning that the other is not verified, or was reset.
      nodes::Text trust_warning;
      std::optional<widgets::Button<banner_press>> banner_button;
      std::optional<composer_view_holder> their_view;
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
    explicit chat_column(const ui_needs<Actions>& n) : chat_column(n, n.actions) {}
    // What it makes its call view with: a copy, as the window keeps one --
    // the needs it was made from were not always there by then.
    ui_needs<Actions> needs_{};
    // The call shown in it, or none.
    void show_call(const call_view& view) {
      if (parts.call)
        parts.call->show(view);
      else
        parts.call.emplace(needs_, view);
      this->invalidateLayout();
      this->markDamaged();
    }
    void hide_call() {
      if (!parts.call)
        return;
      parts.call.reset();
      this->invalidateLayout();
      this->markDamaged();
    }
    chat_column(const ui_needs<Actions>& n, Actions* a)
        : parts{.search = search_bar<Actions>(n),
                .selection = selection_bar<Actions>(n),
                .area = timeline_area<Actions>(n),
                .mentions = mention_list(*n.colours),
                .trust_warning = nodes::Text("", 13.0f, n.colours->text),
                .line = composer_bar<Actions>(n),
                .empty = empty_state(*n.colours, a)} {
      needs_ = n;
      header.apply({.fillX = true, .height = chat_header<Actions>::kHeight});
      parts.pinned.apply({.fillX = true, .height = pinned_bar<pinned_press>::kHeight});
      parts.pinned.setVisible(false);
      header.show({}, [&n](const auto& shown) { return chat_header<Actions>(n, shown); });
      // A plain colour: the wallpaper is the messages' own -- the timeline's
      // Wallpaper -- not behind Select a chat, where Telegram has none.
      // Nothing, where the background is behind the whole window.
      fState.apply({.fillY = true, .grow = scene::axes::kX,
                    .background = n.looks->window.behind ? skia::SkColor{0} : n.colours->chat});
      area.apply({.fillX = true, .grow = scene::axes::kY});
      parts.mentions.setVisible(false);
      parts.trust_warning.setWrapped(true);
      parts.trust_warning.apply({.fillX = true, .padding = {6.0f, 14.0f, 6.0f, 14.0f},
                                 .background = (n.colours->accent & 0x00FFFFFFu) | (0x22u << 24)});
      parts.trust_warning.setVisible(false);
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
    // Threads, in the info's place while they are open.
    threads_panel<Actions> threads;
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
  void onKey(scene::phase::bubble, const scene::key::down& press, scene::Reply& reply);
  // Messages selected: the selection bar in place of the head, and the
  // messages marked; none, the head back.
  void show_selection(const std::set<std::string>& ids, bool forwardable, bool deletable) {
    auto& bar = chat.parts.selection;
    bar.setVisible(!ids.empty());
    if (!ids.empty())
      bar.show(ids.size(), forwardable, deletable);
    header.setVisible(ids.empty() && !search.visible());
    chat.area.set_selected(ids);
    this->invalidateLayout();
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
  nodes::ScrollContainer<nodes::Flow<std::vector<message_bubble<Actions>>>>& timeline = chat.area.parts.timeline;
  // The chat whose messages are shown, how many, and how many came while
  // the view was above the newest.
  std::optional<conversation_id> shown_chat;
  std::string shown_last;
  // Whether the timeline last shown was cut off from the newest: what then
  // comes after it is a page of what is older than the newest, not new.
  bool shown_detached = false;
  // Where each chat was scrolled to when it was left: it comes back there.
  std::map<conversation_id, float> scrolled;
  int unseen = 0;
  composer_bar<Actions>& line = chat.line;

  explicit conversations_screen(const ui_needs<Actions>& n) : conversations_screen(n, n.actions) {}
  conversations_screen(const ui_needs<Actions>& n, Actions* a)
      : actions(a),
        needs_(n),
        parts{.side = side_column(*n.colours, a),
              .edge = side_edge(*n.colours, resize_sidebar_to<Actions>{a}),
              .chat = chat_column(n),
              .info_edge = info_edge_t(*n.colours, resize_info_to<Actions>{a}, false),
              .info = info_panel<Actions>(a, *n.colours, *n.shared),
              .threads = threads_panel<Actions>(n)} {
    fState.apply({.fill = true});
    this->setHorizontal();
    needs_.shared->docked_panel_watcher = fState.fId;
    // The edges take a pixel between the columns, their line, and are
    // wider than that over them to be caught.
    edge.apply({.fillY = true, .width = 7.0f, .margin = {0.0f, -3.0f, 0.0f, -3.0f}});
    info_edge.apply({.fillY = true, .width = 7.0f, .margin = {0.0f, -3.0f, 0.0f, -3.0f}});
    info.apply({.fillY = true, .width = info_width});
    this->show_info();
  }


  // The space bars of the account shown: Home, Direct messages, and its
  // spaces, each in the bars it is put in -- none said, the side one --
  // in the order put there, the rest after them as the model has them.
  void show_space_bars(const model& now);

  // The space menu closed by any press off it, wherever on the screen: at
  // once, where the press is not on it; one on it chooses first.
  using Node::onPointer;
  void onPointer(scene::phase::capture, const scene::pointer::down& press, scene::PointerReply& reply);
  // The swipe let go: far enough to the right, and more across than down --
  // out of the chat, to the chats.
  void onPointer(scene::phase::capture, const scene::pointer::up& lift, scene::PointerReply& reply);
  // Single, a step back: from the threads or the info to the chat, from
  // the chat to the chats -- a swipe across, or Esc.
  void step_back() {
    if (threads_open)
      actions->toggle_threads();
    else if (info_open)
      actions->toggle_info();
    else
      actions->close_chat();
  }
  // Esc too.
  bool close_space_menu() {
    if (!side.menu_up())
      return false;
    side.close_menu();
    return true;
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
    parts.threads.apply({.width = info_width});
  }

  // The chat's info beside it where it is open and a chat is chosen -- or
  // its threads in the info's place, where they are.
  void show_info() {
    const bool shown = (info_open || threads_open) && chosen.has_value();
    info.setVisible(shown && !threads_open);
    parts.threads.setVisible(shown && threads_open);
    info_edge.setVisible(shown && !single);
    side.wanted = side_width;
    // Single: the chats with none chosen; the chat; or its info in its place.
    side.whole = single;
    side.setVisible(!single || !chosen.has_value());
    edge.setVisible(!single);
    chat.setVisible(!single || (chosen.has_value() && !shown));
    const float across = single ? single_width : info_width;
    info.apply({.width = across});
    parts.threads.apply({.width = across});
    const int now_pane = !single ? -1 : !chosen.has_value() ? 0 : shown ? 2 : 1;
    if (single && pane_shown >= 0 && now_pane != pane_shown) {
      pane_from = now_pane > pane_shown ? 1.0f : -1.0f;
      pane_in.jump(0.0f);
      pane_in.setTarget(1.0f);
      scene::work::mark(fState.fId);
    }
    pane_shown = now_pane;
    this->place_pane();
  }
  // Single or not as the window is now: looked at as it is laid out.
  void layoutChildren() {
    // Its id as it is now, for the docked panel to tell it by.
    needs_.shared->docked_panel_watcher = fState.fId;
    const skia::SkRect box = fState.contentBox();
    const bool now = box.width() < 600.0f && box.height() > box.width();
    if (now != single || (now && box.width() != single_width)) {
      if (now != single) {
        head_shown.back = now;
        header.show(head_shown, [this](const auto& shown) { return chat_header<Actions>(needs_, shown); });
      }
      single = now;
      single_width = box.width();
      this->show_info();
    }
    this->nodes::Stack::layoutChildren();
  }
  bool threads_open = false;
  // The chosen chat's background, as the program resolves it.
  config::wallpaper_t wallpaper = config::wallpaper::theme{};
  // And its bubbles' look: the bubbles made again where it changes.
  config::bubble_look bubbles;
  // The threads' panel opened or closed: whether it is open now.
  bool toggle_threads() {
    threads_open = !threads_open;
    parts.threads.open.reset();
    parts.threads.stop_answering();
    this->show_info();
    return threads_open;
  }
  void open_thread(std::string root) {
    threads_open = true;
    if (parts.threads.open != root)
      parts.threads.stop_answering();
    parts.threads.open = std::move(root);
    this->show_info();
  }
  void close_thread() {
    parts.threads.open.reset();
    parts.threads.stop_answering();
  }
  // The thread open, where the panel shows one.
  [[nodiscard]] std::optional<std::string> thread_open() const {
    return threads_open ? parts.threads.open : std::nullopt;
  }
  // What is written goes into the thread open: its field has the keys.
  [[nodiscard]] bool writing_in_thread() const { return thread_open() && parts.threads.parts.line.parts.input.parts.field.focused(); }
  // An answer in the thread open answered there.
  [[nodiscard]] bool answer_in_thread(const std::string& root, std::string id, compose_context said) {
    if (!threads_open || parts.threads.open != root)
      return false;
    parts.threads.answer(std::move(id), std::move(said));
    return true;
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
  // When the older were asked: an answer that never comes -- the request
  // failed, and nothing said so -- let go after a while, and asked again.
  double history_asked_ms = 0.0;
  static constexpr double kHistoryPatienceMs = 10000.0;
  // Asked, and the answer not in yet: the page it brings moves where to
  // page back from on.
  [[nodiscard]] bool history_pending() const { return history_asked && history_from && *history_asked == *history_from; }
  // Something to do at the top: the stretch made to slide up, or older to
  // ask for -- looked at at every frame while the view is up there. The
  // scroll that brought it there stopped in a frame whose tick had gone by
  // here already, and nothing ticked this again: it sat at the top, and
  // nothing more came.
  [[nodiscard]] bool older_due() const {
    if (!chosen || !last_model || jumping_to)
      return false;
    const float ahead = std::max(300.0f, timeline.bounds().height() * 1.5f);
    if (timeline.current() > ahead)
      return false;
    if (const conversation* one = last_model->find(*chosen))
      if (this->made_indices(one->timeline).first > 0)
        return true;
    return timeline.current() <= 4.0f && history_from.has_value() && !this->history_pending();
  }

  // Frames wanted while a jump goes on: it is carried out a step a frame --
  // made, paged back to, fetched around, aimed at -- and skiff draws the next
  // frame only for what is still settling. And while the list slides in --
  // another space, a forum -- and the panels ease to a chat's opacity: only
  // ticked (wantsTick), they moved a step at a frame something else asked
  // for, and the slide went by in jerks unless the mouse moved.
  [[nodiscard]] bool settling() const {
    return jumping_to.has_value() || aiming.has_value() || slide_wait > 0 || list_in.moving() ||
           needs_.paint->ease.t.moving() || pane_in.moving();
  }
  // Where jumps in a chat came from -- a reply's quote, a link to a
  // message: "↓" goes back to each in turn, the last first, before it goes
  // to the newest, as Telegram's. And the chats jumps went out of, to a
  // message in another: the button over "↓" goes back to the last.
  std::map<conversation_id, std::vector<std::string>> returns;
  std::vector<conversation_id> chat_returns;
  // The ways back that are where a jump goes: done with, as tdesktop's
  // skipReplyReturn -- from the top, while they are that message.
  void skip_return(const std::string& id) {
    if (!chosen)
      return;
    if (const auto found = returns.find(*chosen); found != returns.end())
      while (!found->second.empty() && found->second.back() == id)
        found->second.pop_back();
  }
  // The message pressed for the jump about to go, where one was: come back
  // to, rather than to where the view is.
  std::optional<std::string> return_from;
  // A jump in this chat about to go: the message it went from, else where
  // the view is, to come back to.
  void note_return() {
    if (chosen)
      if (std::optional<std::string> here = std::exchange(return_from, std::nullopt).or_else([&] { return this->last_seen(); }))
        returns[*chosen].push_back(std::move(*here));
  }
  // A jump to another chat about to go: this one, to come back to.
  void note_chat_return() {
    if (chosen)
      chat_returns.push_back(*chosen);
  }
  [[nodiscard]] std::optional<conversation_id> take_chat_return() {
    if (chat_returns.empty())
      return std::nullopt;
    conversation_id back = chat_returns.back();
    chat_returns.pop_back();
    return back;
  }
  [[nodiscard]] bool has_return() const {
    if (!chosen)
      return false;
    const auto found = returns.find(*chosen);
    return found != returns.end() && !found->second.empty();
  }
  // Back to where the last jump in this chat came from, where there is
  // one: as any jump, flashed where it lands.
  bool go_back() {
    if (!this->has_return())
      return false;
    auto& stack = returns[*chosen];
    std::string id = std::move(stack.back());
    stack.pop_back();
    // As a jump there goes: the same way, in the middle and flashed.
    this->jump_to(std::move(id));
    return true;
  }
  // Back to the newest, and nothing unseen. A jump still on its way, or a
  // message still aimed at -- one jumped to a moment ago, held in view while
  // what is above it settles -- let go first: it pulled the view back to
  // itself, and the button had to be pressed twice.
  void jump_to_end() {
    this->stop_jump();
    aiming.reset();
    jump_fragment.reset();
    if (chosen)
      returns.erase(*chosen);
    // The stretch at the end made again, even where it reaches the end: one
    // grown up from there -- the reader high above -- slid on up as the view
    // left the top, dropped its end to stay within its most, and the glide
    // stopped short of the newest: pressed twice.
    if (last_model) {
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
  // What the server was last asked for where nothing joined matched, and
  // what came of it.
  std::string asked_elsewhere;
  std::vector<directory_room> rooms_elsewhere;
  std::vector<found_person> people_elsewhere;
  bool rooms_came = false;
  bool people_came = false;
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
    friend bool operator==(const made_range&, const made_range&) = default;
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
    made_range next{from < all.size() ? std::optional<std::string>(all[from].id) : std::nullopt,
                    to > 0 ? std::optional<std::string>(all[to - 1].id) : std::nullopt, to == all.size()};
    if (next != made)
      needs_.shared->pictures_due = true;
    made = std::move(next);
  }
  // A message to bring into view, once it is made and laid out -- flashed,
  // unless it is where a chat opened, at what it was read up to.
  std::optional<std::string> jumping_to;
  // How long a jump waits on a window around its message before paging
  // back to it, and on anything at all before it is given up.
  static constexpr double kContextPatienceMs = 4000.0;
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
  // When the jump last got anywhere -- asked for, a window or a page asked,
  // more of the chat come -- and how much of it was held then: it waits and
  // is given up by that, in time, not in frames. Ten seconds of frames from
  // its start gave up on a message a few pages back while they came, and a
  // window in the background -- drawn seldom -- waited ten times as long.
  double jump_since_ms = -1.0;
  std::size_t jump_held = 0;
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
    for (const message_bubble<Actions>& row : std::get<0>(std::get<0>(timeline.fChildren).fChildren)) {
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
    jump_since_ms = -1.0;
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
        if (const message* said = held_message(*one, id)) {
          shown.line = said->body.plain.empty() && said->attachment ? std::string("Photo") : said->body.plain;
          std::ranges::replace(shown.line, '\n', ' ');
        }
        bar.setVisible(true);
        bar.show(shown, [this](const pinned_view& view) { return pinned_bar<pinned_press>(*needs_.colours, {this, view.id}, view); });
      }
    }
  }
  // The pin the bar shows, as Telegram's: the newest pinned above the view
  // -- before the first message seen -- else the oldest. A pin not loaded
  // is taken as older than all that is.
  [[nodiscard]] std::size_t pin_above(const conversation& one) {
    const skia::SkRect view = timeline.bounds();
    const auto& entries = std::get<0>(std::get<0>(timeline.fChildren).fChildren);
    const auto first = std::ranges::find_if(entries, [&](const message_bubble<Actions>& row) {
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
  // Away from the newest: scrolled up, in a window of the history, or with
  // a way back from a jump to go.
  [[nodiscard]] bool away() const {
    const conversation* shown_one = chosen && last_model ? last_model->find(*chosen) : nullptr;
    return !timeline.atEnd(40.0f) || (shown_one && shown_one->detached) || this->has_return();
  }
  // Ticked too while "↓" says otherwise than where the view is: the scroll
  // that brought the view to the end stopped in a frame whose tick had gone
  // by here already, and nothing ticked this again -- the arrow stayed.
  [[nodiscard]] bool wantsTick() const {
    return slide_wait > 0 || list_in.moving() || pane_in.moving() || needs_.shared->docked_panel_height != docked_applied ||
           needs_.paint->ease.t.moving() ||
           this->away() != chat.area.parts.jump.visible() ||
           this->older_due() || this->history_pending() ||
           jumping_to.has_value() || aiming.has_value() || jump_age != 0 || timeline.moving() ||
           !rooms_waiting.empty() || !rooms_unfound.empty();
  }
  void update(double now_ms);

  // The screen as the model is: the list, and the chat shown -- or, where
  // only what the list lists changed (another space, a forum, the search),
  // the list alone: the chat's messages were reconciled again for nothing.
  void show(const model& now, bool with_chat = true);

  // What the chat's protocol says over the composer (proto::composer_banners):
  // Matrix's warning where the other is not verified, for one. Its banners
  // one under the other, the bar in the first's tone.
  void place_head_view(std::nullopt_t) {}
  template <class View>
  void place_head_view(std::optional<View> made) {
    if (made)
      chat.parts.their_head.emplace(std::move(*made));
  }
  void place_composer_view(std::nullopt_t) {}
  template <class View>
  void place_composer_view(std::optional<View> made) {
    if (made)
      chat.parts.their_view.emplace(std::move(*made));
  }
  void show_banners(const conversation* one, const model& now);
  void show_conversation(const model& now);
};

}  // namespace mux::ui
