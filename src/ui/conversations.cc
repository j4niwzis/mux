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
// The direct chats: one person each.
struct direct {
  friend bool operator==(direct, direct) = default;
};
}  // namespace folder
using folder_t = splice::variant<folder::all, folder::space, folder::group, folder::direct>;
// The space a folder is, where it is one.
[[nodiscard]] inline std::optional<std::string> space_of(const folder_t& one) {
  return splice::visit(splice::overloaded{[](const folder::space& s) { return std::optional<std::string>(s.room); },
                                          [](const auto&) { return std::optional<std::string>(); }},
                       one);
}

// An item of a space bar: Home, Direct messages, or a space -- round, its
// picture or its mark, ringed in the accent while its chats are the ones
// listed. Pressed, they are.
template <class Pick>
struct space_icon : nodes::Stack {
  Pick pick;
  folder_t which;
  config::space_item_t item;
  config::space_bar_t bar;
  std::string name;
  std::string key;  // its picture's
  float diameter = 0.0f;
  struct parts_t {
    std::optional<avatar_mark> face;
    std::optional<nodes::Text> mark;
  } parts;
  space_icon(config::space_item_t what, folder_t shows, config::space_bar_t in, std::string id, std::string shown, bool chosen,
             float size, Pick act)
      : pick(std::move(act)), which(std::move(shows)), item(std::move(what)), bar(in), name(shown), key(id), diameter(size) {
    this->setHorizontal();
    fStack.justify = nodes::justify::middle{};
    fState.apply({.width = size, .height = size, .cornerRadius = size * 0.5f, .background = tile_colour,
                  .hoverBackground = chosen_colour,
                  .border = scene::Border{chosen ? accent_colour : skia::SkColor{0}, chosen ? 2.0f : 0.0f}});
    splice::visit(splice::overloaded{[&](config::space_item::home) { parts.mark.emplace("\u2302", size * 0.5f, text_colour); },
                                     [&](config::space_item::direct) { parts.mark.emplace("@", size * 0.45f, text_colour, true); },
                                     [&](const config::space_item::space&) { parts.face.emplace(id, shown, size - 6.0f); }},
                  item);
    if (parts.mark)
      parts.mark->apply({.alignSelf = scene::align::kMiddle});
    if (parts.face)
      parts.face->apply({.alignSelf = scene::align::kMiddle});
  }
  [[nodiscard]] bool acceptsInput() const { return true; }
  [[nodiscard]] bool hoverChangesAppearance() const { return true; }
  [[nodiscard]] bool onClick(float, float) {
    pick(which);
    return true;
  }
  // Ringed or not, as it is chosen or not: restyled where it is, not made
  // again -- every icon of both bars was, at every space chosen.
  void set_chosen(bool on) {
    fState.apply({.border = scene::Border{on ? accent_colour : skia::SkColor{0}, on ? 2.0f : 0.0f}});
  }
};

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
  // Chosen or not: its colour and its line, where it is.
  void set_chosen(bool on) {
    if (on == chosen)
      return;
    chosen = on;
    parts.label.setColour(on ? accent_colour : dim_colour);
    parts.underline.setVisible(on);
    this->markDamaged();
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
  [[nodiscard]] bool is_forum(const conversation_id& id) const {
    const conversation* one = last_model ? last_model->find(id) : nullptr;
    return one && one->space && forums.contains(id);
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
      rows.emplace_back(actions, one);
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
  // A space bar: its items, in a line.
  using icons_t = nodes::Flow<std::vector<space_icon<pick_folder>>>;
  // The top bar's view: its line of items, moved along by the wheel where
  // it is longer than the view -- cut to it.
  struct top_view : nodes::Stack {
    float offset = 0.0f;
    struct parts_t {
      icons_t line{{.direction = nodes::direction::horizontal{}, .spacingX = 4.0f, .wrap = false}, {}};
    } parts;
    top_view() {
      this->setHorizontal();
      parts.line.apply({.fillY = true, .autoSize = scene::axes::kX});
    }
    [[nodiscard]] float most() const {
      return std::max(0.0f, parts.line.bounds().width() - fState.contentBox().width());
    }
    void scroll_by(float delta) {
      const float to = std::clamp(offset + delta, 0.0f, this->most());
      if (to == offset)
        return;
      offset = to;
      parts.line.apply({.shiftX = -offset});
      this->markDamaged();
    }
    using Node::onPointer;
    void onPointer(scene::phase::bubble, const scene::pointer::scroll& wheel, scene::PointerReply& reply) {
      if (this->most() <= 0.0f)
        return;
      this->scroll_by(-(wheel.dx != 0.0f ? wheel.dx : wheel.dy) * 40.0f);
      reply.handle();
    }
    void onPointer(scene::phase::target, const scene::pointer::scroll& wheel, scene::PointerReply& reply) {
      if (this->most() <= 0.0f)
        return;
      this->scroll_by(-(wheel.dx != 0.0f ? wheel.dx : wheel.dy) * 40.0f);
      reply.handle();
    }
    [[nodiscard]] bool acceptsInput() const { return true; }
  };
  struct side_column : nodes::Stack {
    float wanted = 300.0f;
    struct head_row : nodes::Stack {
      using explore_button = icon_button<ask<Actions, &Actions::open_explore>>;
      struct parts_t {
        menu_button<Actions> menu;
        nodes::Text name{"mux", 17.0f, text_colour, true};
        // The top bar of spaces, after the name: there, empty or not, unless
        // the settings say otherwise -- something can always be put in it.
        // Longer than its room, it scrolls sideways.
        top_view top;
        // Explore rooms, out of the new chat's box: beside the chats, as
        // Element's compass is.
        explore_button explore;
      } parts;
      explicit head_row(Actions* a) : parts{.menu = menu_button<Actions>(a), .explore = explore_button(icon::compass{}, {a})} {
        this->setHorizontal();
        this->setGap(10.0f);
        fState.apply({.fillX = true, .height = 52.0f, .padding = {8.0f, 8.0f, 8.0f, 8.0f}});
        parts.name.apply({.alignSelf = scene::align::kMiddle});
        parts.top.apply({.height = 34.0f, .grow = scene::axes::kX, .alignSelf = scene::align::kMiddle, .cornerRadius = 8.0f,
                         .masking = true});
        parts.explore.apply({.alignSelf = scene::align::kMiddle});
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
    // What is right of the side bar: the search, the tabs, the chats.
    // A forum open: its name, and the way back to the chats.
    struct forum_head_t : nodes::Stack {
      struct parts_t {
        icon_button<ask<Actions, &Actions::close_forum>> back;
        nodes::Text name{"", 15.0f, text_colour, true};
        // Its settings: it is in no bar, to be right-pressed.
        icon_button<ask<Actions, &Actions::manage_forum>> settings;
      } parts;
      explicit forum_head_t(Actions* a)
          : parts{.back = icon_button<ask<Actions, &Actions::close_forum>>(icon::back{}, {a}),
                  .settings = icon_button<ask<Actions, &Actions::manage_forum>>(icon::gear{}, {a})} {
        this->setHorizontal();
        this->setGap(8.0f);
        fState.apply({.fillX = true, .height = 40.0f, .padding = {0.0f, 8.0f, 0.0f, 8.0f}});
        parts.back.apply({.alignSelf = scene::align::kMiddle});
        parts.name.setElided(true);
        parts.name.apply({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
        parts.settings.apply({.alignSelf = scene::align::kMiddle});
      }
    };
    // A message found, in the list of them: who, when, and its words.
    struct pick_found {
      Actions* actions;
      std::size_t index;
      void operator()() const { actions->search_pick(index); }
    };
    struct found_row : nodes::Stack {
      pick_found pick;
      struct lines_t : nodes::Stack {
        struct top_t : name_time_line {
          top_t(std::string name, std::string when)
              : name_time_line(std::move(name), std::move(when), text_colour, dim_colour, 12.0f) {}
        };
        struct parts_t {
          top_t top;
          nodes::Text text;
        } parts;
        lines_t(const search_result& one)
            : parts{.top = top_t(one.name, std::format("{:%d.%m.%y}", std::chrono::floor<std::chrono::days>(one.at))),
                    .text = nodes::Text(one.snippet, 13.0f, dim_colour)} {
          this->setGap(4.0f);
          fState.apply({.autoSize = scene::axes::kY, .grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
          parts.text.setElided(true);
          parts.text.apply({.fillX = true});
        }
      };
      struct parts_t {
        avatar_mark face;
        lines_t lines;
      } parts;
      found_row(Actions* a, const search_result& one)
          : pick{a, one.index}, parts{.face = avatar_mark(one.sender, one.name, 40.0f), .lines = lines_t(one)} {
        this->setHorizontal();
        this->setGap(10.0f);
        fState.apply({.fillX = true, .height = 56.0f, .padding = {0.0f, 12.0f, 0.0f, 10.0f}, .hoverBackground = chosen_colour});
        parts.face.apply({.alignSelf = scene::align::kMiddle});
      }
      [[nodiscard]] bool acceptsInput() const { return true; }
      [[nodiscard]] bool hoverChangesAppearance() const { return true; }
      [[nodiscard]] bool onClick(float, float) {
        pick();
        return true;
      }
    };
    using found_list_t = nodes::ScrollContainer<nodes::Flow<std::vector<found_row>>>;
    struct rest_t : nodes::Stack {
      struct parts_t {
        forum_head_t forum_head;
        search_box search;
        // The folders, where the account has groups -- or spaces, with no
        // bars: a line of tabs.
        nodes::Flow<std::vector<folder_tab<pick_folder>>> folders{
            {.direction = nodes::direction::horizontal{}, .spacingX = 2.0f, .spacingY = 2.0f}, {}};
        nodes::Text no_chats{"No chats yet.", 13.0f, dim_colour};
        list_t list{nodes::Flow<std::vector<conversation_row<Actions>>>({.spacingY = 0.0f, .wrap = false}, {})};
        // While a chat is searched: what was found, in the chats' place.
        nodes::Text found_title{"", 13.0f, dim_colour, true};
        found_list_t found{nodes::Flow<std::vector<found_row>>({.spacingY = 0.0f, .wrap = false}, {})};
      } parts;
      explicit rest_t(Actions* a) : parts{.forum_head = forum_head_t(a)} {
        parts.found_title.apply({.margin = {4.0f, 16.0f, 6.0f, 16.0f}});
        parts.found_title.setVisible(false);
        parts.found.apply({.fillX = true, .grow = scene::axes::kY});
        std::get<0>(parts.found.fChildren).apply({.fillX = true, .autoSize = scene::axes::kY});
        parts.found.setVisible(false);
        fState.apply({.fillY = true, .grow = scene::axes::kX});
        parts.forum_head.setVisible(false);
        parts.no_chats.apply({.margin = {12.0f, 16.0f, 0.0f, 16.0f}});
        parts.folders.apply({.fillX = true, .autoSize = scene::axes::kY, .margin = {0.0f, 8.0f, 6.0f, 8.0f}});
        parts.list.apply({.fillX = true, .grow = scene::axes::kY});
        std::get<0>(parts.list.fChildren).apply({.fillX = true, .autoSize = scene::axes::kY});
      }
    };
    // Under the head: the side bar of spaces, under the menu's button and
    // down to the window's bottom -- drawn where it holds any -- and the rest.
    struct body_t : nodes::Stack {
      struct parts_t {
        // Longer than the window, it scrolls.
        nodes::ScrollContainer<icons_t> side{icons_t({.spacingY = 8.0f, .wrap = false, .crossAlign = scene::align::kMiddle}, {})};
        rest_t rest;
      } parts;
      explicit body_t(Actions* a) : parts{.rest = rest_t(a)} {
        this->setHorizontal();
        fState.apply({.fillX = true, .grow = scene::axes::kY});
        parts.side.apply({.fillY = true, .width = 56.0f});
        std::get<0>(parts.side.fChildren).apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {4.0f, 6.0f, 8.0f, 6.0f}});
      }
    };
    // What a right press on an item offers: the bars it is in, or hidden.
    struct set_bars_act {
      Actions* actions;
      std::string account;
      config::space_item_t item;
      bool side = true, top = false;
      void operator()() const { actions->set_space_bars(account, item, side, top); }
    };
    struct explore_act {
      Actions* actions;
      std::string room;
      void operator()() const { actions->explore_space(room); }
    };
    struct manage_act {
      Actions* actions;
      std::string room;
      void operator()() const { actions->manage_space(room); }
    };
    // The column's menus' look: a card over the rest, 190 wide.
    static void as_popup(nodes::Stack& menu) {
      menu.setGap(4.0f);
      menu.fState.apply({.width = 190.0f, .autoSize = scene::axes::kY, .padding = {8.0f, 8.0f, 8.0f, 8.0f}, .cornerRadius = 10.0f,
                         .background = popup_colour(), .border = scene::Border{band_colour, 1.0f},
                         .shadow = scene::Shadow{skia::colorSetARGB(70, 0, 0, 0), 3.0f}});
    }
    // A chat's: its settings.
    struct chat_settings_act {
      Actions* actions;
      conversation_id id;
      void operator()() const {
        actions->choose(id);
        actions->open_manage();
      }
    };
    // Listed in another account's list too, or moved there.
    struct place_act {
      Actions* actions;
      conversation_id chat;
      account_id to;
      bool moved;
      void operator()() const { actions->place_chat(chat, to, moved); }
    };
    // Out of this list, where it is another account's: back to its own
    // where it was moved.
    struct unplace_act {
      Actions* actions;
      conversation_id chat;
      account_id from;
      void operator()() const { actions->unplace_chat(chat, from); }
    };
    struct strip_act {
      Actions* actions;
      conversation_id chat;
      account_id in;
      void operator()() const { actions->flip_chat_strip(chat, in); }
    };
    struct strip_colour_act {
      Actions* actions;
      conversation_id chat;
      account_id in;
      void operator()(const config::accent_t& colour) const { actions->set_chat_strip_colour(chat, in, colour); }
    };
    struct chat_menu : nodes::Stack {
      struct parts_t {
        nodes::Text title;
        widgets::Button<chat_settings_act> settings;
        std::vector<widgets::Button<place_act>> places;
        std::optional<widgets::Button<unplace_act>> unplace;
        std::optional<widgets::Button<strip_act>> strip;
        std::optional<accent_circles<strip_colour_act>> strip_colours;
      } parts;
      // Its places: Copy to and Move to each other account. Listed here from
      // another, its way out of this list, and its strip.
      chat_menu(Actions* a, conversation_id id, std::string name, const account_id& listing,
                const std::vector<account_id>& accounts, const config::theme_t& theme)
          : parts{.title = nodes::Text(std::move(name), 13.0f, dim_colour, true),
                  .settings = widgets::Button<chat_settings_act>("Chat settings\u2026", {a, id})} {
        as_popup(*this);
        fState.apply({.width = 320.0f});
        parts.title.setElided(true);
        parts.title.apply({.fillX = true});
        parts.settings.apply({.fillX = true, .height = 30.0f});
        for (const account_id& to : accounts)
          if (to != id.account && to != listing) {
            parts.places.emplace_back(std::format("Copy to {}", to.address), place_act{a, id, to, false});
            parts.places.emplace_back(std::format("Move to {}", to.address), place_act{a, id, to, true});
          }
        if (id.account != listing) {
          parts.unplace.emplace("Remove from this list", unplace_act{a, id, listing});
          parts.strip.emplace("Strip on or off", strip_act{a, id, listing});
          parts.strip_colours.emplace(strip_colour_act{a, id, listing}, theme, false);
        }
        for (auto& each : parts.places)
          each.apply({.fillX = true, .height = 30.0f});
        if (parts.unplace)
          parts.unplace->apply({.fillX = true, .height = 30.0f});
        if (parts.strip)
          parts.strip->apply({.fillX = true, .height = 30.0f});
        if (parts.strip_colours)
          parts.strip_colours->apply({.margin = {4.0f, 0.0f, 0.0f, 0.0f}});
      }
    };
    struct space_menu : nodes::Stack {
      struct parts_t {
        nodes::Text title;
        widgets::Button<explore_act> explore;
        widgets::Button<manage_act> manage;
        widgets::Button<set_bars_act> side, top, both, hide;
      } parts;
      [[nodiscard]] static std::string room_of(const config::space_item_t& item) {
        return splice::visit(splice::overloaded{[](const config::space_item::space& s) { return s.room; },
                                                [](const auto&) { return std::string(); }},
                             item);
      }
      space_menu(Actions* a, const std::string& account, const config::space_item_t& item, std::string name)
          : parts{.title = nodes::Text(std::move(name), 13.0f, dim_colour, true),
                  .explore = widgets::Button<explore_act>("Explore its rooms\u2026", {a, room_of(item)}),
                  .manage = widgets::Button<manage_act>("Space settings\u2026", {a, room_of(item)}),
                  .side = widgets::Button<set_bars_act>("Side bar only", {a, account, item, true, false}),
                  .top = widgets::Button<set_bars_act>("Top bar only", {a, account, item, false, true}),
                  .both = widgets::Button<set_bars_act>("Both bars", {a, account, item, true, true}),
                  .hide = widgets::Button<set_bars_act>("Hide", {a, account, item, false, false})} {
        as_popup(*this);
        parts.title.setElided(true);
        parts.title.apply({.fillX = true});
        for (scene::Node* each : std::initializer_list<scene::Node*>{&parts.explore, &parts.manage, &parts.side, &parts.top, &parts.both, &parts.hide})
          each->apply({.fillX = true, .height = 30.0f});
        parts.explore.setVisible(!room_of(item).empty());
        parts.manage.setVisible(!room_of(item).empty());
      }
    };
    struct parts_t {
      head_row head;
      body_t body;
      // A right press's menu, over the rest: a space's, or a chat's.
      std::optional<space_menu> menu;
      std::optional<chat_menu> row_menu;
      // The item dragged: picked up off its place, it goes with the pointer;
      // let go, it flies to where it goes -- or back.
      std::optional<space_icon<pick_folder>> ghost;
    } parts;
    // Its parts by their names, for what reads them: it is never moved.
    head_row& head = parts.head;
    search_box& search = parts.body.parts.rest.parts.search;
    forum_head_t& forum_head = parts.body.parts.rest.parts.forum_head;
    nodes::Text& found_title = parts.body.parts.rest.parts.found_title;
    found_list_t& found = parts.body.parts.rest.parts.found;
    nodes::Flow<std::vector<folder_tab<pick_folder>>>& folders = parts.body.parts.rest.parts.folders;
    nodes::Text& no_chats = parts.body.parts.rest.parts.no_chats;
    list_t& list = parts.body.parts.rest.parts.list;
    // The bars as they show -- what is hidden, lit, a drop's place -- and the
    // lines of items in them.
    top_view& top_bar = parts.head.parts.top;
    nodes::ScrollContainer<icons_t>& side_bar = parts.body.parts.side;
    icons_t& top_line = parts.head.parts.top.parts.line;
    icons_t& side_line = std::get<0>(parts.body.parts.side.fChildren);
    Actions* actions = nullptr;
    // Whose spaces the bars hold, as the screen says as it shows them.
    std::string account;
    explicit side_column(Actions* a) : parts{.head = head_row(a), .body = body_t(a)}, actions(a) {
      fState.apply({.fillY = true, .background = sidebar_colour});
    }

    // ---- an item dragged from a bar to the other, or along one ------------
    struct drag_t {
      config::space_item_t item;
      config::space_bar_t from;
      float x0 = 0.0f, y0 = 0.0f;
      bool moving = false;
      std::chrono::steady_clock::time_point pressed = std::chrono::steady_clock::now();
      // A quick move along the top bar: it scrolled, by the pointer.
      bool scrolling = false;
      float last_x = 0.0f;
    };
    std::optional<drag_t> drag;
    bool menu_close_due = false;
    // For the chat menu: the account whose list this is, every account (its
    // places), and the theme its strip colours are chosen in.
    std::optional<account_id> current_account;
    std::vector<account_id> accounts_known;
    config::theme_t theme_now = config::theme::tinted{};
    // The flight of the one let go, from where it was let go to its place;
    // and the item whose icon shows once it is there.
    skiff::paint::Tween fly{0.0f, 180.0f};
    skia::SkPoint fly_from{}, fly_to{};
    std::optional<config::space_item_t> landing;
    [[nodiscard]] space_icon<pick_folder>* icon_of(const config::space_item_t& item, const config::space_bar_t& bar) {
      icons_t& line = splice::visit(splice::overloaded{[&](config::space_bar::top) -> icons_t& { return top_line; },
                                                       [&](const auto&) -> icons_t& { return side_line; }},
                                    bar);
      const auto found = std::ranges::find(icons_of(line), item, &space_icon<pick_folder>::item);
      return found == icons_of(line).end() ? nullptr : &*found;
    }
    // The one dragged, its middle at a point: drawn moved there, not laid out.
    void ghost_at(float x, float y) {
      if (!parts.ghost)
        return;
      const skia::SkRect box = fState.fBounds;
      const float half = parts.ghost->diameter * 0.5f;
      parts.ghost->apply({.shiftX = x - box.fLeft - half, .shiftY = y - box.fTop - half});
    }
    void lift(const drag_t& from, float x, float y) {
      space_icon<pick_folder>* one = this->icon_of(from.item, from.from);
      if (!one)
        return;
      parts.ghost.emplace(one->item, one->which, one->bar, one->key, one->name, false, one->diameter, one->pick);
      parts.ghost->apply({.place = scene::anchor::kTopLeft, .x = 0.0f, .y = 0.0f, .alpha = 0.9f});
      // Off its place: the others close up.
      one->setVisible(false);
      this->ghost_at(x, y);
      this->invalidateLayout();
    }
    void let_go(const config::space_item_t& item, skia::SkPoint from, skia::SkPoint to) {
      landing = item;
      fly_from = from;
      fly_to = to;
      fly.jump(0.0f);
      fly.setTarget(1.0f);
      scene::work::mark(fState.fId);
    }
    // There: its icon shown where it went, the one flying gone.
    void land() {
      if (landing)
        for (icons_t* line : {&top_line, &side_line})
          for (auto& one : icons_of(*line))
            if (one.item == *landing) {
              one.setVisible(true);
              one.fState.setAlpha(1.0f);
              one.markDamaged();
            }
      landing.reset();
      parts.ghost.reset();
      this->invalidateLayout();
      this->markDamaged();
    }
    [[nodiscard]] std::vector<space_icon<pick_folder>>& icons_of(icons_t& bar) { return std::get<0>(bar.fChildren); }
    [[nodiscard]] skia::SkRect shown_at(const space_icon<pick_folder>& one) const {
      return splice::visit(splice::overloaded{[&](config::space_bar::top) { return one.bounds().makeOffset(-top_bar.offset, 0.0f); },
                                              [&](const auto&) { return side_bar.toView(one.bounds()); }},
                           one.bar);
    }
    [[nodiscard]] const space_icon<pick_folder>* icon_at(float x, float y) {
      if (top_bar.visible())
        for (const auto& one : icons_of(top_line))
          if (this->shown_at(one).contains(x, y))
            return &one;
      if (side_bar.visible())
        for (const auto& one : icons_of(side_line))
          if (this->shown_at(one).contains(x, y))
            return &one;
      return nullptr;
    }
    // The bar a point is over: the head (the top bar, where there is one),
    // or the side bar's strip -- shown while something is dragged.
    [[nodiscard]] std::optional<config::space_bar_t> bar_at(float x, float y) const {
      if (top_bar.visible() && parts.head.bounds().contains(x, y))
        return config::space_bar::top{};
      if (side_bar.visible() && side_bar.bounds().contains(x, y))
        return config::space_bar::side{};
      return std::nullopt;
    }
    // A bar's items with the one dragged put where it is let go: before the
    // first whose middle is past the point, along the bar.
    [[nodiscard]] std::vector<config::space_item_t> order_with(const config::space_bar_t& bar, const config::space_item_t& item,
                                                               float x, float y) {
      const bool along_x = splice::visit(splice::overloaded{[](config::space_bar::top) { return true; }, [](const auto&) { return false; }}, bar);
      icons_t& icons = along_x ? top_line : side_line;
      std::vector<config::space_item_t> out = icons_of(icons) |
                                              std::views::filter([&](const auto& one) { return one.item != item; }) |
                                              std::views::transform([](const auto& one) { return one.item; }) |
                                              std::ranges::to<std::vector>();
      const auto before = std::ranges::count_if(icons_of(icons), [&](const auto& one) {
        const skia::SkRect at = this->shown_at(one);
        return one.item != item && (along_x ? at.centerX() < x : at.centerY() < y);
      });
      out.insert(out.begin() + before, item);
      return out;
    }
    void show_drop_targets(bool on) {
      if (on)
        side_bar.setVisible(true);
      this->invalidateLayout();
      this->markDamaged();
    }
    void light(const std::optional<config::space_bar_t>& over) {
      const bool top = over && *over == config::space_bar_t{config::space_bar::top{}};
      const bool side = over && *over == config::space_bar_t{config::space_bar::side{}};
      top_bar.apply({.background = top ? chosen_colour : skia::SkColor{0}});
      side_bar.apply({.background = side ? chosen_colour : skia::SkColor{0}});
    }
    void close_menu() {
      if (!parts.menu && !parts.row_menu)
        return;
      parts.menu.reset();
      parts.row_menu.reset();
      this->invalidateLayout();
      this->markDamaged();
    }
    [[nodiscard]] bool menu_up() const { return parts.menu || parts.row_menu; }
    [[nodiscard]] bool menu_has(float x, float y) const {
      return (parts.menu && parts.menu->bounds().contains(x, y)) || (parts.row_menu && parts.row_menu->bounds().contains(x, y));
    }
    void drag_down(const scene::pointer::down& press, scene::PointerReply& reply) {
      // A press off the menu closes it at once -- nothing of it is pressed;
      // one on it chooses, and the program closes it then.
      if (this->menu_up() && !this->menu_has(press.x, press.y))
        this->close_menu();
      drag.reset();
      const space_icon<pick_folder>* one = this->icon_at(press.x, press.y);
      // A right press on a chat in the list: its menu, where it was pressed.
      if (!one && press.button == 3 && list.visible())
        for (const auto& row : std::get<0>(std::get<0>(list.fChildren).fChildren))
          if (list.toView(row.bounds()).contains(press.x, press.y)) {
            const skia::SkRect box = fState.fBounds;
            parts.row_menu.emplace(actions, row.id, row.parts.lines.parts.top.parts.name.text(),
                                   current_account ? *current_account : row.id.account, accounts_known, theme_now);
            parts.row_menu->apply({.place = scene::anchor::kTopLeft,
                                   .x = std::clamp(press.x - box.fLeft, 0.0f, std::max(0.0f, box.width() - 320.0f)),
                                   .y = std::clamp(press.y - box.fTop, 0.0f, std::max(0.0f, box.height() - 260.0f))});
            menu_close_due = false;
            this->invalidateLayout();
            reply.handle();
            return;
          }
      if (!one)
        return;
      // A right press: its menu, where it was pressed, kept in the column.
      if (press.button == 3) {
        const skia::SkRect box = fState.fBounds;
        parts.menu.emplace(actions, account, one->item, one->name);
        parts.menu->apply({.place = scene::anchor::kTopLeft,
                           .x = std::clamp(press.x - box.fLeft, 0.0f, std::max(0.0f, box.width() - 190.0f)),
                           .y = std::clamp(press.y - box.fTop, 0.0f, std::max(0.0f, box.height() - 180.0f))});
        menu_close_due = false;
        this->invalidateLayout();
        reply.handle();
        return;
      }
      drag = drag_t{one->item, one->bar, press.x, press.y};
    }
    void drag_move(const scene::pointer::move& at, scene::PointerReply& reply) {
      if (!drag)
        return;
      if (drag->scrolling) {
        top_bar.scroll_by(drag->last_x - at.x);
        drag->last_x = at.x;
        reply.handle();
        return;
      }
      if (!drag->moving) {
        const float dx = at.x - drag->x0, dy = at.y - drag->y0;
        if (std::abs(dx) < 6.0f && std::abs(dy) < 6.0f)
          return;
        // In the side bar, a quick move along it is a scroll -- a finger's or
        // a quick drag's -- left to the bar; one held a moment first, or one
        // out across it, carries the item.
        const bool along_side = splice::visit(splice::overloaded{[](config::space_bar::side) { return true; },
                                                                 [](const auto&) { return false; }},
                                              drag->from);
        const bool quick = std::chrono::steady_clock::now() - drag->pressed < std::chrono::milliseconds(250);
        if (along_side && std::abs(dy) > std::abs(dx) && quick) {
          drag.reset();
          return;
        }
        // Along the top bar, the same: it scrolls, the pointer held for it.
        if (!along_side && std::abs(dx) > std::abs(dy) && quick) {
          drag->scrolling = true;
          drag->last_x = at.x;
          top_bar.scroll_by(drag->x0 - at.x);
          reply.capturePointer();
          reply.handle();
          return;
        }
        drag->moving = true;
        reply.capturePointer();
        reply.suppressHover();
        this->show_drop_targets(true);
        this->lift(*drag, at.x, at.y);
      }
      this->ghost_at(at.x, at.y);
      this->light(this->bar_at(at.x, at.y));
      reply.handle();
    }
    void drag_up(const scene::pointer::up& at, scene::PointerReply& reply) {
      if (!drag)
        return;
      const drag_t was = *std::exchange(drag, std::nullopt);
      if (was.scrolling) {
        reply.releasePointer();
        reply.handle();
        return;
      }
      if (!was.moving)
        return;
      reply.releasePointer();
      reply.handle();
      this->light(std::nullopt);
      // To where it goes: the place of the one it now comes before, or past
      // the last -- or back where it was.
      const auto bar = this->bar_at(at.x, at.y);
      const config::space_bar_t to_bar = bar ? *bar : was.from;
      const bool along_x = splice::visit(splice::overloaded{[](config::space_bar::top) { return true; }, [](const auto&) { return false; }}, to_bar);
      const std::vector<config::space_item_t> order = this->order_with(to_bar, was.item, at.x, at.y);
      const auto index = static_cast<std::size_t>(std::ranges::find(order, was.item) - order.begin());
      skia::SkPoint target{at.x, at.y};
      icons_t& line = along_x ? top_line : side_line;
      std::vector<skia::SkRect> others;
      for (const auto& one : icons_of(line))
        if (one.item != was.item && one.visible())
          others.push_back(this->shown_at(one));
      const float step = (parts.ghost ? parts.ghost->diameter : 40.0f) + (along_x ? 4.0f : 8.0f);
      if (index < others.size())
        target = {others[index].centerX(), others[index].centerY()};
      else if (!others.empty())
        target = along_x ? skia::SkPoint{others.back().centerX() + step, others.back().centerY()}
                         : skia::SkPoint{others.back().centerX(), others.back().centerY() + step};
      else
        target = along_x ? skia::SkPoint{top_bar.bounds().fLeft + step * 0.5f, top_bar.bounds().centerY()}
                         : skia::SkPoint{side_bar.bounds().centerX(), side_bar.bounds().fTop + step * 0.5f};
      if (bar)
        actions->place_spaces(account, *bar, order, was.from, was.item);
      this->let_go(was.item, {at.x, at.y}, target);
      this->show_drop_targets(false);
    }
    using Node::onPointer;
    void onPointer(scene::phase::capture, const scene::pointer::down& press, scene::PointerReply& reply) { drag_down(press, reply); }
    void onPointer(scene::phase::capture, const scene::pointer::move& at, scene::PointerReply& reply) { drag_move(at, reply); }
    void onPointer(scene::phase::target, const scene::pointer::move& at, scene::PointerReply& reply) { drag_move(at, reply); }
    void onPointer(scene::phase::capture, const scene::pointer::up& at, scene::PointerReply& reply) { drag_up(at, reply); }
    void onPointer(scene::phase::target, const scene::pointer::up& at, scene::PointerReply& reply) { drag_up(at, reply); }
    // The menu closed after the press that chose from it, or one off it:
    // not from inside that press's handling.
    [[nodiscard]] bool wantsTick() const { return menu_close_due || fly.moving() || (parts.ghost && !drag); }
    // Frames asked for while a bar flies back to its place: ticked only, it
    // moved at the frames something else asked for.
    [[nodiscard]] bool settling() const { return fly.moving() || (parts.ghost && !drag); }
    void update(double now) {
      if (parts.ghost && !drag) {
        fly.step(now);
        const float t = fly.value();
        this->ghost_at(fly_from.x() + (fly_to.x() - fly_from.x()) * t, fly_from.y() + (fly_to.y() - fly_from.y()) * t);
        if (!fly.moving())
          this->land();
      }
      if (std::exchange(menu_close_due, false) && parts.menu) {
        parts.menu.reset();
        this->invalidateLayout();
        this->markDamaged();
      }
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
      // A plain colour: the wallpaper is the messages' own -- the timeline's
      // Wallpaper -- not behind Select a chat, where Telegram has none.
      // Nothing, where the background is behind the whole window.
      fState.apply({.fillY = true, .grow = scene::axes::kX,
                    .background = window_look().behind ? skia::SkColor{0} : chat_colour});
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
  void onKey(scene::phase::bubble, const scene::key::down& press, scene::Reply& reply) {
    namespace keys = scene::keys;
    if (press.key == keys::kEscape && this->close_space_menu()) {
      reply.handle();
      return;
    }
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
    if (!chosen && !pointed)
      return;
    // Only a forum gone to: Alt+Up and Alt+Down go on from it, nothing else.
    if (!chosen && !((press.key == keys::kUp || press.key == keys::kDown) && press.modifiers.template has<scene::modifier::alt>()))
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
    } else if (press.key == keys::kEscape && !any && parts.threads.answering) {
      parts.threads.stop_answering();
    } else if (press.key == keys::kEscape && !any && line.answering()) {
      actions->cancel_compose();
    } else if ((press.key == keys::kTab && control) ||
               ((press.key == keys::kUp || press.key == keys::kDown) && press.modifiers.template has<scene::modifier::alt>())) {
      // To the next chat in the list, or the one before: Ctrl+Tab and
      // Ctrl+Shift+Tab, Alt+Down and Alt+Up.
      const bool back = press.key == keys::kUp || press.modifiers.template has<scene::modifier::shift>();
      const auto& rows = std::get<0>(std::get<0>(list.fChildren).fChildren);
      // From the forum gone to, where one is; else from the chat open.
      const auto at = std::ranges::find(rows, pointed ? *pointed : *chosen, &conversation_row<Actions>::id);
      if (at == rows.end() || rows.empty())
        return;
      const auto index = static_cast<std::size_t>(at - rows.begin());
      const std::size_t to = back ? (index == 0 ? rows.size() - 1 : index - 1) : (index + 1) % rows.size();
      // A forum: gone to, lit, not opened -- Alt+Right opens it. A chat: opened.
      if (is_forum(rows[to].id)) {
        pointed = rows[to].id;
        if (last_model)
          this->show(*last_model, false);
      } else {
        pointed.reset();
        actions->choose(rows[to].id);
      }
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
  // Whether the timeline last shown was cut off from the newest: what then
  // comes after it is a page of what is older than the newest, not new.
  bool shown_detached = false;
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
              .info = info_panel<Actions>(a),
              .threads = threads_panel<Actions>(a)} {
    fState.apply({.fill = true});
    this->setHorizontal();
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
  void show_space_bars(const model& now) {
    struct entry {
      config::space_item_t item;
      folder_t shows;
      std::string id;
      std::string name;
    };
    std::vector<entry> all{{config::space_item::home{}, folder::all{}, "", "Home"},
                           {config::space_item::direct{}, folder::direct{}, "", "Direct messages"}};
    const std::string address = current ? current->address : std::string();
    // Spaces in spaces: each under the first space found holding it. Only
    // those in none are items of the bars; the others show under theirs
    // while it -- or one in it, at any depth -- is the one chosen.
    std::map<std::string, std::string> parent_of;
    std::map<std::string, std::vector<const conversation*>> spaces_in;
    const auto* chats = current ? &now.accounts().at(*current).conversations : nullptr;
    if (chats)
      for (const auto& [key, one] : *chats)
        if (one.space)
          for (const std::string& child : one.children)
            if (const auto found = chats->find(child);
                found != chats->end() && found->second.space && !forums.contains(found->second.id) && child != one.id.id &&
                !parent_of.contains(child)) {
              parent_of.emplace(child, one.id.id);
              spaces_in[one.id.id].push_back(&found->second);
            }
    std::set<std::string> open;
    if (const std::optional<std::string> chosen_room = space_of(folder))
      for (std::string at = *chosen_room; open.insert(at).second;) {
        const auto up = parent_of.find(at);
        if (up == parent_of.end())
          break;
        at = up->second;
      }
    if (chats)
      for (const auto& [key, one] : *chats)
        // A space shown as a forum is a chat in the list, not an item of a bar.
        if (one.space && !parent_of.contains(one.id.id) && !forums.contains(one.id))
          all.push_back({config::space_item::space{one.id.id}, folder::space{one.id.id}, one.id.id, display_name(one)});
    // A bar's icons: its items, and under an open space its own spaces --
    // smaller, a level at a time.
    struct shown_icon {
      const entry* top = nullptr;
      const conversation* sub = nullptr;
      int depth = 0;
    };
    std::vector<entry> subs_made;
    const auto expanded = [&](const std::vector<const entry*>& items) {
      std::vector<shown_icon> out;
      for (const entry* one : items) {
        out.push_back({one, nullptr, 0});
        const std::optional<std::string> room = splice::visit(
            splice::overloaded{[](const config::space_item::space& it) { return std::optional<std::string>(it.room); },
                               [](const auto&) { return std::optional<std::string>(); }},
            one->item);
        if (!room || !open.contains(*room))
          continue;
        std::vector<std::pair<const conversation*, int>> todo;
        const auto push_children = [&](const std::string& of, int depth) {
          if (const auto found = spaces_in.find(of); found != spaces_in.end())
            for (const conversation* sub : found->second | std::views::reverse)
              todo.emplace_back(sub, depth);
        };
        push_children(*room, 1);
        std::set<std::string> seen{*room};
        while (!todo.empty()) {
          const auto [sub, depth] = todo.back();
          todo.pop_back();
          if (!seen.insert(sub->id.id).second)
            continue;
          out.push_back({nullptr, sub, depth});
          if (open.contains(sub->id.id))
            push_children(sub->id.id, depth + 1);
        }
      }
      return out;
    };
    const std::vector<config::space_placed> mine = space_places |
                                                   std::views::filter([&](const config::space_placed& p) { return p.account == address; }) |
                                                   std::ranges::to<std::vector>();
    const auto in_bar = [&](const entry& one, const config::space_bar_t& bar) {
      const bool placed = std::ranges::any_of(mine, [&](const auto& p) { return p.item == one.item; });
      if (!placed)
        return bar == config::space_bar_t{config::space_bar::side{}};
      return std::ranges::any_of(mine, [&](const auto& p) { return p.item == one.item && p.bar == bar; });
    };
    const auto bar_of = [&](const config::space_bar_t& bar) {
      std::vector<std::pair<std::size_t, const entry*>> ranked;
      for (std::size_t i = 0; i < all.size(); ++i)
        if (in_bar(all[i], bar)) {
          const auto at = std::ranges::find_if(mine, [&](const auto& p) { return p.item == all[i].item && p.bar == bar; });
          ranked.emplace_back(at == mine.end() ? mine.size() + i : static_cast<std::size_t>(at - mine.begin()), &all[i]);
        }
      std::ranges::sort(ranked, {}, &std::pair<std::size_t, const entry*>::first);
      return ranked | std::views::values | std::ranges::to<std::vector>();
    };
    const std::vector<const entry*> side_items = bar_of(config::space_bar::side{});
    const std::vector<const entry*> top_items = bar_of(config::space_bar::top{});
    // For the settings to list them.
    space_account_now() = address;
    space_items_now() = all | std::views::transform([&](const entry& one) {
                          return space_item_shown{one.item, one.name, in_bar(one, config::space_bar::side{}),
                                                  in_bar(one, config::space_bar::top{})};
                        }) |
                        std::ranges::to<std::vector>();
    side.account = address;
    // Made again only where they changed.
    std::vector<std::string> made;
    const std::vector<shown_icon> side_shown = expanded(side_items);
    const std::vector<shown_icon> top_shown = expanded(top_items);
    for (const auto& [items, mark] : {std::pair{&side_shown, "s"}, std::pair{&top_shown, "t"}})
      for (const shown_icon& one : *items)
        // Joined, not std::format: clang 23 crashed instantiating these
        // format strings in the UI test's build (TemplateArgument::
        // isPackExpansion, in SubstType of the format_string).
        made.push_back(one.top ? std::string(mark) + '|' + std::string(config::word_of(one.top->item)) + '|' + one.top->name + '|' +
                                     (avatar_images().has(one.top->id) ? "1" : "0")
                               : std::string(mark) + '|' + one.sub->id.id + '|' + display_name(*one.sub) + '|' +
                                     std::to_string(one.depth) + '|' + (avatar_images().has(one.sub->id.id) ? "1" : "0"));
    made.push_back(std::string(spaces_on ? "1" : "0") + (top_bar_on ? "1" : "0"));
    if (made != shown_bars) {
      shown_bars = made;
      auto& side_icons = std::get<0>(side.side_line.fChildren);
      auto& top_icons = std::get<0>(side.top_line.fChildren);
      side_icons.clear();
      top_icons.clear();
      // A space in a space: smaller for each level down.
      const auto emit = [&](auto& icons, const std::vector<shown_icon>& shown, const config::space_bar_t& bar, float size) {
        for (const shown_icon& one : shown) {
          if (one.top) {
            icons.emplace_back(one.top->item, one.top->shows, bar, one.top->id, one.top->name, one.top->shows == folder, size,
                               pick_folder{this});
            continue;
          }
          const folder_t shows = folder::space{one.sub->id.id};
          icons.emplace_back(config::space_item::space{one.sub->id.id}, shows, bar, one.sub->id.id, display_name(*one.sub),
                             shows == folder, std::max(20.0f, size - 6.0f * static_cast<float>(one.depth)), pick_folder{this});
        }
      };
      emit(side_icons, side_shown, config::space_bar::side{}, 40.0f);
      emit(top_icons, top_shown, config::space_bar::top{}, 30.0f);
      side.side_line.invalidateLayout();
      side.top_line.invalidateLayout();
      if (side.landing)
        for (auto* icons : {&side_icons, &top_icons})
          for (auto& one : *icons)
            if (one.item == *side.landing)
              one.fState.setAlpha(0.0f);
      // A space come or gone: the bars painted again whole.
      side.side_bar.markDamaged();
      side.top_bar.markDamaged();
    }
    // Which is chosen: its ring, on the icons as they are.
    for (auto* icons : {&std::get<0>(side.side_line.fChildren), &std::get<0>(side.top_line.fChildren)})
      for (auto& one : *icons)
        one.set_chosen(one.which == folder);
    // The top bar there unless turned off; the side one where it holds any.
    side.top_bar.setVisible(spaces_on && top_bar_on);
    if (!side.drag || !side.drag->moving)
      side.side_bar.setVisible(spaces_on && !side_items.empty());
  }

  // The space menu closed by any press off it, wherever on the screen: at
  // once, where the press is not on it; one on it chooses first.
  using Node::onPointer;
  void onPointer(scene::phase::capture, const scene::pointer::down& press, scene::PointerReply&) {
    if (side.menu_up() && !side.menu_has(press.x, press.y))
      side.close_menu();
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
    info_edge.setVisible(shown);
    side.wanted = side_width;
    info.apply({.width = info_width});
    parts.threads.apply({.width = info_width});
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
           panel_ease().t.moving();
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
    return slide_wait > 0 || list_in.moving() || panel_ease().t.moving() || this->away() != chat.area.parts.jump.visible() ||
           this->older_due() || this->history_pending() ||
           jumping_to.has_value() || aiming.has_value() || jump_age != 0 || timeline.moving() ||
           !rooms_waiting.empty() || !rooms_unfound.empty();
  }
  void update(double now_ms) {
    if (slide_wait > 0 && --slide_wait == 0)
      list_in.setTarget(1.0f);
    if (list_in.step(now_ms))
      this->place_list();
    // The older asked long ago and not come: asked again.
    if (this->history_pending() && now_ms - history_asked_ms > kHistoryPatienceMs)
      history_asked.reset();
    // Near the last chat made, with more listed: the next few made.
    if (last_model && chats_made < chats_listed && list.visible() &&
        list.atEnd(std::max(300.0f, list.bounds().height() * 1.5f))) {
      chats_made += kChatsStep;
      this->show(*last_model, false);
    }
    // The panels' opacity on its way to the chat's.
    if (auto& ease = panel_ease(); ease.t.step(now_ms)) {
      panel_look().opacity = ease.from + (ease.to - ease.from) * ease.t.value();
      this->markDamaged();
    }
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
      // Begun, or more of the chat come: the jump got somewhere.
      if (jump_since_ms < 0.0 || (one && one->timeline.size() != jump_held)) {
        jump_since_ms = now_ms;
        jump_held = one ? one->timeline.size() : 0;
      }
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
          jump_since_ms = now_ms;
          actions->load_context(*chosen, *jumping_to);
        } else if (now_ms - jump_since_ms > kContextPatienceMs) {
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
          history_asked_ms = now_ms;
          jump_since_ms = now_ms;
          actions->load_older(*chosen, *history_from);
        }
      } else if (!history_from && now_ms - jump_since_ms > 2000.0) {
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
    // No time limit: a jump goes on as long as there is history to page
    // back through, as far as the chat's own limit (none, where it says 0) --
    // or until it is stopped by hand, at the loader.
    // And while the older, asked at the top, are on their way.
    if (const bool loading = jump_age > 6 || (this->history_pending() && timeline.current() <= 300.0f);
        loading != chat.area.parts.loading.visible())
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
      this->show(*last_model, false);
    }
    // Each message to come back to, as tdesktop lets one go
    // (checkReplyReturns, at every scroll but its own scroll to a message):
    // once the view has come down to it -- its top above the view's middle,
    // or the view at the very end -- or past it, where it is not made and
    // older than the newest made. The one under it next, if not passed.
    if (chosen && !jumping_to && !aiming)
      if (const auto found = returns.find(*chosen); found != returns.end()) {
        auto& stack = found->second;
        const auto& entries = std::get<0>(std::get<0>(timeline.fChildren).fChildren);
        const skia::SkRect view = timeline.bounds();
        const conversation* one = last_model ? last_model->find(*chosen) : nullptr;
        const auto newest = std::ranges::find_if(entries.rbegin(), entries.rend(),
                                                 [](const message_bubble& row) { return !row.message_id.empty(); });
        while (!stack.empty()) {
          const auto it = std::ranges::find(entries, stack.back(), &message_bubble::message_id);
          bool passed = false;
          if (it != entries.end() && !it->bounds().isEmpty()) {
            passed = timeline.atEnd(0.5f) || timeline.toView(it->bounds()).fTop < view.centerY();
          } else if (one && newest != entries.rend()) {
            const auto at = std::ranges::find(one->timeline, stack.back(), &message::id);
            const auto last = std::ranges::find(one->timeline, newest->message_id, &message::id);
            passed = at != one->timeline.end() && last != one->timeline.end() && at < last;
          }
          if (!passed)
            break;
          stack.pop_back();
        }
      }
    const bool away = this->away();
    if (away != chat.area.parts.jump.visible())
      chat.area.parts.jump.setVisible(away);
    // The @ and the heart, stacked over "↓" where it is up.
    if (const conversation* here = chosen && last_model ? last_model->find(*chosen) : nullptr) {
      int slot = chat.area.parts.jump.visible() ? 1 : 0;
      // Come back to the chat a jump left by hand: that way back is done.
      while (!chat_returns.empty() && chat_returns.back() == *chosen)
        chat_returns.pop_back();
      chat.area.parts.back.show(!chat_returns.empty(), slot);
      if (!chat_returns.empty())
        ++slot;
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
    if (chosen && last_model && !jumping_to && !timeline.glidingToEnd())
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
          history_asked_ms = now_ms;
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

  // The screen as the model is: the list, and the chat shown -- or, where
  // only what the list lists changed (another space, a forum, the search),
  // the list alone: the chat's messages were reconciled again for nothing.
  void show(const model& now, bool with_chat = true) {
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
        // Spaces in their bars, where there are bars; else as tabs.
        if (one.space && !spaces_on)
          folders.emplace_back(display_name(one), folder::space{one.id.id});
        groups.insert(one.groups.begin(), one.groups.end());
      }
      for (const std::string& name : groups)
        folders.emplace_back(name, folder::group{name});
    }
    // The folder chosen kept while it is still there: a tab's, or a bar's --
    // Direct messages, or a space the account has (the bars' are no tabs).
    const bool in_bars = spaces_on && splice::visit(
        splice::overloaded{[](const folder::direct&) { return true; },
                           [&](const folder::space& s) {
                             if (!current)
                               return false;
                             const auto& chats = now.accounts().at(*current).conversations;
                             const auto found = chats.find(s.room);
                             return found != chats.end() && found->second.space;
                           },
                           [](const auto&) { return false; }},
        folder);
    if (!in_bars && std::ranges::find(folders, folder, &std::pair<std::string, folder_t>::second) == folders.end())
      folder = folder::all{};
    // Made again only where they changed: made at every change in the model,
    // new tabs were a full walk and the bar laid out and painted again.
    auto& tabs = std::get<0>(side.folders.fChildren);
    if (folders != shown_folders) {
      tabs.clear();
      for (auto& [name, which] : folders)
        tabs.emplace_back(name, which, which == folder, pick_folder{this});
      shown_folders = folders;
    }
    for (auto& tab : tabs)
      tab.set_chosen(tab.which == folder);
    shown_folder = folder;
    side.folders.setVisible(folders.size() > 1);
    this->show_space_bars(now);
    // Whether a chat is in the folder chosen. A space is a folder, not a
    // chat: it is never listed.
    const account* in = current ? &now.accounts().at(*current) : nullptr;
    // The rooms of the space chosen: its own, and those of the spaces in it,
    // down every level -- each space once, however they hold each other.
    std::set<std::string> in_space;
    if (in)
      if (const std::optional<std::string> chosen_room = space_of(folder)) {
        std::set<std::string> seen;
        std::vector<std::string> todo{*chosen_room};
        while (!todo.empty()) {
          const std::string at = std::move(todo.back());
          todo.pop_back();
          if (!seen.insert(at).second)
            continue;
          if (const auto found = in->conversations.find(at); found != in->conversations.end())
            for (const std::string& child : found->second.children) {
              in_space.insert(child);
              todo.push_back(child);
            }
        }
      }
    // Forums: each listed as one chat; their rooms in them, not beside them.
    std::set<std::string> in_forums;
    if (in)
      for (const conversation_id& one : forums)
        if (one.account == *current)
          if (const auto found = in->conversations.find(one.id); found != in->conversations.end())
            in_forums.insert(found->second.children.begin(), found->second.children.end());
    // The forum open: still one; its rooms, the list.
    if (forum_open && (!current || !forums.contains(conversation_id{*current, *forum_open})))
      forum_open.reset();
    const conversation* forum = forum_open && in && in->conversations.contains(*forum_open) ? &in->conversations.at(*forum_open) : nullptr;
    side.forum_head.setVisible(forum != nullptr);
    if (forum)
      side.forum_head.parts.name.setText(display_name(*forum));
    // What the account's spaces hold: out of Home, where it is chosen so --
    // but direct messages.
    std::set<std::string> in_spaces;
    // Every space's, where Home hides all that spaces hold; else those of
    // the spaces that hide theirs.
    if (in && folder == folder_t{folder::all{}})
      for (const auto& [key, each] : in->conversations)
        if (each.space && (home_hides_spaced || hidden_from_home.contains(each.id)))
          in_spaces.insert(each.children.begin(), each.children.end());
    const auto direct = [](const conversation& one) {
      return splice::visit(splice::overloaded{[](conversation_kind::direct) { return true; }, [](const auto&) { return false; }}, one.kind);
    };
    const auto in_folder = [&](const conversation& one) {
      if (!one.space && in_spaces.contains(one.id.id) && !direct(one))
        return false;
      if (home_hides_spaced && home_hides_direct && folder == folder_t{folder::all{}} && direct(one))
        return false;
      if (forum)
        return !one.space && std::ranges::contains(forum->children, one.id.id);
      // A space is a folder, not a chat -- but a forum is one chat.
      if (one.space && !forums.contains(one.id))
        return false;
      if (!one.space && in_forums.contains(one.id.id))
        return false;
      return splice::visit(splice::overloaded{[](const folder::all&) { return true; },
                                   [&](const folder::space&) { return in_space.contains(one.id.id); },
                                   [&](const folder::group& g) { return std::ranges::contains(one.groups, g.name); },
                                   [&](const folder::direct&) {
                                     return splice::visit(splice::overloaded{[](conversation_kind::direct) { return true; },
                                                                             [](const auto&) { return false; }},
                                                          one.kind);
                                   }},
                        folder);
    };
    // Its own chats -- not those moved to another account's list -- and the
    // other accounts' listed in it.
    const auto found_by = [&](const conversation& one) {
      // Upgraded away, its new room here: only the new one listed, as Element.
      if (one.replaced_by && in && in->conversations.contains(*one.replaced_by))
        return false;
      return in_folder(one) && (wanted.empty() || lower(display_name(one)).contains(wanted) || lower(one.id.id).contains(wanted));
    };
    if (in)
      for (const auto& [key, one] : in->conversations)
        if (!moved_out.contains(one.id) && found_by(one))
          chats.push_back(&one);
    if (in && current)
      if (const auto extra = listed_in.find(*current); extra != listed_in.end())
        for (const conversation_id& id : extra->second)
          if (const conversation* one = now.find(id); one && found_by(*one))
            chats.push_back(one);
    // The strip of a chat listed here from another account.
    const auto strip_for = [&](const conversation* one) -> std::optional<skia::SkColor> {
      if (!current || one->id.account == *current)
        return std::nullopt;
      const auto found = strips.find(one->id);
      return found == strips.end() ? std::nullopt : std::optional<skia::SkColor>(found->second);
    };
    // Each chat's room events as it shows them: what it hides is not its
    // newest, nor counted.
    const auto events_of = [&](const conversation* one) {
      const auto found = event_filters.find(one->id);
      return found == event_filters.end() ? room_event_filter{} : found->second;
    };
    // A forum's row, as tdesktop's: the newest of its topics, and their
    // unread summed -- not the space's own, which says what was done to it.
    std::map<conversation_id, conversation> forum_shown;
    if (in)
      for (const conversation*& one : chats)
        if (one->space && forums.contains(one->id)) {
          conversation made = *one;
          made.timeline.clear();
          made.read_up_to.reset();
          made.detached = false;
          made.unread = 0;
          made.highlights = 0;
          const message* best = nullptr;
          std::string best_in;
          for (const std::string& child : one->children)
            if (const auto found = in->conversations.find(child); found != in->conversations.end() && !found->second.space) {
              const conversation& topic = found->second;
              made.unread += topic.unread_here(events_of(&topic));
              made.highlights += topic.highlights;
              if (const message* last = newest(topic, events_of(&topic)); last && (!best || last->at > best->at)) {
                best = last;
                best_in = topic.id.id;
              }
            }
          if (best) {
            made.timeline.push_back(*best);
            // Said as tdesktop says a forum's newest: in which topic, by whom.
            if (const auto found = in->conversations.find(best_in); found != in->conversations.end()) {
              made.forum_topic = display_name(found->second);
              made.members = found->second.members;
            }
          }
          one = &forum_shown.insert_or_assign(one->id, std::move(made)).first->second;
        }
    // Invites first, as Element lists them; then by their newest.
    std::ranges::sort(chats, std::ranges::greater{}, [&](const conversation* one) {
      const message* last = newest(*one, events_of(one));
      return std::pair{one->invite.has_value(), last ? last->at : std::chrono::sys_time<std::chrono::milliseconds>{}};
    });
    // The rows, as a function of the chats: those whose chat shows the same
    // are kept as they are.
    // The chat open -- or, where Alt+Up or Alt+Down went to a forum, that.
    const auto is_chosen = [&](const conversation* one) {
      return pointed ? *pointed == one->id : chosen && *chosen == one->id;
    };
    const std::vector<conversation_id> listed_before =
        rows | std::views::transform([](const conversation_row<Actions>& row) { return row.id; }) | std::ranges::to<std::vector>();
    chats_listed = chats.size();
    {
      const std::set<conversation_id> listed = chats | std::views::take(chats_made) |
                                               std::views::transform([](const conversation* one) { return one->id; }) |
                                               std::ranges::to<std::set>();
      for (conversation_row<Actions>& row : rows)
        if (!listed.contains(row.id)) {
          const conversation_id id = row.id;
          rows_kept.insert_or_assign(id, std::move(row));
        }
      while (rows_kept.size() > kRowsKept)
        rows_kept.erase(rows_kept.begin());
    }
    if (nodes::reconcile(
            rows, chats | std::views::take(chats_made), [](const conversation* one) { return one->id; },
            [](const conversation_row<Actions>& row) { return row.id; },
            [&](const conversation* one) {
              if (const auto kept = rows_kept.find(one->id); kept != rows_kept.end()) {
                const bool same = kept->second.shown == conversation_row<Actions>::view_of(*one, is_chosen(one), muted.contains(one->id),
                                                                                           draft_of(one->id), events_of(one), strip_for(one));
                if (same) {
                  conversation_row<Actions> back = std::move(kept->second);
                  rows_kept.erase(kept);
                  return back;
                }
                rows_kept.erase(kept);
              }
              return conversation_row<Actions>(actions, *one, is_chosen(one), muted.contains(one->id), draft_of(one->id),
                                               events_of(one), strip_for(one));
            },
            [&](const conversation_row<Actions>& row, const conversation* one) {
              return row.shown ==
                     conversation_row<Actions>::view_of(*one, is_chosen(one), muted.contains(one->id), draft_of(one->id),
                                                        events_of(one), strip_for(one));
            })) {
      list.invalidateLayout();
      // A chat come or gone -- or moved to another place: the whole list
      // painted again, not only what says it moved.
      if (!std::ranges::equal(listed_before, rows, {}, {}, [](const conversation_row<Actions>& row) { return row.id; }))
        list.markDamaged();
    }
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
    if (with_chat)
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
    if (bubbles != bubble_look_now()) {
      bubble_look_now() = bubbles;
      entries.clear();
    }
    info.show(*one, now, muted.contains(one->id));
    chat.area.show_wallpaper(wallpaper);
    if (parts.threads.visible())
      parts.threads.show(*one, &now);
    // Whether the reader may post here: their power against what a message
    // asks, as the room's power levels say; and whether any message of
    // theirs here was not sent.
    {
      const auto mine = one->powers.find(one->id.account.address);
      const std::int64_t level = mine != one->powers.end() ? mine->second : one->power_default;
      const auto asked = one->needs.events.find("m.room.message");
      const std::int64_t needs = asked != one->needs.events.end() ? asked->second : one->needs.events_default;
      chat.line.set_can_post(level >= needs);
      chat.line.set_replaced(one->replaced_by.has_value());
      // Those knocking, for whoever may invite.
      chat.line.show_knocks(actions, one->knocking, level >= one->needs.invite);
      chat.line.show_unsent(std::ranges::any_of(one->timeline, [](const message& said) {
        return said.outgoing &&
               splice::visit(splice::overloaded{[](const delivery::failed&) { return true; }, [](const auto&) { return false; }},
                             said.delivery);
      }));
    }
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
    // A chat shown anew: its stretch as it was left, or its newest.
    if (shown_chat != chosen)
      parts.threads.open.reset();
    if (shown_chat != chosen) {
      if (shown_chat)
        made_of[*shown_chat] = made;
      const auto kept = made_of.find(*chosen);
      made = kept == made_of.end() ? made_range{} : kept->second;
    }
    const auto [first_made, last_made] = this->made_indices(all);
    this->set_made(all, first_made, last_made);
    // A bubble comes in moving only as its message comes: one newer than the
    // newest this chat showed, shown -- not an event the room hides -- and
    // seen, the reader being at the newest. Anything else made -- a row made
    // again as it changed, history paged in, a chat opened, rows scrolled
    // back into what is made -- is there at once. One's own, once the
    // server has it, is the same message under its new id: not again.
    const bool same_chat = shown_chat == chosen;
    const auto newest_before = std::ranges::find(all, shown_last, &message::id);
    const std::size_t new_from =
        newest_before == all.end() ? all.size() : static_cast<std::size_t>(newest_before - all.begin()) + 1;
    const auto arrives = [&](std::size_t i) {
      const bool acknowledged = all[i].outgoing && splice::visit(splice::overloaded{[](const delivery::sent&) { return true; },
                                                                         [](const auto&) { return false; }},
                                                              all[i].delivery);
      return same_chat && was_at_end && i >= new_from && shows(all[i]) && !acknowledged &&
             appeared.insert(all[i].id).second;
    };
    // The bubbles, as a function of the messages: the timeline's, as a
    // thread's are made.
    chat.area.show_messages(*one, all, first_made, last_made, now,
                            shown_how{.filter = filter,
                                      .receipts = receipts_in.contains(one->id),
                                      .previews = !previews_off.contains(one->id),
                                      .unread_from = unread_from},
                            arrives, rooms_wanted);
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
      // Another chat shown: the ways back from jumps in the one left gone,
      // as tdesktop clears its reply returns (showHistory).
      returns.clear();
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
      // Glided only to a newer message: rows above made again or grown kept
      // at the end where they are, at once -- glided, every message slid by
      // and settled.
      timeline.scrollToEnd(last != shown_last);
      unseen = 0;
    } else if (last != shown_last && !one->detached && !shown_detached) {
      // A jump far back holds the timeline from there: the pages after it
      // loaded as the reader goes down are not new, and 30 of them were
      // counted as new under the button -- nor the page that joins
      // it to the newest again.
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
    shown_detached = one->detached;
  }
};

}  // namespace mux::ui
