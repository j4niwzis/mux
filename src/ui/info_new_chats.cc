// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:info_new_chats -- Where a message is forwarded to; Start chat; Create a room.
export module mux.ui:info_new_chats;

import std;
import mux.logic.text;
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

export namespace mux::ui {
// A room made in a space, as its menu asks: the space, its name, and
// whether what is made is a space.
struct new_room_place {
  conversation_id space;
  std::string name;
  bool make_space = false;
};

// A chat to forward to: its id and name.
struct forward_target {
  conversation_id id;
  std::string name;
};

// Where to forward a message, as tdesktop's box: the account's chats, with
// a field to find one by its name; a press sends it there.
template <class Actions>
struct forward_box : nodes::Stack {
  // The dialog it is shown in.
  [[nodiscard]] static dialog_look look_of_dialog() { return {.size = dialog_size::fixed{400.0f, 520.0f}}; }
  Actions* actions = nullptr;
  // The colours it is made in, for the rows it makes later.
  const palette* colours_ = nullptr;
  std::vector<forward_target> all;
  struct close_it {
    Actions* actions;
    void operator()() const { actions->close_forward(); }
  };
  struct typed {
    forward_box* box;
    void operator()(std::string_view text) const { box->find(text); }
  };
  struct row : nodes::Stack {
    Actions* actions;
    conversation_id id;
    struct parts_t {
      avatar_mark face;
      nodes::Text name;
    } parts;
    row(Actions* a, const palette& colours, const forward_target& one)
        : actions(a), id(one.id), parts{.face = avatar_mark(one.id.id, one.name, 36.0f),
                                        .name = nodes::Text(one.name, 15.0f, colours.text)} {
      this->setHorizontal();
      this->setGap(12.0f);
      fState.apply({.fillX = true, .height = 50.0f, .padding = {0.0f, 20.0f, 0.0f, 20.0f},
                    .hoverBackground = colours.chosen});
      fState.setCursor(scene::cursor::hand{});
      parts.name.setElided(true);
      parts.name.apply({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
    }
    [[nodiscard]] bool acceptsInput() const { return true; }
    [[nodiscard]] bool hoverChangesAppearance() const { return true; }
    [[nodiscard]] bool onClick(float, float) {
      actions->forward_to(id);
      return true;
    }
  };
  using rows_t = nodes::Flow<std::vector<row>>;
  using header_t = page_header<no_back, close_it>;
  struct parts_t {
    header_t header;
    widgets::TextBox<typed> field;
    nodes::ScrollContainer<rows_t> list{rows_t({.spacingY = 0.0f, .wrap = false}, {})};
  } parts;

  forward_box(Actions* a, const palette& colours, const std::vector<forward_target>& chats)
      : actions(a), colours_(&colours), all(chats),
        parts{.header = header_t(colours, "Forward to…", {}, {a}, false, true),
              .field = widgets::TextBox<typed>(colours.widgets, "Search", {this})} {
    fState.apply({.fillX = true, .height = 520.0f});
    parts.field.setSearchIcon(true);
    parts.field.apply({.fillX = true, .height = 34.0f, .margin = {0.0f, 16.0f, 8.0f, 16.0f}});
    parts.list.apply({.fillX = true, .grow = scene::axes::kY});
    std::get<0>(parts.list.fChildren).apply({.fillX = true, .autoSize = scene::axes::kY});
    this->find({});
  }
  // The chats whose names have what is typed, in any case.
  void find(std::string_view text) {
    constexpr auto lower = mux::logic::folded;
    const std::string wanted = lower(text);
    auto& rows = std::get<0>(std::get<0>(parts.list.fChildren).fChildren);
    rows.clear();
    for (const forward_target& one : all)
      if (wanted.empty() || lower(one.name).contains(wanted))
        rows.emplace_back(actions, *colours_, one);
    parts.list.invalidateLayout();
    parts.list.scrollTo(0.0f);
  }
};

// Someone found: their picture, name and ID; pressed, the chat with them --
// in Start chat, and in the chat list where nothing joined matches.
template <class Actions>
struct found_person_row : nodes::Stack {
  Actions* actions;
  std::string id;
  struct lines_t : two_lines {
    lines_t(const palette& colours, const found_person& one) : two_lines(colours, one.name.empty() ? one.id : one.name, one.id, 14.0f, 2.0f) {}
  };
  struct parts_t {
    avatar_mark face;
    lines_t lines;
  } parts;
  found_person_row(Actions* a, const palette& colours, const found_person& one)
      : actions(a), id(one.id),
        parts{.face = avatar_mark(one.id, one.name.empty() ? one.id : one.name, 36.0f), .lines = lines_t(colours, one)} {
    this->setHorizontal();
    this->setGap(12.0f);
    fState.apply({.fillX = true, .height = 52.0f, .padding = {8.0f, 14.0f, 8.0f, 14.0f}, .cornerRadius = 8.0f,
                  .hoverBackground = colours.chosen});
    parts.face.apply({.alignSelf = scene::align::kMiddle});
  }
  [[nodiscard]] bool acceptsInput() const { return true; }
  [[nodiscard]] bool hoverChangesAppearance() const { return true; }
  [[nodiscard]] bool onClick(float, float) {
    actions->start_direct(id);
    return true;
  }
};

// Element's Start chat (its InviteDialog, for a direct chat): who to talk
// to, found as it is typed -- among those one already has chats with, and
// in the server's user directory -- each with their picture, name and ID,
// a press on one starting the chat; and one's own link, to send to someone
// not found.
template <class Actions>
struct start_chat_box : nodes::Stack {
  // The dialog it is shown in.
  [[nodiscard]] static dialog_look look_of_dialog() { return {.size = dialog_size::fixed{480.0f, 560.0f}}; }
  Actions* actions = nullptr;
  // The colours it is made in, for its parts and the rows it makes later.
  const palette* colours_ = nullptr;
  // Those one has direct chats with, and what the directory found for what
  // is typed now; one's own link.
  std::vector<found_person> known;
  std::vector<found_person> found;
  std::string query;
  std::string link;
  struct close_it {
    Actions* actions;
    void operator()() const { actions->close_new_chat(); }
  };
  struct typed {
    start_chat_box* box;
    void operator()(std::string_view text) const { box->search(text); }
  };
  struct go_press {
    start_chat_box* box;
    void operator()() const { box->go(); }
  };
  struct copy_press {
    start_chat_box* box;
    void operator()() const { box->actions->copy_text(box->link); }
  };
  using person_row = found_person_row<Actions>;
  using header_t = page_header<no_back, close_it>;
  using rows_t = nodes::Flow<std::vector<person_row>>;
  struct search_row : nodes::Stack {
    struct parts_t {
      widgets::TextBox<typed> field;
      widgets::Button<go_press> go;
    } parts;
    explicit search_row(start_chat_box* box)
        : parts{.field = widgets::TextBox<typed>(box->colours_->widgets, "Search", {box}),
                .go = widgets::Button<go_press>(box->colours_->widgets, "Go", {box})} {
      this->setHorizontal();
      this->setGap(8.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 10.0f, 0.0f, 10.0f}});
      parts.field.setSearchIcon(true);
      parts.field.apply({.height = 36.0f, .relativeSize = scene::axes::kNone, .grow = scene::axes::kX,
                         .alignSelf = scene::align::kMiddle});
      parts.go.setPrimary(true);
      parts.go.apply({.width = 64.0f, .height = 34.0f, .alignSelf = scene::align::kMiddle});
    }
  };
  struct link_row : nodes::Stack {
    struct parts_t {
      nodes::Text link;
      widgets::Button<copy_press> copy;
    } parts;
    link_row(start_chat_box* box, const std::string& link)
        : parts{.link = nodes::Text(link, 13.0f, box->colours_->accent),
                .copy = widgets::Button<copy_press>(box->colours_->widgets, "Copy", {box})} {
      this->setHorizontal();
      this->setGap(8.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {2.0f, 10.0f, 0.0f, 10.0f}});
      parts.link.setElided(true);
      parts.link.apply({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
      parts.copy.apply({.width = 70.0f, .height = 30.0f, .alignSelf = scene::align::kMiddle});
    }
  };
  struct parts_t {
    header_t header;
    nodes::Text intro;
    search_row search;
    nodes::Text status;
    nodes::ScrollContainer<rows_t> list{rows_t({.spacingY = 0.0f, .wrap = false}, {})};
    nodes::Text note;
    link_row share;
  } parts;
  start_chat_box(Actions* a, const palette& colours, std::vector<found_person> people, std::string own_link)
      : actions(a), colours_(&colours), known(std::move(people)), link(std::move(own_link)),
        parts{.header = header_t(colours, "Start chat", {}, {a}, false, true),
              .intro = nodes::Text("Start a conversation with someone using their name or username (like @user:server).",
                                   14.0f, colours.text),
              .search = search_row(this),
              .status = nodes::Text("Suggestions", 12.0f, colours.dim, true),
              .note = nodes::Text("If you can't see who you're looking for, send them your invite link below.", 13.0f,
                                  colours.dim),
              .share = link_row(this, link)} {
    this->setGap(8.0f);
    fState.apply({.fillX = true, .height = 560.0f, .padding = {0.0f, 12.0f, 16.0f, 12.0f}});
    parts.intro.setWrapped(true);
    parts.intro.apply({.fillX = true, .margin = {0.0f, 10.0f, 4.0f, 10.0f}});
    parts.status.apply({.margin = {6.0f, 10.0f, 0.0f, 10.0f}});
    parts.list.apply({.fillX = true, .grow = scene::axes::kY});
    std::get<0>(parts.list.fChildren).apply({.fillX = true, .autoSize = scene::axes::kY});
    parts.note.setWrapped(true);
    parts.note.apply({.fillX = true, .margin = {6.0f, 10.0f, 0.0f, 10.0f}});
    this->show_rows();
  }
  // Someone's whole address, as typed -- a person, as some protocol reads
  // it: offered as it is, first.
  [[nodiscard]] static bool whole_id(std::string_view text) {
    const auto link = logic::link_of_id(text);
    return link && spl::visit(spl::overloaded{[](const logic::mention::person&) { return true; },
                                                    [](const auto&) { return false; }},
                                 logic::mention_in(*link));
  }
  static constexpr auto lower = mux::logic::folded;
  void search(std::string_view text) {
    query = std::string(text);
    found.clear();
    this->show_rows();
    if (query.size() >= 2)
      actions->find_people(query);
  }
  // The directory's answer, where it is for what is typed now.
  void show_found(const std::vector<found_person>& people, const std::string& asked) {
    if (asked != query)
      return;
    found = people;
    for (const found_person& one : people)
      if (one.avatar && !one.avatar->empty())
        listed_avatars().emplace_back(one.id, *one.avatar);
    this->show_rows();
  }
  void show_rows() {
    auto& rows = std::get<0>(std::get<0>(parts.list.fChildren).fChildren);
    rows.clear();
    const std::string wanted = lower(query);
    std::set<std::string> listed;
    const auto add = [&](const found_person& one) {
      if (rows.size() < 60 && listed.insert(one.id).second)
        rows.emplace_back(actions, *colours_, one);
    };
    if (whole_id(query))
      add(found_person{.id = query});
    for (const found_person& one : known)
      if (wanted.empty() || lower(one.name).contains(wanted) || lower(one.id).contains(wanted))
        add(one);
    for (const found_person& one : found)
      add(one);
    parts.status.setText(query.empty() ? "Suggestions" : rows.empty() ? "No results" : "Results");
    parts.list.invalidateLayout();
    parts.list.scrollTo(0.0f);
  }
  // Go: the ID typed, or the first found.
  void go() {
    if (whole_id(query)) {
      actions->start_direct(query);
      return;
    }
    const auto& rows = std::get<0>(std::get<0>(parts.list.fChildren).fChildren);
    if (!rows.empty())
      actions->start_direct(rows.front().id);
  }
};

// Element's Create a room (its CreateRoomDialog): a name, a topic, who can
// join -- by invitation, or anyone, with the address it is found by -- and,
// among the advanced, whether those of other servers may ever join.
template <class Actions>
struct create_room_box : nodes::Stack {
  // The dialog it is shown in.
  [[nodiscard]] static dialog_look look_of_dialog() { return {.size = dialog_size::fitting{480.0f}}; }
  Actions* actions = nullptr;
  // The colours it is made in, for its parts.
  const palette* colours_ = nullptr;
  std::string server;
  bool open_room = false;
  bool federate = true;
  bool encrypted = true;  // as Element: on for a private room, off for a public one
  // Made in a space, where it is: its members let in by default, as
  // Element's "Visible to space members".
  std::optional<new_room_place> place;
  bool space_members = false;
  bool advanced = false;
  bool choosing = false;  // the list of who can join, open
  struct close_it {
    Actions* actions;
    void operator()() const { actions->close_new_room(); }
  };
  struct create_press {
    create_room_box* box;
    void operator()() const {
      const std::string& name = box->parts.name.text();
      if (!name.empty())
        box->actions->create_room(name, box->parts.topic.text(), box->open_room, box->parts.address.text(), box->federate,
                                  box->encrypted, box->place ? std::optional<conversation_id>(box->place->space) : std::nullopt,
                                  box->space_members, box->place && box->place->make_space);
    }
  };
  struct cancel_press {
    Actions* actions;
    void operator()() const { actions->close_new_room(); }
  };
  struct flip_list {
    create_room_box* box;
    void operator()() const {
      box->choosing = !box->choosing;
      box->show_choice();
    }
  };
  struct choose_members {
    create_room_box* box;
    void operator()() const {
      box->open_room = false;
      box->space_members = true;
      box->encrypted = true;
      box->choosing = false;
      box->show_choice();
    }
  };
  struct choose_private {
    create_room_box* box;
    void operator()() const {
      box->open_room = false;
      box->space_members = false;
      box->encrypted = true;
      box->choosing = false;
      box->show_choice();
    }
  };
  struct choose_public {
    create_room_box* box;
    void operator()() const {
      box->open_room = true;
      box->space_members = false;
      box->encrypted = false;
      box->choosing = false;
      box->show_choice();
    }
  };
  struct flip_advanced {
    create_room_box* box;
    void operator()() const {
      box->advanced = !box->advanced;
      box->show_choice();
    }
  };
  struct flip_federate {
    create_room_box* box;
    void operator()() const {
      box->federate = !box->federate;
      box->parts.block.parts.toggle.setOn(!box->federate);  // on: blocked
    }
  };
  struct flip_encrypted {
    create_room_box* box;
    void operator()() const {
      box->encrypted = !box->encrypted;
      box->show_choice();
    }
  };
  // Who can join, as Element's dropdown shows it: the choice and a chevron.
  struct choice_button : nodes::Stack {
    flip_list press;
    struct parts_t {
      nodes::Text value;
      nodes::Icon chevron;
    } parts;
    explicit choice_button(create_room_box* box)
        : press{box},
          parts{.value = nodes::Text("", 14.0f, box->colours_->text),
                .chevron = nodes::Icon(shape_of(icon::down{}), box->colours_->dim)} {
      this->setHorizontal();
      this->setGap(8.0f);
      fState.apply({.fillX = true, .height = 38.0f, .margin = {0.0f, 10.0f, 0.0f, 10.0f}, .padding = {0.0f, 12.0f, 0.0f, 12.0f},
                    .cornerRadius = 6.0f, .background = box->colours_->tile, .hoverBackground = box->colours_->chosen,
                    .border = scene::Border{box->colours_->band, 1.0f}});
      parts.value.apply({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
      parts.chevron.apply({.width = 16.0f, .height = 16.0f, .alignSelf = scene::align::kMiddle});
    }
    [[nodiscard]] bool acceptsInput() const { return true; }
    [[nodiscard]] bool hoverChangesAppearance() const { return true; }
    [[nodiscard]] bool onClick(float, float) {
      press();
      return true;
    }
  };
  // One of the choices, in the open list: its name and what it means.
  template <class Choose>
  struct option_row : nodes::Stack {
    Choose choose;
    struct parts_t {
      nodes::Text name;
      nodes::Text meaning;
    } parts;
    option_row(create_room_box* box, std::string name, std::string meaning)
        : choose{box}, parts{.name = nodes::Text(std::move(name), 14.0f, box->colours_->text, true),
                             .meaning = nodes::Text(std::move(meaning), 12.0f, box->colours_->dim)} {
      this->setGap(2.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY, .margin = {0.0f, 10.0f, 0.0f, 10.0f},
                    .padding = {8.0f, 12.0f, 8.0f, 12.0f}, .cornerRadius = 6.0f, .hoverBackground = box->colours_->chosen});
      parts.meaning.setWrapped(true);
      parts.meaning.apply({.fillX = true});
    }
    [[nodiscard]] bool acceptsInput() const { return true; }
    [[nodiscard]] bool hoverChangesAppearance() const { return true; }
    [[nodiscard]] bool onClick(float, float) {
      choose();
      return true;
    }
  };
  // A switch with what it does: blocking other servers, encrypting.
  template <class Flip>
  struct switch_row : nodes::Stack {
    struct parts_t {
      nodes::Text label;
      widgets::Toggle<Flip> toggle;
    } parts;
    switch_row(create_room_box* box, std::string label)
        : parts{.label = nodes::Text(std::move(label), 13.0f, box->colours_->text),
                .toggle = widgets::Toggle<Flip>(box->colours_->widgets, {box})} {
      this->setHorizontal();
      this->setGap(12.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {4.0f, 10.0f, 4.0f, 10.0f}});
      parts.label.setWrapped(true);
      parts.label.apply({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
      parts.toggle.apply({.alignSelf = scene::align::kMiddle});
    }
  };
  using buttons_row = dialog_buttons<cancel_press, create_press>;
  using header_t = page_header<no_back, close_it>;
  struct parts_t {
    header_t header;
    field name;
    field topic;
    nodes::Text rule_caption;
    choice_button rule;
    option_row<choose_private> private_option;
    option_row<choose_members> members_option;
    option_row<choose_public> public_option;
    nodes::Text rule_note;
    field address;
    switch_row<flip_encrypted> encryption;
    nodes::Text encryption_note;
    widgets::Button<flip_advanced> show_advanced;
    switch_row<flip_federate> block;
    nodes::Text block_note;
    buttons_row buttons;
  } parts;
  create_room_box(Actions* a, const palette& colours, std::string own_server, std::optional<new_room_place> where = std::nullopt)
      : actions(a), colours_(&colours), server(std::move(own_server)), place(std::move(where)), space_members(place.has_value()),
        parts{.header = header_t(colours, "Create a room", {}, {a}, false, true),
              .name = field(colours, "Name", ""),
              .topic = field(colours, "Topic (optional)", ""),
              .rule_caption = nodes::Text("Who can join", 13.0f, colours.dim),
              .rule = choice_button(this),
              .private_option = option_row<choose_private>(this, "Private room (invite only)",
                                                           "Only people invited will be able to find and join this room."),
              .members_option = option_row<choose_members>(
                  this, "Visible to space members",
                  place ? "Anyone in " + place->name + " will be able to find and join." : std::string()),
              .public_option = option_row<choose_public>(this, "Public room", "Anyone will be able to find and join this room."),
              .rule_note = nodes::Text("", 13.0f, colours.dim),
              .address = field(colours, "Address", std::format("#room-name:{}", server)),
              .encryption = switch_row<flip_encrypted>(this, "Enable end-to-end encryption"),
              .encryption_note = nodes::Text("", 12.0f, colours.dim),
              .show_advanced = widgets::Button<flip_advanced>(colours.widgets, "Show advanced", {this}),
              .block = switch_row<flip_federate>(
                  this, std::format("Block anyone not part of {} from ever joining this room.", server)),
              .block_note = nodes::Text("You might enable this if the room will only be used for collaborating with internal "
                                        "teams on your server. This cannot be changed later.",
                                        12.0f, colours.dim),
              .buttons = buttons_row(colours, "Create room", {a}, {this}, 120.0f)} {
    this->setGap(8.0f);
    parts.topic.multi_line(4);
    fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 12.0f, 18.0f, 12.0f}});
    parts.rule_caption.apply({.margin = {4.0f, 10.0f, 0.0f, 10.0f}});
    parts.rule_note.setWrapped(true);
    parts.rule_note.apply({.fillX = true, .margin = {0.0f, 10.0f, 0.0f, 10.0f}});
    parts.show_advanced.apply({.width = 150.0f, .height = 30.0f, .margin = {4.0f, 10.0f, 0.0f, 10.0f}});
    parts.block_note.setWrapped(true);
    parts.block_note.apply({.fillX = true, .margin = {0.0f, 10.0f, 0.0f, 10.0f}});
    parts.encryption_note.setWrapped(true);
    parts.encryption_note.apply({.fillX = true, .margin = {0.0f, 10.0f, 0.0f, 10.0f}});
    this->show_choice();
  }
  // What is shown for the choices made: the list open or not, the address
  // for a public room, the advanced part.
  void show_choice() {
    // What is made, and where: a room or a space, in a space or not.
    const std::string what = place && place->make_space ? "space" : "room";
    parts.header.parts.title.setText(place ? std::format("Create a {} in {}", what, place->name)
                                           : open_room ? "Create a public room" : "Create a room");
    parts.rule.parts.value.setText(open_room       ? "Public " + what
                                   : space_members ? std::string("Visible to space members")
                                                   : std::format("Private {} (invite only)", what));
    parts.private_option.setVisible(choosing);
    parts.members_option.setVisible(choosing && place.has_value());
    parts.public_option.setVisible(choosing);
    parts.rule_note.setText(open_room       ? std::format("Anyone will be able to find and join this {}.", what)
                            : space_members ? std::format("Anyone in {} will be able to find and join this {}.",
                                                          place ? place->name : std::string(), what)
                                            : std::format("Only people invited will be able to find and join this {}. You can "
                                                          "change this at any time from its settings.",
                                                          what));
    parts.address.setVisible(open_room);
    // A space has no messages to encrypt.
    parts.encryption.setVisible(!(place && place->make_space));
    parts.encryption_note.setVisible(!(place && place->make_space));
    parts.encryption.parts.toggle.setOn(encrypted);
    parts.encryption_note.setText(
        encrypted ? "Only those in the room will read its messages -- not the server. You can't turn this off later."
        : open_room
            ? "Not encrypted: a public room is for anyone to read. You can turn encryption on later, not off."
            : "Not encrypted: the server and anyone with access to it can read the messages. You can turn "
              "encryption on later, not off.");
    parts.show_advanced.setLabel(advanced ? "Hide advanced" : "Show advanced");
    parts.block.setVisible(advanced);
    parts.block_note.setVisible(advanced);
    this->invalidateLayout();
  }
};

}  // namespace mux::ui
