// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:conversations_side -- The chat list's side: its folders and spaces, the space bars, and the column of chats.
export module mux.ui:conversations_side;

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
using folder_t = spl::variant<folder::all, folder::space, folder::group, folder::direct>;
// The space a folder is, where it is one.
[[nodiscard]] inline std::optional<std::string> space_of(const folder_t& one) {
  return spl::visit(spl::overloaded{[](const folder::space& s) { return std::optional<std::string>(s.room); },
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
  // The colours it is made and ringed in.
  const palette* colours_ = nullptr;
  struct parts_t {
    std::optional<avatar_mark> face;
    std::optional<nodes::Text> mark;
  } parts;
  space_icon(const palette& colours, config::space_item_t what, folder_t shows, config::space_bar_t in, std::string id,
             std::string shown, bool chosen, float size, Pick act)
      : pick(std::move(act)), which(std::move(shows)), item(std::move(what)), bar(in), name(shown), key(id), diameter(size),
        colours_(&colours) {
    this->setHorizontal();
    fStack.justify = nodes::justify::middle{};
    fState.apply({.width = size, .height = size, .cornerRadius = size * 0.5f, .background = colours.tile,
                  .hoverBackground = colours.chosen,
                  .border = scene::Border{chosen ? colours.accent : skia::SkColor{0}, chosen ? 2.0f : 0.0f}});
    spl::visit(spl::overloaded{[&](config::space_item::home) { parts.mark.emplace("\u2302", size * 0.5f, colours.text); },
                                     [&](config::space_item::direct) { parts.mark.emplace("@", size * 0.45f, colours.text, true); },
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
    fState.apply({.border = scene::Border{on ? colours_->accent : skia::SkColor{0}, on ? 2.0f : 0.0f}});
  }
};

// A folder's tab over the chat list, as Telegram's: its name, and under
// the one chosen a line in the accent.
template <class Pick>
struct folder_tab : scene::Node {
  Pick pick;
  folder_t which;
  bool chosen = false;
  // The colours it is made and lit in.
  const palette* colours_ = nullptr;
  struct parts_t {
    nodes::Text label;
    // The line under the one chosen, in the accent.
    nodes::Box<> underline;
  } parts;
  folder_tab(const palette& colours, std::string name, folder_t what, bool is_chosen, Pick act)
      : pick(std::move(act)), which(std::move(what)), chosen(is_chosen), colours_(&colours),
        parts{.label = nodes::Text(std::move(name), 13.0f, is_chosen ? colours.accent : colours.dim, true),
              .underline = nodes::Box<>(colours.accent)} {
    auto& [label, underline] = parts;
    fState.apply({.height = 32.0f,
                  .autoSize = scene::axes::kX,
                  .padding = {0.0f, 10.0f, 0.0f, 10.0f},
                  .cornerRadius = 6.0f,
                  .hoverBackground = colours.chosen,
                  .focusBackground = colours.chosen});
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
    parts.label.setColour(on ? colours_->accent : colours_->dim);
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

inline constexpr float kMinSidebar = 240.0f;
// The side column's pieces: a space bar's view, and the column, each picked
// in as Pick says -- the screen's own choice.
// A space bar: its items, in a line.
template <class Pick>
using space_icons = nodes::Flow<std::vector<space_icon<Pick>>>;
// The top bar's view: its line of items, moved along by the wheel where
// it is longer than the view -- cut to it.
template <class Pick>
struct top_view : nodes::Stack {
  float offset = 0.0f;
  struct parts_t {
    space_icons<Pick> line{{.direction = nodes::direction::horizontal{}, .spacingX = 4.0f, .wrap = false}, {}};
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
template <class Actions, class Pick>
struct side_column : nodes::Stack {
  // The colours it is made in, for what it makes later: its menus, the
  // icon dragged.
  const palette* colours_ = nullptr;
  float wanted = 300.0f;
  // All of the window across: one thing at a time (single, below).
  bool whole = false;
  struct head_row : nodes::Stack {
    using explore_button = icon_button<ask<Actions, &Actions::open_explore>>;
    struct parts_t {
      menu_button<Actions> menu;
      nodes::Text name;
      // The top bar of spaces, after the name: there, empty or not, unless
      // the settings say otherwise -- something can always be put in it.
      // Longer than its room, it scrolls sideways.
      top_view<Pick> top;
      // Explore rooms, out of the new chat's box: beside the chats, as
      // Element's compass is.
      explore_button explore;
    } parts;
    head_row(const palette& colours, Actions* a)
        : parts{.menu = menu_button<Actions>(colours, a),
                .name = nodes::Text("mux", 17.0f, colours.text, true),
                .explore = explore_button(colours, icon::compass{}, {a})} {
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
      widgets::TextArea<> field;
    } parts;
    widgets::TextArea<>& field = parts.field;
    explicit search_box(const palette& colours) : parts{.field = widgets::TextArea<>(colours.widgets, "Search")} {
      fState.apply({.fillX = true, .height = 36.0f, .margin = {0.0f, 10.0f, 8.0f, 10.0f}, .cornerRadius = 18.0f, .background = colours.tile, .selectedBackground = colours.chosen});
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
      nodes::Text name;
      // Its settings: it is in no bar, to be right-pressed.
      icon_button<ask<Actions, &Actions::manage_forum>> settings;
    } parts;
    forum_head_t(const palette& colours, Actions* a)
        : parts{.back = icon_button<ask<Actions, &Actions::close_forum>>(colours, icon::back{}, {a}),
                .name = nodes::Text("", 15.0f, colours.text, true),
                .settings = icon_button<ask<Actions, &Actions::manage_forum>>(colours, icon::gear{}, {a})} {
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
        top_t(const palette& colours, std::string name, std::string when)
            : name_time_line(std::move(name), std::move(when), colours.text, colours.dim, 12.0f) {}
      };
      struct parts_t {
        top_t top;
        nodes::Text text;
      } parts;
      lines_t(const palette& colours, const search_result& one)
          : parts{.top = top_t(colours, one.name, std::format("{:%d.%m.%y}", std::chrono::floor<std::chrono::days>(one.at))),
                  .text = nodes::Text(one.snippet, 13.0f, colours.dim)} {
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
    found_row(const palette& colours, Actions* a, const search_result& one)
        : pick{a, one.index}, parts{.face = avatar_mark(one.sender, one.name, 40.0f), .lines = lines_t(colours, one)} {
      this->setHorizontal();
      this->setGap(10.0f);
      fState.apply({.fillX = true, .height = 56.0f, .padding = {0.0f, 12.0f, 0.0f, 10.0f}, .hoverBackground = colours.chosen});
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
  // Nothing joined matching what is searched: the rooms of the server's
  // directory and the people of its user directory that do -- as Explore
  // and Start chat list them, to join or to write to.
  using room_rows_t = nodes::Flow<std::vector<directory_row<Actions>>>;
  using people_rows_t = nodes::Flow<std::vector<found_person_row<Actions>>>;
  struct elsewhere_list : nodes::Stack {
    struct parts_t {
      nodes::Text rooms_title;
      room_rows_t rooms{room_rows_t({.spacingY = 0.0f, .wrap = false}, {})};
      nodes::Text people_title;
      people_rows_t people{people_rows_t({.spacingY = 0.0f, .wrap = false}, {})};
      nodes::Text status;
    } parts;
    explicit elsewhere_list(const palette& colours)
        : parts{.rooms_title = nodes::Text("Rooms", 13.0f, colours.dim, true),
                .people_title = nodes::Text("People", 13.0f, colours.dim, true),
                .status = nodes::Text("", 13.0f, colours.dim)} {
      this->setGap(4.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {4.0f, 0.0f, 8.0f, 0.0f}});
      for (nodes::Text* each : {&parts.rooms_title, &parts.people_title, &parts.status})
        each->apply({.margin = {6.0f, 16.0f, 2.0f, 16.0f}});
      parts.status.setWrapped(true);
      parts.rooms.apply({.fillX = true, .autoSize = scene::axes::kY});
      parts.people.apply({.fillX = true, .autoSize = scene::axes::kY});
    }
  };
  using elsewhere_t = nodes::ScrollContainer<elsewhere_list>;
  struct rest_t : nodes::Stack {
    struct parts_t {
      forum_head_t forum_head;
      search_box search;
      // The folders, where the account has groups -- or spaces, with no
      // bars: a line of tabs.
      nodes::Flow<std::vector<folder_tab<Pick>>> folders{
          {.direction = nodes::direction::horizontal{}, .spacingX = 2.0f, .spacingY = 2.0f}, {}};
      nodes::Text no_chats;
      list_t list{nodes::Flow<std::vector<conversation_row<Actions>>>({.spacingY = 0.0f, .wrap = false}, {})};
      // While a chat is searched: what was found, in the chats' place.
      nodes::Text found_title;
      found_list_t found{nodes::Flow<std::vector<found_row>>({.spacingY = 0.0f, .wrap = false}, {})};
      elsewhere_t elsewhere;
    } parts;
    rest_t(const palette& colours, Actions* a)
        : parts{.forum_head = forum_head_t(colours, a),
                .search = search_box(colours),
                .no_chats = nodes::Text("No chats yet.", 13.0f, colours.dim),
                .found_title = nodes::Text("", 13.0f, colours.dim, true),
                .elsewhere = elsewhere_t(elsewhere_list(colours))} {
      parts.found_title.apply({.margin = {4.0f, 16.0f, 6.0f, 16.0f}});
      parts.found_title.setVisible(false);
      parts.found.apply({.fillX = true, .grow = scene::axes::kY});
      std::get<0>(parts.found.fChildren).apply({.fillX = true, .autoSize = scene::axes::kY});
      parts.found.setVisible(false);
      parts.elsewhere.apply({.fillX = true, .grow = scene::axes::kY});
      parts.elsewhere.setVisible(false);
      fState.apply({.fillY = true, .grow = scene::axes::kX});
      parts.forum_head.setVisible(false);
      parts.no_chats.apply({.margin = {12.0f, 16.0f, 0.0f, 16.0f}});
      parts.folders.apply({.fillX = true, .autoSize = scene::axes::kY, .margin = {0.0f, 8.0f, 6.0f, 8.0f}});
      parts.list.apply({.fillX = true, .grow = scene::axes::kY});
      // Chats change places by their newest: the view stays at its offset,
      // not following the row it showed first down the list.
      parts.list.setHoldsInView(false);
      std::get<0>(parts.list.fChildren).apply({.fillX = true, .autoSize = scene::axes::kY});
    }
  };
  // Under the head: the side bar of spaces, under the menu's button and
  // down to the window's bottom -- drawn where it holds any -- and the rest.
  struct body_t : nodes::Stack {
    struct parts_t {
      // Longer than the window, it scrolls.
      nodes::ScrollContainer<space_icons<Pick>> side{space_icons<Pick>({.spacingY = 8.0f, .wrap = false, .crossAlign = scene::align::kMiddle}, {})};
      rest_t rest;
    } parts;
    body_t(const palette& colours, Actions* a) : parts{.rest = rest_t(colours, a)} {
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
  struct leave_act {
    Actions* actions;
    std::string account;
    std::string room;
    void operator()() const { actions->open_leave_space(conversation_id{account_id{protocol_of(account), account}, room}); }
  };
  // A room, or a space, made in it -- Element's Add room and Add space.
  struct create_in_act {
    Actions* actions;
    std::string account;
    std::string room;
    std::string name;
    bool make_space = false;
    void operator()() const {
      actions->open_new_room_in(conversation_id{account_id{protocol_of(account), account}, room}, name, make_space);
    }
  };
  // The column's menus' look: a card over the rest, 190 wide.
  static void as_popup(nodes::Stack& menu, const palette& colours) {
    menu.setGap(4.0f);
    menu.fState.apply({.width = 190.0f, .autoSize = scene::axes::kY, .padding = {8.0f, 8.0f, 8.0f, 8.0f}, .cornerRadius = 10.0f,
                       .background = colours.popup(), .border = scene::Border{colours.band, 1.0f},
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
    chat_menu(const palette& colours, Actions* a, conversation_id id, std::string name, const account_id& listing,
              const std::vector<account_id>& accounts, const config::theme_t& theme)
        : parts{.title = nodes::Text(std::move(name), 13.0f, colours.dim, true),
                .settings = widgets::Button<chat_settings_act>(colours.widgets, "Chat settings\u2026", {a, id})} {
      as_popup(*this, colours);
      fState.apply({.width = 320.0f});
      parts.title.setElided(true);
      parts.title.apply({.fillX = true});
      parts.settings.apply({.fillX = true, .height = 30.0f});
      for (const account_id& to : accounts)
        if (to != id.account && to != listing) {
          parts.places.emplace_back(colours.widgets, std::format("Copy to {}", to.address), place_act{a, id, to, false});
          parts.places.emplace_back(colours.widgets, std::format("Move to {}", to.address), place_act{a, id, to, true});
        }
      if (id.account != listing) {
        parts.unplace.emplace(colours.widgets, "Remove from this list", unplace_act{a, id, listing});
        parts.strip.emplace(colours.widgets, "Strip on or off", strip_act{a, id, listing});
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
      widgets::Button<create_in_act> add_room, add_space;
      widgets::Button<leave_act> leave;
      widgets::Button<set_bars_act> side, top, both, hide;
    } parts;
    [[nodiscard]] static std::string room_of(const config::space_item_t& item) {
      return spl::visit(spl::overloaded{[](const config::space_item::space& s) { return s.room; },
                                              [](const auto&) { return std::string(); }},
                           item);
    }
    space_menu(const palette& colours, Actions* a, const std::string& account, const config::space_item_t& item, std::string name)
        : parts{.title = nodes::Text(name, 13.0f, colours.dim, true),
                .explore = widgets::Button<explore_act>(colours.widgets, "Explore its rooms\u2026", {a, room_of(item)}),
                .manage = widgets::Button<manage_act>(colours.widgets, "Space settings\u2026", {a, room_of(item)}),
                .add_room = widgets::Button<create_in_act>(colours.widgets, "Create a room in it\u2026", {a, account, room_of(item), name, false}),
                .add_space = widgets::Button<create_in_act>(colours.widgets, "Create a space in it\u2026", {a, account, room_of(item), name, true}),
                .leave = widgets::Button<leave_act>(colours.widgets, "Leave space\u2026", {a, account, room_of(item)}),
                .side = widgets::Button<set_bars_act>(colours.widgets, "Side bar only", {a, account, item, true, false}),
                .top = widgets::Button<set_bars_act>(colours.widgets, "Top bar only", {a, account, item, false, true}),
                .both = widgets::Button<set_bars_act>(colours.widgets, "Both bars", {a, account, item, true, true}),
                .hide = widgets::Button<set_bars_act>(colours.widgets, "Hide", {a, account, item, false, false})} {
      as_popup(*this, colours);
      parts.title.setElided(true);
      parts.title.apply({.fillX = true});
      for (scene::Node* each : std::initializer_list<scene::Node*>{&parts.explore, &parts.manage, &parts.add_room, &parts.add_space,
                                                                   &parts.leave, &parts.side, &parts.top, &parts.both, &parts.hide})
        each->apply({.fillX = true, .height = 30.0f});
      for (scene::Node* each : std::initializer_list<scene::Node*>{&parts.explore, &parts.manage, &parts.add_room, &parts.add_space,
                                                                   &parts.leave})
        each->setVisible(!room_of(item).empty());
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
    std::optional<space_icon<Pick>> ghost;
  } parts;
  // Its parts by their names, for what reads them: it is never moved.
  head_row& head = parts.head;
  search_box& search = parts.body.parts.rest.parts.search;
  forum_head_t& forum_head = parts.body.parts.rest.parts.forum_head;
  nodes::Text& found_title = parts.body.parts.rest.parts.found_title;
  found_list_t& found = parts.body.parts.rest.parts.found;
  elsewhere_t& elsewhere = parts.body.parts.rest.parts.elsewhere;
  nodes::Flow<std::vector<folder_tab<Pick>>>& folders = parts.body.parts.rest.parts.folders;
  nodes::Text& no_chats = parts.body.parts.rest.parts.no_chats;
  list_t& list = parts.body.parts.rest.parts.list;
  // The bars as they show -- what is hidden, lit, a drop's place -- and the
  // lines of items in them.
  top_view<Pick>& top_bar = parts.head.parts.top;
  nodes::ScrollContainer<space_icons<Pick>>& side_bar = parts.body.parts.side;
  space_icons<Pick>& top_line = parts.head.parts.top.parts.line;
  space_icons<Pick>& side_line = std::get<0>(parts.body.parts.side.fChildren);
  Actions* actions = nullptr;
  // Whose spaces the bars hold, as the screen says as it shows them.
  std::string account;
  side_column(const palette& colours, Actions* a)
      : colours_(&colours), parts{.head = head_row(colours, a), .body = body_t(colours, a)}, actions(a) {
    fState.apply({.fillY = true, .background = colours.sidebar});
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
  [[nodiscard]] space_icon<Pick>* icon_of(const config::space_item_t& item, const config::space_bar_t& bar) {
    space_icons<Pick>& line = spl::visit(spl::overloaded{[&](config::space_bar::top) -> space_icons<Pick>& { return top_line; },
                                                     [&](const auto&) -> space_icons<Pick>& { return side_line; }},
                                  bar);
    const auto found = std::ranges::find(icons_of(line), item, &space_icon<Pick>::item);
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
    space_icon<Pick>* one = this->icon_of(from.item, from.from);
    if (!one)
      return;
    parts.ghost.emplace(*colours_, one->item, one->which, one->bar, one->key, one->name, false, one->diameter, one->pick);
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
      for (space_icons<Pick>* line : {&top_line, &side_line})
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
  [[nodiscard]] std::vector<space_icon<Pick>>& icons_of(space_icons<Pick>& bar) { return std::get<0>(bar.fChildren); }
  [[nodiscard]] skia::SkRect shown_at(const space_icon<Pick>& one) const {
    return spl::visit(spl::overloaded{[&](config::space_bar::top) { return one.bounds().makeOffset(-top_bar.offset, 0.0f); },
                                            [&](const auto&) { return side_bar.toView(one.bounds()); }},
                         one.bar);
  }
  [[nodiscard]] const space_icon<Pick>* icon_at(float x, float y) {
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
    const bool along_x = spl::visit(spl::overloaded{[](config::space_bar::top) { return true; }, [](const auto&) { return false; }}, bar);
    space_icons<Pick>& icons = along_x ? top_line : side_line;
    std::vector<config::space_item_t> out = std::ranges::to<std::vector>(std::views::transform(std::views::filter(icons_of(icons), [&](const auto& one) { return one.item != item; }), [](const auto& one) { return one.item; }));
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
    top_bar.apply({.background = top ? colours_->chosen : skia::SkColor{0}});
    side_bar.apply({.background = side ? colours_->chosen : skia::SkColor{0}});
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
  // A menu just made, where it was pressed, kept inside the column: as far
  // from its right and bottom edges as the menu is wide and high.
  void place_menu(auto& menu, float x, float y, float width, float height) {
    const skia::SkRect box = fState.fBounds;
    menu.apply({.place = scene::anchor::kTopLeft,
                .x = std::clamp(x - box.fLeft, 0.0f, std::max(0.0f, box.width() - width)),
                .y = std::clamp(y - box.fTop, 0.0f, std::max(0.0f, box.height() - height))});
    menu_close_due = false;
    this->invalidateLayout();
  }
  void drag_down(const scene::pointer::down& press, scene::PointerReply& reply) {
    // A press off the menu closes it at once -- nothing of it is pressed;
    // one on it chooses, and the program closes it then.
    if (this->menu_up() && !this->menu_has(press.x, press.y))
      this->close_menu();
    drag.reset();
    const space_icon<Pick>* one = this->icon_at(press.x, press.y);
    // A right press on a chat in the list: its menu, where it was pressed.
    if (!one && press.button == 3 && list.visible())
      for (const auto& row : std::get<0>(std::get<0>(list.fChildren).fChildren))
        if (list.toView(row.bounds()).contains(press.x, press.y)) {
          parts.row_menu.emplace(*colours_, actions, row.id, row.parts.lines.parts.top.parts.name.text(),
                                 current_account ? *current_account : row.id.account, accounts_known, theme_now);
          this->place_menu(*parts.row_menu, press.x, press.y, 320.0f, 260.0f);
          reply.handle();
          return;
        }
    if (!one)
      return;
    // A right press: its menu, where it was pressed, kept in the column.
    if (press.button == 3) {
      parts.menu.emplace(*colours_, actions, account, one->item, one->name);
      this->place_menu(*parts.menu, press.x, press.y, 190.0f, 180.0f);
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
      const bool along_side = spl::visit(spl::overloaded{[](config::space_bar::side) { return true; },
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
    const bool along_x = spl::visit(spl::overloaded{[](config::space_bar::top) { return true; }, [](const auto&) { return false; }}, to_bar);
    const std::vector<config::space_item_t> order = this->order_with(to_bar, was.item, at.x, at.y);
    const auto index = static_cast<std::size_t>(std::ranges::find(order, was.item) - order.begin());
    skia::SkPoint target{at.x, at.y};
    space_icons<Pick>& line = along_x ? top_line : side_line;
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
    if (whole) {
      fState.fWidth = parent.width();
      return;
    }
    fState.fWidth = std::clamp(wanted, std::min(kMinSidebar, parent.width()),
                               std::max(kMinSidebar, parent.width() * 0.6f));
  }
  // The bar of spaces taller for a finger, and under Search, all of the
  // column across, as the user would have it (#13671) -- or as it was, in
  // the head after "mux".
  bool big = false;
  static constexpr float kBigBar = 58.0f;
  void set_big_spaces(bool on) {
    big = on;
    parts.head.parts.top.apply({.height = on ? kBigBar : 34.0f});
    // Room under Search for it: the chats below it moved down by as much.
    search.apply({.margin = {0.0f, 10.0f, on ? 8.0f + kBigBar + 8.0f : 8.0f, 10.0f}});
    this->invalidateLayout();
    this->markDamaged();
  }
  void layoutChildren() {
    auto& top = parts.head.parts.top;
    top.fState.setOutOfFlow(big);
    if (big) {
      // Placed under Search by its anchor in the head, not shifted there:
      // where it is laid out is where it is drawn, pressed and repainted.
      // Search where the last layout put it, the head where it is.
      const float below = search.bounds().fBottom + 8.0f - parts.head.fState.contentBox().fTop;
      top.apply({.place = scene::anchor::kTopLeft, .x = 0.0f, .y = below,
                 .width = std::max(0.0f, fState.contentBox().width() - 16.0f)});
    } else {
      top.apply({.x = 0.0f, .y = 0.0f});
    }
    top.fState.shiftTo(0.0f, 0.0f);
    this->nodes::Stack::layoutChildren();
  }
};

}  // namespace mux::ui
