// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:info_explore -- A server's public rooms, or a space's, explored and joined.
export module mux.ui:info_explore;

import std;
import mux.logic.text;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
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
import :info_threads;
import :info_packs;

export namespace mux::ui {
// Join a room; a space in a space's listing, opened -- its own listed.
template <class Actions>
struct directory_join {
  // A space opened to look into, else the room joined.
  using Answer = std::variant<::mux::ui::request::explore_space, ::mux::ui::request::join_directory_room>;
  std::string room;
  std::string server;
  bool open = false;
  std::string name;
  Answer operator()() const {
    if (open)
      return ::mux::ui::request::explore_space{room, name};
    return ::mux::ui::request::join_directory_room{room, server};
  }
};

// A room of a directory: its picture, name, address, members and topic, and
// Join -- in Explore, and in the chat list where nothing joined matches.
template <class Actions> struct directory_row : skiff::compose::Stacked {
  struct texts_t : skiff::compose::Stacked {
    struct parts_t {
      nodes::Text name;
      nodes::Text line;
      nodes::Text topic;
    } parts;
    texts_t(const palette &colours, const directory_room &one)
        : Stacked(
              skiff::compose::vbox(2.0f, {.autoSize = scene::axes::kY,
                                          .grow = scene::axes::kX,
                                          .shrink = scene::axes::kX,
                                          .alignSelf = scene::align::kMiddle})),
          parts{.name = elided(nodes::Text(
                    one.name.empty() ? (one.alias.empty() ? one.id : one.alias)
                                     : one.name,
                    14.0f, colours.text, true)),
                .line = elided(nodes::Text(
                    std::format("{}{}{} member{}", one.alias,
                                one.alias.empty() ? "" : " \u00b7 ",
                                one.members, one.members == 1 ? "" : "s"),
                    12.0f, colours.dim)),
                .topic = skiff::compose::visible(
                    !one.topic.empty(),
                    elided(nodes::Text(one.topic, 13.0f, colours.text)))} {

      for (nodes::Text* each : {&parts.name, &parts.line, &parts.topic})
        each->apply({.fillX = true});
    }
  };
  struct parts_t {
    avatar_mark face;
    texts_t texts;
    widgets::Button<directory_join<Actions>> join;
  } parts;
  directory_row(const palette &colours, const directory_room &one,
                const std::string &server)
      : Stacked(skiff::compose::hbox(12.0f,
                                     {.fillX = true,
                                      .autoSize = scene::axes::kY,
                                      .padding = {8.0f, 16.0f, 8.0f, 16.0f}})),
        parts{.face = avatar_mark(
                  one.id, one.name.empty() ? one.alias : one.name, 40.0f),
              .texts = texts_t(colours, one),
              .join = skiff::compose::styled(
                  {.width = 70.0f,
                   .height = 30.0f,
                   .alignSelf = scene::align::kMiddle},
                  primary(widgets::Button<directory_join<Actions>>(
                      colours.widgets, one.space ? "Open" : "Join",
                      {one.space ? one.id
                                 : (one.alias.empty() ? one.id : one.alias),
                       server, one.space, one.name})))} {}
};

// Element's Explore rooms: a server's public directory, searched -- one's
// own, or another named -- each room with its picture, name, address, how
// many are in it and what it is about, and Join. An address typed in is
// gone to at once.
// Explore open: the server it lists first.
// What a directory listed, as Explore shows it: the rooms so far, of which
// server, in which space, for what was searched, and where the next page
// starts.
struct explore_listing {
  std::vector<directory_room> rooms;
  std::string server;
  std::optional<std::string> space;
  std::string query;
  std::optional<std::string> next;
};
// A space looked into: its room, and its name over what it holds.
struct explore_space_shown {
  std::string room;
  std::string name;
};
struct explore_facts {
  std::string own_server;
  std::optional<explore_listing> listing;
  // Asked of the server, not answered yet.
  bool loading = false;
  std::optional<explore_space_shown> space;
};
template <class Actions> struct explore_box : skiff::compose::Stacked {
  // The dialog it is shown in.
  [[nodiscard]] static dialog_look look_of_dialog() { return {.size = dialog_size::fixed{640.0f, 560.0f}}; }
  // The colours it is made in, for its parts and the rows it makes later.
  const palette* colours_ = nullptr;
  struct close_it {
    using Answer = ::mux::ui::request::close_explore;
    ::mux::ui::request::close_explore operator()() { return ::mux::ui::request::close_explore{}; }
  };
  struct search_press {
    using Answer = std::optional<::mux::ui::request::search_rooms>;
    explore_box* box;
    std::optional<::mux::ui::request::search_rooms> operator()() const {
      if (box->space) {
        box->filter(box->parts.search.parts.query.text());
        return std::nullopt;
      }
      box->parts.status.setText("Searching\u2026");
      box->parts.status.setVisible(true);
      return ::mux::ui::request::search_rooms{box->parts.search.parts.server.text(), box->parts.search.parts.query.text()};
    }
  };
  // Whose rooms are listed, where a space's are; and what it listed, to be
  // searched here -- its server searches no space.
  std::optional<std::string> space;
  std::vector<directory_room> listed;
  std::string listed_server;
  // The search listed, and where its next page starts, where there is one.
  std::string listed_query;
  std::optional<std::string> next;
  struct more_press {
    using Answer = std::optional<::mux::ui::request::search_rooms>;
    explore_box* box;
    std::optional<::mux::ui::request::search_rooms> operator()() const {
      if (!box->next)
        return std::nullopt;
      box->parts.more.setLabel("Loading\u2026");
      return ::mux::ui::request::search_rooms{box->listed_server, box->listed_query, *box->next};
    }
  };
  // A space's name and picture, over what it holds.
  struct space_head_t : skiff::compose::Stacked {
    struct parts_t {
      std::optional<avatar_mark> face;
      nodes::Text name;
    } parts;
    explicit space_head_t(const palette &colours)
        : Stacked(skiff::compose::hbox(
              10.0f, {.fillX = true,
                      .autoSize = scene::axes::kY,
                      .padding = {0.0f, 10.0f, 8.0f, 10.0f}})),
          parts{
              .name = skiff::compose::styled(
                  {.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle},
                  elided(nodes::Text("", 17.0f, colours.text, true)))} {}
  };
  using join_press = directory_join<Actions>;
  using result_row = directory_row<Actions>;
  using header_t = page_header<no_back, close_it>;
  struct search_row : skiff::compose::Stacked {
    struct parts_t {
      field query;
      field server;
      widgets::Button<search_press> search;
    } parts;
    search_row(explore_box *box, const std::string &own)
        : Stacked(skiff::compose::hbox(8.0f,
                                       {.fillX = true,
                                        .autoSize = scene::axes::kY,
                                        .padding = {0.0f, 6.0f, 0.0f, 6.0f}})),
          parts{
              .query = skiff::compose::styled(
                  {.relativeSize = scene::axes::kNone, .grow = scene::axes::kX},
                  field(*box->colours_, "Find a room",
                        "Name, topic, or #address:server")),
              .server = skiff::compose::styled(
                  {.width = 170.0f, .relativeSize = scene::axes::kNone},
                  field(*box->colours_, "Server", own, own)),
              .search = skiff::compose::styled(
                  {.width = 90.0f,
                   .height = 34.0f,
                   .alignSelf = scene::align::kEnd,
                   .margin = {0.0f, 0.0f, 2.0f, 0.0f}},
                  primary(widgets::Button<search_press>(box->colours_->widgets,
                                                        "Search", {box})))} {

      // Their full-width default let go -- fillX = false says nothing -- or
      // the query, as wide as the row and growing too, lost its growing and
      // pushed the rest out past the dialog's right edge.
    }
  };
  using rows_t = nodes::Flow<std::vector<result_row>>;
  struct parts_t {
    header_t header;
    space_head_t space_head;
    search_row search;
    nodes::Text status;
    nodes::ScrollContainer<rows_t> list{rows_t({.spacingY = 0.0f, .wrap = false}, {})};
    // The server's next page, after its first fifty, as Element's.
    widgets::Button<more_press> more;
  } parts;
  // The row's fields, by their names, for what reads them.
  field& query_field() { return parts.search.parts.query; }
  explore_box(const ui_needs<Actions>& n, const explore_facts& facts) : explore_box(*n.colours, facts.own_server) { this->show_page(facts); }
  // What is shown, as its facts say: the space looked into, the asking, the
  // rooms listed -- all of them, as the program keeps them.
  void show_page(const explore_facts& facts) {
    if (facts.space)
      this->as_space(facts.space->room, facts.space->name);
    if (facts.loading) {
      parts.status.setText("Loading the rooms this server lists\u2026");
      parts.status.setVisible(true);
    }
    if (facts.listing)
      this->show(facts.listing->rooms, facts.listing->server, facts.listing->space, facts.listing->query, facts.listing->next, false);
  }
  explore_box(const palette &colours, const std::string &own_server)
      : Stacked(skiff::compose::vbox(0.0f,
                                     {.fillX = true,
                                      .height = 560.0f,
                                      .padding = {0.0f, 12.0f, 12.0f, 12.0f}})),
        colours_(&colours),
        parts{
            .header = header_t(colours, "Explore rooms", {}, {}, false, true),
            .space_head = skiff::compose::visible(false, space_head_t(colours)),
            .search = search_row(this, own_server),
            .status = skiff::compose::visible(
                false,
                skiff::compose::styled(
                    {.fillX = true, .margin = {6.0f, 10.0f, 4.0f, 10.0f}},
                    wrapped(nodes::Text("", 13.0f, colours.dim)))),
            .more = skiff::compose::visible(
                false, skiff::compose::styled(
                           {.fillX = true,
                            .height = 32.0f,
                            .margin = {6.0f, 10.0f, 0.0f, 10.0f}},
                           widgets::Button<more_press>(colours.widgets,
                                                       "Load more", {this})))} {

    parts.list.apply({.fillX = true, .grow = scene::axes::kY});
    std::get<0>(parts.list.fChildren).apply({.fillX = true, .autoSize = scene::axes::kY});
  }
  // A space's listing: its name and picture on top, no server to ask --
  // the search looks through what it holds.
  void as_space(const std::string& room, const std::string& name) {
    space = room;
    parts.space_head.parts.face.emplace(room, name, 36.0f);
    parts.space_head.parts.name.setText(name);
    parts.space_head.setVisible(true);
    parts.search.parts.server.setVisible(false);
    this->invalidateLayout();
  }
  // What it listed, whose name, topic or address has what is typed.
  void filter(const std::string& typed) {
    constexpr auto lower = mux::logic::folded;
    const std::string wanted = lower(typed);
    const std::vector<directory_room> found =
        std::ranges::to<std::vector>(std::views::filter(listed, [&](const directory_room& one) {
          return wanted.empty() || lower(one.name).contains(wanted) || lower(one.topic).contains(wanted) ||
                 lower(one.alias).contains(wanted);
        }));
    this->show_rows(found, listed_server, space);
  }
  // What the directory listed -- or a space.
  // A further page goes after what was listed.
  void show(const std::vector<directory_room>& rooms, const std::string& server,
            const std::optional<std::string>& space_of = std::nullopt, const std::string& query = {},
            const std::optional<std::string>& after = std::nullopt, bool more = false) {
    if (more)
      listed.insert(listed.end(), rooms.begin(), rooms.end());
    else
      listed = rooms;
    listed_server = server;
    listed_query = query;
    next = after;
    parts.more.setLabel("Load more");
    parts.more.setVisible(next.has_value() && !space_of);
    this->show_rows(listed, server, space_of);
  }
  void show_rows(const std::vector<directory_room>& rooms, const std::string& server,
                 const std::optional<std::string>& space) {
    auto& rows = std::get<0>(std::get<0>(parts.list.fChildren).fChildren);
    rows.clear();
    rows.reserve(rooms.size());
    for (const directory_room& one : rooms)
      rows.emplace_back(*colours_, one, server);
    // Their pictures, asked for as a chat's are.
    for (const directory_room& one : rooms)
      if (one.avatar && !one.avatar->empty())
        listed_avatars().emplace_back(one.id, *one.avatar);
    parts.status.setText(space ? (rooms.empty() ? std::string("Nothing in this space, or its server would not say.")
                                                : std::format("{} rooms and spaces in this space", rooms.size()))
                               : rooms.empty() ? std::string("No rooms found.")
                                               : std::format("{} rooms", rooms.size()));
    parts.status.setVisible(true);
    this->invalidateLayout();
  }
};

}  // namespace mux::ui
