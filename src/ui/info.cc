// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:info -- A chat's info, and a person's.
export module mux.ui:info;

import std;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes.box;
import skiff.nodes.flow;
import skiff.nodes.icon;
import skiff.nodes.scroll;
import skiff.nodes.text;
import skiff.widgets.pill;
import skiff.widgets.button;
import skiff.widgets.textbox;
import skiff.widgets.textarea;
import mux.core;
import mux.config;
import mux.logic.links;
import :base;
import :icons;
import :controls;
import :themes;
import :names;
import :forms;

export namespace mux::ui {

// One of the square buttons of a chat's info: its icon over its name.
template <class Act>
struct action_tile : nodes::Stack {
  Act act;
  struct parts_t {
    icon_mark mark;
    nodes::Text label;
  } parts;

  // Declared: the icon at the top, the name at the bottom.
  action_tile(std::string text, icon_t icon, Act what = {})
      : act(std::move(what)), parts{.mark = icon_mark(icon), .label = nodes::Text(std::move(text), 12.0f, text_colour)} {
    auto& [mark, label] = parts;
    fState.apply({.height = 58.0f, .padding = {6.0f, 0.0f, 8.0f, 0.0f}, .cornerRadius = 8.0f, .background = tile_colour, .hoverBackground = chosen_colour, .focusBackground = chosen_colour});
    fStack.justify = nodes::justify::space_between{};
    mark.setColour(text_colour);
    mark.apply({.height = 24.0f});
    label.apply({.alignSelf = scene::align::kMiddle});
  }

  [[nodiscard]] bool acceptsInput() const { return true; }
  [[nodiscard]] bool hoverChangesAppearance() const { return true; }
  [[nodiscard]] bool focusChangesAppearance() const { return true; }
  [[nodiscard]] bool onClick(float, float) {
    act();
    return true;
  }
  [[nodiscard]] scene::Semantics semantics() const {
    scene::Semantics out;
    out.fRole = scene::semantic_role::button{};
    out.fLabel = parts.label.text();
    out.fActions = {scene::semantic_action::focus{}, scene::semantic_action::activate{}};
    return out;
  }
};

// Someone in a group, in its info: avatar, name, how they are, and their
// role in a pill. Pressed, they are shown on a page of their own.
template <class Open>
struct member_row : nodes::Stack {
  // Who it shows and how they are: while the same, the row is kept.
  member who;
  std::string how_shown;
  Open open;
  std::string id;
  std::optional<std::string> role;
  // Their name, and how they are under it.
  struct texts_column : nodes::Stack {
    struct parts_t {
      nodes::Text name;
      nodes::Text state;
    } parts;
    texts_column(std::string shown, std::string how)
        : parts{.name = nodes::Text(std::move(shown), 14.0f, text_colour, true),
                .state = nodes::Text(std::move(how), 12.0f, dim_colour)} {
      this->setGap(4.0f);
      fState.apply({.autoSize = scene::axes::kY, .grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
      for (nodes::Text* each : {&parts.name, &parts.state}) {
        each->setElided(true);
        each->apply({.fillX = true});
      }
    }
  };
  // Their role, in a pill beside their name.
  struct role_pill : widgets::Pill {
    explicit role_pill(std::string what)
        : widgets::Pill(std::move(what), {.plate = skia::colorSetARGB(255, 62, 52, 96),
                                          .text = skia::colorSetARGB(255, 190, 170, 250),
                                          .size = 12.0f,
                                          .height = 20.0f,
                                          .padX = 8.0f}) {
      fState.apply({.alignSelf = scene::align::kStart, .margin = {10.0f, 0.0f, 0.0f, 0.0f}});
    }
  };
  struct parts_t {
    avatar_mark face;
    texts_column texts;
    role_pill pill;
  } parts;

  // Declared: the avatar, the name over how they are, the role at the end.
  member_row(const member& one, std::string how, Open what)
      : who(one), how_shown(how), open(std::move(what)), id(one.id), role(one.role),
        parts{.face = avatar_mark(one.id, one.name.empty() ? one.id : one.name, 40.0f),
              .texts = texts_column(one.name.empty() ? one.id : one.name, std::move(how)),
              .pill = role_pill(one.role.value_or(""))} {
    this->setHorizontal();
    this->setGap(12.0f);
    fState.apply({.fillX = true, .height = 54.0f, .padding = {0.0f, 16.0f, 0.0f, 16.0f}, .hoverBackground = chosen_colour});
    parts.pill.setVisible(one.role.has_value());
  }
  [[nodiscard]] bool acceptsInput() const { return true; }
  [[nodiscard]] bool hoverChangesAppearance() const { return true; }
  [[nodiscard]] bool onClick(float, float) {
    open(*this);
    return true;
  }
};

// A round avatar on its own: the chat's, big, over its name.
struct big_avatar : avatar_mark {
  big_avatar() : avatar_mark(std::string(), std::string(), 96.0f) {}
};
// An icon on its own, not to be pressed.
struct icon_view : nodes::Icon {
  explicit icon_view(icon_t mark) : nodes::Icon(shape_of(mark), dim_colour) { fState.apply({.width = 28.0f, .height = 36.0f}); }
};
// A band between sections: just darker than the panel.
inline nodes::Box<> section_band() {
  nodes::Box<> out{section_colour};
  out.apply({.fillX = true, .height = 6.0f, .margin = {6.0f, 0.0f, 6.0f, 0.0f}});
  return out;
}

// A chat's info, beside it, as Telegram Desktop shows it: a big avatar, the
// name and who is in it, three square buttons, its ID, and its members.
// Declared: a column of these, nothing placed by hand.
// An ID, whole -- wrapped, never cut -- and copied when pressed: a chat's
// or a person's.
struct id_line : nodes::Stack {
  struct parts_t {
    nodes::Text id;
    nodes::Text label{"ID", 12.0f, dim_colour};
  } parts;
  std::string copied;
  bool a_link = false;  // what is copied is a link to it, not the ID
  id_line(std::string text, std::string link)
      : parts{.id = nodes::Text(text, 14.0f, accent_colour)}, copied(link.empty() ? text : link), a_link(!link.empty()) {
    this->setGap(2.0f);
    fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {8.0f, 20.0f, 8.0f, 20.0f}, .hoverBackground = chosen_colour, .focusBackground = chosen_colour});
    fState.setCursor(scene::cursor::hand{});
    parts.id.setWrapped(true);
    parts.id.apply({.fillX = true});
  }
  [[nodiscard]] bool acceptsInput() const { return true; }
  [[nodiscard]] bool hoverChangesAppearance() const { return true; }
  [[nodiscard]] bool onClick(float, float) {
    skiff::scene::setClipboardText(copied);
    parts.label.setText(a_link ? "ID · link copied, with its servers" : "ID · copied");
    return true;
  }
};

// A person's name and how they are, as a chat knows them: a member's, with
// their role; the other side of a direct chat; anyone else by their address.
struct person_facts {
  std::string name;
  std::string status;
};
[[nodiscard]] inline person_facts person_of(const conversation* in, const model& now, const account_id& account,
                                            const std::string& id) {
  person_facts out{id, presence_of(now, account, id)};
  if (in == nullptr)
    return out;
  if (const auto found = std::ranges::find(in->members, id, &member::id); found != in->members.end()) {
    if (!found->name.empty())
      out.name = found->name;
    if (found->role)
      out.status = out.status.empty() ? *found->role : std::format("{} · {}", out.status, *found->role);
  } else if (!is_group(*in) && id == contact_of(*in)) {
    out.name = display_name(*in);
  }
  return out;
}

// A person's info, as tdesktop's profile layer: a box in the middle of the
// window over the chats -- a bar with its title and ✕, their photo beside
// their name and how they are, their ID to copy, and a message to them.
template <class Actions>
struct person_card : nodes::Stack {
  struct message_them {
    Actions* actions = nullptr;
    conversation_id who;
    void operator()() const {
      actions->message_person(who);
      actions->close_person_info();
    }
  };
  using close_button = icon_button<ask<Actions, &Actions::close_person_info>>;
  struct top_bar : nodes::Stack {
    struct parts_t {
      nodes::Text title{"User info", 16.0f, text_colour, true};
      close_button close;
    } parts;
    explicit top_bar(Actions* a) : parts{.close = close_button(icon::close{}, {a})} {
      this->setHorizontal();
      fState.apply({.fillX = true, .height = 56.0f, .padding = {0.0f, 10.0f, 0.0f, 22.0f}});
      parts.title.apply({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
      parts.close.apply({.alignSelf = scene::align::kMiddle});
    }
  };
  // tdesktop's cover: 108 high, a 72 photo, the name and status beside it.
  struct cover : nodes::Stack {
    struct parts_t {
      avatar_button<Actions> photo;
      two_lines texts;
    } parts;
    cover(Actions* a, const std::string& key, const person_facts& facts)
        : parts{.photo = avatar_button<Actions>(a, key, facts.name, 72.0f),
                .texts = two_lines(facts.name, facts.status, 17.0f, 6.0f)} {
      this->setHorizontal();
      this->setGap(16.0f);
      fState.apply({.fillX = true, .height = 108.0f, .padding = {0.0f, 22.0f, 0.0f, 22.0f}});
    }
  };
  struct parts_t {
    top_bar top;
    cover face;
    nodes::Box<> band = section_band();
    id_line id;
    action_tile<message_them> message;
  } parts;

  person_card(Actions* a, const account_id& account, const std::string& key, const person_facts& facts)
      : parts{.top = top_bar(a),
              .face = cover(a, key, facts),
              .id = id_line(key, ""),
              .message = action_tile<message_them>("Message", icon::send{}, {a, conversation_id{account, key}})} {
    fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 0.0f, 16.0f, 0.0f}});
    parts.message.apply({.fillX = true, .margin = {8.0f, 22.0f, 0.0f, 22.0f}});
  }
};

// A room not joined, as a link names it, in the person card's layout: its
// photo beside its name and how many are in it, what it is about, its ID to
// copy, and a button to join it. What its server says (/room_summary) fills
// it when it comes; till then, or where it says nothing, the address alone.
template <class Actions>
struct room_card : nodes::Stack {
  struct join_it {
    Actions* actions = nullptr;
    void operator()() const { actions->join_room_card(); }
  };
  using close_button = icon_button<ask<Actions, &Actions::close_room_card>>;
  struct top_bar : nodes::Stack {
    struct parts_t {
      nodes::Text title{"Room info", 16.0f, text_colour, true};
      close_button close;
    } parts;
    explicit top_bar(Actions* a) : parts{.close = close_button(icon::close{}, {a})} {
      this->setHorizontal();
      fState.apply({.fillX = true, .height = 56.0f, .padding = {0.0f, 10.0f, 0.0f, 22.0f}});
      parts.title.apply({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
      parts.close.apply({.alignSelf = scene::align::kMiddle});
    }
  };
  struct cover : nodes::Stack {
    struct parts_t {
      avatar_mark photo;
      two_lines texts;
    } parts;
    cover(const std::string& key, const std::string& name, const std::string& line)
        : parts{.photo = avatar_mark(key, name, 72.0f), .texts = two_lines(name, line, 17.0f, 6.0f)} {
      this->setHorizontal();
      this->setGap(16.0f);
      fState.apply({.fillX = true, .height = 108.0f, .padding = {0.0f, 22.0f, 0.0f, 22.0f}});
    }
  };
  // Its name, else its address, else what the link said.
  static std::string name_of(const std::string& asked, const room_preview& known) {
    return !known.name.empty() ? known.name : !known.alias.empty() ? known.alias : asked;
  }
  // Under the name: the address, where the name is not it, and how many.
  static std::string line_of(const std::string& asked, const room_preview& known) {
    std::string out = known.alias.empty() ? (known.name.empty() ? std::string() : asked) : known.alias;
    if (out == name_of(asked, known))
      out.clear();
    if (known.members)
      out += std::format("{}{} {}", out.empty() ? "" : " · ", *known.members, *known.members == 1 ? "member" : "members");
    return out.empty() ? std::string("Matrix room") : out;
  }
  struct parts_t {
    top_bar top;
    cover face;
    nodes::Box<> band = section_band();
    nodes::Text about;
    id_line id;
    action_tile<join_it> join;
  } parts;

  room_card(Actions* a, const std::string& asked, const room_preview& known)
      : parts{.top = top_bar(a),
              .face = cover(known.id.empty() ? asked : known.id, name_of(asked, known), line_of(asked, known)),
              .about = nodes::Text(known.topic.empty() ? known.note : known.topic, 14.0f,
                                   known.topic.empty() ? dim_colour : text_colour),
              .id = id_line(known.id.empty() ? asked : known.id, ""),
              .join = action_tile<join_it>("Join", icon::plus{}, {a})} {
    fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 0.0f, 16.0f, 0.0f}});
    parts.about.setWrapped(true);
    parts.about.apply({.fillX = true, .margin = {2.0f, 22.0f, 8.0f, 22.0f}});
    parts.join.apply({.fillX = true, .margin = {8.0f, 22.0f, 0.0f, 22.0f}});
  }
};

// A reaction as the event it is: who, with what, when.
struct reaction_entry {
  std::string event;
  std::string who;
  std::string name;
  std::string key;
  std::string when;
};

// A message's reactions as events, as Matrix has them: a box in the middle,
// a row for each -- the person's avatar and name over what they reacted
// with and when. A press on one answers it: the reaction is an event, and a
// message can reply to it.
template <class Actions>
struct reactions_box : nodes::Stack {
  using close_button = icon_button<ask<Actions, &Actions::close_reactions>>;
  struct top_bar : nodes::Stack {
    struct parts_t {
      nodes::Text title{"Reactions", 16.0f, text_colour, true};
      close_button close;
    } parts;
    explicit top_bar(Actions* a) : parts{.close = close_button(icon::close{}, {a})} {
      this->setHorizontal();
      fState.apply({.fillX = true, .height = 56.0f, .padding = {0.0f, 10.0f, 0.0f, 22.0f}});
      parts.title.apply({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
      parts.close.apply({.alignSelf = scene::align::kMiddle});
    }
  };
  struct row : nodes::Stack {
    Actions* actions;
    reaction_entry entry;
    struct parts_t {
      avatar_mark face;
      two_lines texts;
    } parts;
    row(Actions* a, reaction_entry one)
        : actions(a), entry(one),
          parts{.face = avatar_mark(one.who, one.name, 36.0f),
                .texts = two_lines(one.name, std::format("reacted {} · {}", one.key, one.when), 14.0f, 2.0f)} {
      this->setHorizontal();
      this->setGap(12.0f);
      fState.apply({.fillX = true, .height = 52.0f, .padding = {0.0f, 22.0f, 0.0f, 22.0f},
                    .hoverBackground = chosen_colour});
      fState.setCursor(scene::cursor::hand{});
    }
    [[nodiscard]] bool acceptsInput() const { return true; }
    [[nodiscard]] bool hoverChangesAppearance() const { return true; }
    [[nodiscard]] bool onClick(float, float) {
      actions->reply_to(entry.event, std::format("{} reacted {}", entry.name, entry.key));
      actions->close_reactions();
      return true;
    }
  };
  using rows_t = nodes::Flow<std::vector<row>>;
  struct parts_t {
    top_bar top;
    nodes::ScrollContainer<rows_t> list{rows_t({.spacingY = 0.0f, .wrap = false}, {})};
  } parts;

  reactions_box(Actions* a, const std::vector<reaction_entry>& entries) : parts{.top = top_bar(a)} {
    fState.apply({.fillX = true, .height = 420.0f, .padding = {0.0f, 0.0f, 12.0f, 0.0f}});
    parts.list.apply({.fillX = true, .grow = scene::axes::kY});
    auto& flow = std::get<0>(parts.list.fChildren);
    flow.apply({.fillX = true, .autoSize = scene::axes::kY});
    auto& rows = std::get<0>(flow.fChildren);
    rows.reserve(entries.size());
    for (const reaction_entry& one : entries)
      rows.emplace_back(a, one);
  }
};

// The developer tools, as Element's: some JSON to read and copy; a room's
// state, by type, then by key, then the event; an event of any type sent.
template <class Actions>
struct devtools_box : nodes::Stack {
  Actions* actions = nullptr;
  struct close_it {
    Actions* actions;
    void operator()() const { actions->close_devtools(); }
  };
  struct back_up {
    devtools_box* box;
    void operator()() const { box->go_back(); }
  };
  using header_t = page_header<back_up, close_it>;
  // A line of the state's list: a type, or a key of one; pressed, what is
  // under it, at the next frame -- not from inside the list it is in.
  struct pick {
    devtools_box* box;
    std::string type;
    std::optional<std::string> key;
    void operator()() const { box->pending = pick{box, type, key}; }
  };
  struct send_press {
    devtools_box* box;
    void operator()() const { box->send(); }
  };
  using entry_row = row_item<pick>;
  using rows_t = nodes::Flow<std::vector<entry_row>>;
  struct form : nodes::Stack {
    struct parts_t {
      field type;
      field key;
      nodes::Text body_caption{"Content (a JSON object)", 13.0f, dim_colour};
      widgets::TextArea<> body;
      widgets::Button<send_press> send;
    } parts;
    explicit form(devtools_box* box)
        : parts{.type = field("Event type", "m.room.message"),
                .key = field("State key (for a state event; empty for a timeline one)", ""),
                .body = widgets::TextArea<>("{}"),
                .send = widgets::Button<send_press>("Send", {box})} {
      this->setGap(8.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 12.0f, 12.0f, 12.0f}});
      parts.body_caption.apply({.margin = {0.0f, 10.0f, 0.0f, 10.0f}});
      parts.body.apply({.fillX = true, .height = 180.0f, .margin = {0.0f, 10.0f, 0.0f, 10.0f}, .cornerRadius = 6.0f,
                        .background = tile_colour, .border = scene::Border{band_colour, 1.0f}});
      parts.body.setText("{\n  \n}");
      parts.send.setPrimary(true);
      parts.send.apply({.width = 120.0f, .height = 34.0f, .margin = {0.0f, 10.0f, 0.0f, 10.0f}});
    }
  };
  struct parts_t {
    header_t header;
    nodes::ScrollContainer<nodes::Text> reading{nodes::Text("", 13.0f, text_colour)};
    nodes::ScrollContainer<rows_t> list{rows_t({.spacingY = 0.0f, .wrap = false}, {})};
    std::optional<form> sending;
  } parts;
  // What it shows: some text, or the state -- all its events -- at a level.
  std::vector<change::state_entry> state;
  std::optional<std::string> type_shown;
  bool showing_state = false;
  std::optional<pick> pending;

  devtools_box(Actions* a, std::string title, std::string text)
      : actions(a), parts{.header = header_t(std::move(title), {this}, {a}, false, true)} {
    this->lay_out();
    this->show_text(std::move(text));
  }
  devtools_box(Actions* a, std::vector<change::state_entry> entries)
      : actions(a), parts{.header = header_t("Room state", {this}, {a}, false, true)}, state(std::move(entries)) {
    this->lay_out();
    this->show_types();
  }
  struct send_form_t {};
  devtools_box(Actions* a, send_form_t) : actions(a), parts{.header = header_t("Send custom event", {this}, {a}, false, true)} {
    this->lay_out();
    parts.sending.emplace(this);
    parts.reading.setVisible(false);
    parts.list.setVisible(false);
  }
  void lay_out() {
    fState.apply({.fillX = true, .height = 560.0f});
    for (scene::Node* each : std::initializer_list<scene::Node*>{&parts.reading, &parts.list})
      each->apply({.fillX = true, .grow = scene::axes::kY});
    auto& text = std::get<0>(parts.reading.fChildren);
    text.setWrapped(true);
    text.setSelectable(true);
    text.apply({.fillX = true, .padding = {6.0f, 16.0f, 12.0f, 16.0f}});
    std::get<0>(parts.list.fChildren).apply({.fillX = true, .autoSize = scene::axes::kY});
  }
  void show_text(std::string text) {
    std::get<0>(parts.reading.fChildren).setText(std::move(text));
    parts.reading.setVisible(true);
    parts.list.setVisible(false);
    parts.reading.scrollTo(0.0f);
    this->invalidateLayout();
  }
  void show_rows(std::vector<std::pair<std::string, pick>> rows) {
    auto& all = std::get<0>(std::get<0>(parts.list.fChildren).fChildren);
    all.clear();
    for (auto& [label, what] : rows)
      all.emplace_back(std::move(label), std::move(what));
    parts.reading.setVisible(false);
    parts.list.setVisible(true);
    parts.list.scrollTo(0.0f);
    this->invalidateLayout();
  }
  // Every type, and how many events of it.
  void show_types() {
    showing_state = true;
    type_shown.reset();
    parts.header.parts.back.setVisible(false);
    std::map<std::string, std::size_t> counts;
    for (const change::state_entry& one : state)
      ++counts[one.type];
    std::vector<std::pair<std::string, pick>> rows;
    for (const auto& [type, count] : counts)
      rows.emplace_back(std::format("{}  ({})", type, count), pick{this, type, std::nullopt});
    this->show_rows(std::move(rows));
  }
  // A type's keys.
  void show_keys(const std::string& type) {
    type_shown = type;
    parts.header.parts.back.setVisible(true);
    std::vector<std::pair<std::string, pick>> rows;
    for (const change::state_entry& one : state)
      if (one.type == type)
        rows.emplace_back(one.key.empty() ? std::string("(empty key)") : one.key, pick{this, type, one.key});
    this->show_rows(std::move(rows));
  }
  void go_back() {
    if (!showing_state)
      return;
    if (!parts.reading.visible() && type_shown)
      this->show_types();
    else if (type_shown)
      this->show_keys(*type_shown);
  }
  void update(double) {
    if (!pending)
      return;
    const pick what = *std::exchange(pending, std::nullopt);
    if (!what.key) {
      this->show_keys(what.type);
      return;
    }
    for (const change::state_entry& one : state)
      if (one.type == what.type && one.key == *what.key) {
        parts.header.parts.back.setVisible(true);
        this->show_text(one.json);
        return;
      }
  }
  void send() {
    if (!parts.sending)
      return;
    auto& [type, key, caption, body, button] = parts.sending->parts;
    if (type.text().empty())
      return;
    actions->send_custom(type.text(), key.text().empty() ? std::nullopt : std::optional<std::string>(key.text()),
                         body.text());
  }
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
  Actions* actions = nullptr;
  std::vector<forward_target> all;
  struct close_it {
    Actions* actions;
    void operator()() const { actions->close_forward(); }
  };
  struct nothing_back {
    void operator()() const {}
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
    row(Actions* a, const forward_target& one)
        : actions(a), id(one.id), parts{.face = avatar_mark(one.id.id, one.name, 36.0f),
                                        .name = nodes::Text(one.name, 15.0f, text_colour)} {
      this->setHorizontal();
      this->setGap(12.0f);
      fState.apply({.fillX = true, .height = 50.0f, .padding = {0.0f, 20.0f, 0.0f, 20.0f},
                    .hoverBackground = chosen_colour});
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
  using header_t = page_header<nothing_back, close_it>;
  struct parts_t {
    header_t header;
    widgets::TextBox<typed> field;
    nodes::ScrollContainer<rows_t> list{rows_t({.spacingY = 0.0f, .wrap = false}, {})};
  } parts;

  forward_box(Actions* a, const std::vector<forward_target>& chats)
      : actions(a), all(chats),
        parts{.header = header_t("Forward to…", {}, {a}, false, true), .field = widgets::TextBox<typed>("Search", {this})} {
    fState.apply({.fillX = true, .height = 520.0f});
    parts.field.setSearchIcon(true);
    parts.field.apply({.fillX = true, .height = 34.0f, .margin = {0.0f, 16.0f, 8.0f, 16.0f}});
    parts.list.apply({.fillX = true, .grow = scene::axes::kY});
    std::get<0>(parts.list.fChildren).apply({.fillX = true, .autoSize = scene::axes::kY});
    this->find({});
  }
  // The chats whose names have what is typed, in any case.
  void find(std::string_view text) {
    const auto lower = [](std::string_view s) {
      std::string out(s);
      for (char& c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      return out;
    };
    const std::string wanted = lower(text);
    auto& rows = std::get<0>(std::get<0>(parts.list.fChildren).fChildren);
    rows.clear();
    for (const forward_target& one : all)
      if (wanted.empty() || lower(one.name).contains(wanted))
        rows.emplace_back(actions, one);
    parts.list.invalidateLayout();
    parts.list.scrollTo(0.0f);
  }
};

// A new chat, as tdesktop's New Message and New Group: someone's address,
// for a direct chat with them; or a name, for a group.
template <class Actions>
struct new_chat_box : nodes::Stack {
  Actions* actions = nullptr;
  struct close_it {
    Actions* actions;
    void operator()() const { actions->close_new_chat(); }
  };
  struct nothing_back {
    void operator()() const {}
  };
  struct direct_press {
    new_chat_box* box;
    void operator()() const {
      const std::string& user = box->parts.person.text();
      if (!user.empty())
        box->actions->start_direct(user);
    }
  };
  struct group_press {
    new_chat_box* box;
    void operator()() const {
      const std::string& name = box->parts.group.text();
      if (!name.empty())
        box->actions->start_group(name);
    }
  };
  using header_t = page_header<nothing_back, close_it>;
  struct parts_t {
    header_t header;
    field person;
    widgets::Button<direct_press> message;
    field group;
    widgets::Button<group_press> create;
    nodes::Text note{"A direct chat invites them at once; a group is private, and people are invited to it from "
                     "Manage in its info.",
                     13.0f, dim_colour};
  } parts;
  explicit new_chat_box(Actions* a)
      : actions(a),
        parts{.header = header_t("New chat", {}, {a}, false, true),
              .person = field("Message someone", "@someone:server"),
              .message = widgets::Button<direct_press>("Message", {this}),
              .group = field("New group", "The group's name"),
              .create = widgets::Button<group_press>("Create group", {this})} {
    this->setGap(8.0f);
    fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 12.0f, 18.0f, 12.0f}});
    parts.message.setPrimary(true);
    for (scene::Node* button : std::initializer_list<scene::Node*>{&parts.message, &parts.create})
      button->apply({.width = 140.0f, .height = 34.0f, .margin = {0.0f, 10.0f, 8.0f, 10.0f}});
    parts.note.setWrapped(true);
    parts.note.apply({.fillX = true, .margin = {6.0f, 10.0f, 0.0f, 10.0f}});
  }
};

// What the management of a room shows: as it is now.
struct manage_facts {
  std::string name;
  std::string topic;
  join_rule_t join_rule = join_rule::invite{};
  history_rule_t history = history_rule::shared{};
  struct person {
    std::string id;
    std::string name;
    std::int64_t level = 0;
  };
  std::vector<person> members;
};
[[nodiscard]] inline std::string role_of(std::int64_t level) {
  if (level >= 100)
    return "Admin";
  if (level >= 50)
    return "Moderator";
  return level > 0 ? std::format("Level {}", level) : std::string("Member");
}

// A room's management, as tdesktop's and Element's room settings: its name
// and topic to change; who may join it and read its history; someone to
// invite; and its members, each with their role -- made a moderator or an
// admin, or back to a member; removed; banned. What is done is asked of the
// program, which asks the server; the box shows what was so when it opened.
template <class Actions>
struct room_manage : nodes::Stack {
  Actions* actions = nullptr;
  struct close_it {
    Actions* actions;
    void operator()() const { actions->close_manage(); }
  };
  struct nothing_back {
    void operator()() const {}
  };
  // A field's text, done with: the name, the topic, someone invited. The
  // field is found through the box when pressed -- not held by address, for
  // the column it is in is moved into its scroll container once made.
  template <class Make>
  struct from_field {
    room_manage* box;
    void operator()() const {
      const std::string& text = Make::read(box->content());
      if (!text.empty() || Make::may_be_empty)
        box->actions->room_act(Make{}(text));
    }
  };
  struct make_name {
    static constexpr bool may_be_empty = true;
    template <class Column>
    static const std::string& read(Column& c) { return c.parts.name.text(); }
    room_action_t operator()(const std::string& text) const { return room_action::rename{text}; }
  };
  struct make_topic {
    static constexpr bool may_be_empty = true;
    template <class Column>
    static const std::string& read(Column& c) { return c.parts.topic.text(); }
    room_action_t operator()(const std::string& text) const { return room_action::retopic{text}; }
  };
  struct make_invite {
    static constexpr bool may_be_empty = false;
    template <class Column>
    static const std::string& read(Column& c) { return c.parts.invitee.text(); }
    room_action_t operator()(const std::string& text) const { return room_action::invite{text}; }
  };
  // One of a choice: a join rule or a history rule, set.
  template <class Rule>
  struct choose {
    room_manage* box;
    Rule rule;
    void operator()() const { box->chose(rule); }
  };
  // Something done to a member.
  struct to_member {
    Actions* actions;
    room_action_t action;
    void operator()() const { actions->room_act(action); }
  };
  struct member_row : nodes::Stack {
    using act_button = segment<to_member>;
    struct parts_t {
      avatar_mark face;
      two_lines texts;
      act_button role;
      act_button remove;
      act_button ban;
    } parts;
    member_row(Actions* a, const manage_facts::person& one)
        : parts{.face = avatar_mark(one.id, one.name, 32.0f),
                .texts = two_lines(one.name, role_of(one.level), 14.0f, 2.0f),
                .role = act_button(one.level >= 50 ? (one.level >= 100 ? "Member" : "Admin") : "Moderator",
                                   {a, room_action::set_power{one.id, one.level >= 50 ? (one.level >= 100 ? 0 : 100) : 50}}),
                .remove = act_button("Remove", {a, room_action::kick{one.id}}),
                .ban = act_button("Ban", {a, room_action::ban{one.id}})} {
      this->setHorizontal();
      this->setGap(8.0f);
      fState.apply({.fillX = true, .height = 48.0f, .padding = {0.0f, 16.0f, 0.0f, 16.0f}});
      for (act_button* each : {&parts.role, &parts.remove, &parts.ban})
        each->apply({.width = 84.0f, .alignSelf = scene::align::kMiddle, .cornerRadius = 6.0f});
    }
  };
  using header_t = page_header<nothing_back, close_it>;
  using name_save = widgets::Button<from_field<make_name>>;
  using topic_save = widgets::Button<from_field<make_topic>>;
  using invite_button = widgets::Button<from_field<make_invite>>;
  using join_segment = segment<choose<join_rule_t>>;
  using history_segment = segment<choose<history_rule_t>>;
  struct row_of_segments_join : nodes::Stack {
    struct parts_t {
      join_segment open, invite, knock;
    } parts;
    row_of_segments_join(room_manage* box)
        : parts{.open = join_segment("Public", {box, join_rule::open{}}),
                .invite = join_segment("Invite only", {box, join_rule::invite{}}),
                .knock = join_segment("Ask to join", {box, join_rule::knock{}})} {
      this->setHorizontal();
      this->setGap(4.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY, .margin = {0.0f, 10.0f, 0.0f, 10.0f}});
    }
  };
  struct row_of_segments_history : nodes::Stack {
    struct parts_t {
      history_segment shared, invited, joined, anyone;
    } parts;
    row_of_segments_history(room_manage* box)
        : parts{.shared = history_segment("Members", {box, history_rule::shared{}}),
                .invited = history_segment("Since invited", {box, history_rule::invited{}}),
                .joined = history_segment("Since joined", {box, history_rule::joined{}}),
                .anyone = history_segment("Anyone", {box, history_rule::world_readable{}})} {
      this->setHorizontal();
      this->setGap(4.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY, .margin = {0.0f, 10.0f, 0.0f, 10.0f}});
    }
  };
  struct column : nodes::Stack {
    struct parts_t {
      field name;
      name_save save_name;
      field topic;
      topic_save save_topic;
      nodes::Text join_title{"Who can join", 13.0f, dim_colour, true};
      row_of_segments_join join;
      nodes::Text history_title{"Who can read the history", 13.0f, dim_colour, true};
      row_of_segments_history history;
      field invitee;
      invite_button invite;
      nodes::Text members_title{"Members", 13.0f, dim_colour, true};
      std::vector<member_row> members;
      nodes::Text tools_title{"Developer tools", 13.0f, dim_colour, true};
      widgets::Button<ask<Actions, &Actions::explore_state>> explore;
      widgets::Button<ask<Actions, &Actions::open_send_custom>> send_custom;
    } parts;
    column(Actions* a, room_manage* box, const manage_facts& facts)
        : parts{.name = field("Name", "The room's name", facts.name),
                .save_name = name_save("Save name", {box}),
                .topic = field("Topic", "What the room is about", facts.topic),
                .save_topic = topic_save("Save topic", {box}),
                .join = row_of_segments_join(box),
                .history = row_of_segments_history(box),
                .invitee = field("Invite", "@someone:server"),
                .invite = invite_button("Invite", {box}),
                .explore = widgets::Button<ask<Actions, &Actions::explore_state>>("Explore room state", {a}),
                .send_custom = widgets::Button<ask<Actions, &Actions::open_send_custom>>("Send custom event", {a})} {
      this->setGap(8.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {8.0f, 12.0f, 16.0f, 12.0f}});
      for (auto* button : std::initializer_list<scene::Node*>{&parts.save_name, &parts.save_topic, &parts.invite})
        button->apply({.width = 130.0f, .height = 32.0f, .margin = {0.0f, 10.0f, 0.0f, 10.0f}});
      for (nodes::Text* title : {&parts.join_title, &parts.history_title, &parts.members_title})
        title->apply({.margin = {10.0f, 10.0f, 0.0f, 10.0f}});
      parts.members.reserve(facts.members.size());
      for (const auto& one : facts.members)
        parts.members.emplace_back(a, one);
      parts.tools_title.apply({.margin = {10.0f, 10.0f, 0.0f, 10.0f}});
      for (scene::Node* button : std::initializer_list<scene::Node*>{&parts.explore, &parts.send_custom})
        button->apply({.width = 180.0f, .height = 32.0f, .margin = {0.0f, 10.0f, 0.0f, 10.0f}});
    }
  };
  struct parts_t {
    header_t header;
    nodes::ScrollContainer<column> body;
  } parts;

  room_manage(Actions* a, const manage_facts& facts)
      : actions(a),
        parts{.header = header_t("Manage room", {}, {a}, false, true),
              .body = nodes::ScrollContainer<column>(column(a, this, facts))} {
    fState.apply({.fillX = true, .height = 600.0f});
    parts.body.apply({.fillX = true, .grow = scene::axes::kY});
    join_shown = facts.join_rule;
    history_shown = facts.history;
    this->show_rules(facts.join_rule, facts.history);
  }
  [[nodiscard]] column& content() { return std::get<0>(parts.body.fChildren); }
  // The chosen rules lit.
  void show_rules(const join_rule_t& join, const history_rule_t& history) {
    auto& [open, invite, knock] = content().parts.join.parts;
    open.set_active(std::visit(overloaded{[](join_rule::open) { return true; }, [](const auto&) { return false; }}, join));
    invite.set_active(std::visit(overloaded{[](join_rule::invite) { return true; }, [](const auto&) { return false; }}, join));
    knock.set_active(std::visit(overloaded{[](join_rule::knock) { return true; }, [](const auto&) { return false; }}, join));
    auto& [shared, invited, joined, anyone] = content().parts.history.parts;
    shared.set_active(std::visit(overloaded{[](history_rule::shared) { return true; }, [](const auto&) { return false; }}, history));
    invited.set_active(std::visit(overloaded{[](history_rule::invited) { return true; }, [](const auto&) { return false; }}, history));
    joined.set_active(std::visit(overloaded{[](history_rule::joined) { return true; }, [](const auto&) { return false; }}, history));
    anyone.set_active(std::visit(overloaded{[](history_rule::world_readable) { return true; }, [](const auto&) { return false; }}, history));
  }
  join_rule_t join_shown = join_rule::invite{};
  history_rule_t history_shown = history_rule::shared{};
  void chose(const join_rule_t& rule) {
    join_shown = rule;
    actions->room_act(room_action::set_join_rule{rule});
    this->show_rules(join_shown, history_shown);
  }
  void chose(const history_rule_t& rule) {
    history_shown = rule;
    actions->room_act(room_action::set_history{rule});
    this->show_rules(join_shown, history_shown);
  }
};

template <class Actions>
struct info_panel : nodes::Stack {
  Actions* actions = nullptr;
  account_id account;
  std::string key;
  // The member shown on a page of their own, over the group's, if one is:
  // the panel's own state, which the page is made from.
  std::optional<std::string> person;
  // The members as last shown, and how each is.
  std::vector<std::pair<member, std::string>> shown_members;
  // Whose members those are, at which revision of them.
  std::optional<conversation_id> members_of;
  std::uint64_t members_revision = 0;

  // What a press does, to the panel -- which stays where it is while its
  // pages are made again.
  struct open_person {
    info_panel* panel;
    void operator()(const auto& row) const { panel->actions->open_member_info(row.id); }
  };
  struct back_to_group {
    info_panel* panel;
    void operator()() const { panel->close_member(); }
  };
  struct message_them {
    Actions* actions;
    info_panel* panel;
    void operator()() const {
      if (panel->person)
        actions->message_person(conversation_id{panel->account, *panel->person});
    }
  };

  // What the upper part shows: a chat's, or one of its members'.
  struct view {
    std::string key;
    std::string name;
    std::string status;
    bool group = false;
    bool muted = false;
    bool of_person = false;
    // What copying the ID gives: for a Matrix room, a link to it with the
    // servers to join through; else the ID.
    std::string copied;
    friend bool operator==(const view&, const view&) = default;
  };

  // The upper part, made from its view: ← where a member is shown, ✕; the
  // big avatar, the name, how it is; the chat's tiles or the member's; its ID.
  struct head : nodes::Stack {
    struct top_row : nodes::Stack {
      using close_button = icon_button<ask<Actions, &Actions::toggle_info>>;
      struct parts_t {
        icon_button<back_to_group> back;
        nodes::Box<> gap{skia::colorSetARGB(0, 0, 0, 0)};
        close_button close;
      } parts;
      top_row(Actions* a, info_panel* panel, bool with_back)
          : parts{.back = icon_button<back_to_group>(icon::back{}, {panel}), .close = close_button(icon::close{}, {a})} {
        this->setHorizontal();
        fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {8.0f, 8.0f, 0.0f, 8.0f}});
        parts.gap.apply({.height = 1.0f, .grow = scene::axes::kX});
        parts.back.setVisible(with_back);
      }
    };
    struct tiles_row : nodes::Stack {
      using mute_tile = action_tile<ask<Actions, &Actions::toggle_mute>>;
      using manage_tile = action_tile<ask<Actions, &Actions::open_manage>>;
      using leave_tile = action_tile<ask<Actions, &Actions::leave_chat>>;
      using events_tile = action_tile<ask<Actions, &Actions::flip_chat_room_events>>;
      struct parts_t {
        mute_tile mute;
        events_tile events;
        manage_tile manage;
        leave_tile leave;
      } parts;
      tiles_row(Actions* a, bool muted)
          : parts{.mute = mute_tile(muted ? "Unmute" : "Mute", icon::bell{}, {a}),
                .events = events_tile("Events", icon::info{}, {a}),
                  .manage = manage_tile("Manage", icon::sliders{}, {a}),
                  .leave = leave_tile("Leave", icon::leave{}, {a})} {
        this->setHorizontal();
        this->setGap(8.0f);
        fState.apply({.fillX = true, .autoSize = scene::axes::kY, .margin = {16.0f, 16.0f, 4.0f, 16.0f}});
        parts.mute.apply({.grow = scene::axes::kX});
        parts.events.apply({.grow = scene::axes::kX});
        parts.manage.apply({.grow = scene::axes::kX});
        parts.leave.apply({.grow = scene::axes::kX});
      }
    };
    // A member's own: a message to them.
    struct person_row : nodes::Stack {
      struct parts_t {
        action_tile<message_them> message;
      } parts;
      person_row(Actions* a, info_panel* panel)
          : parts{.message = action_tile<message_them>("Message", icon::send{}, {a, panel})} {
        this->setHorizontal();
        fState.apply({.fillX = true, .autoSize = scene::axes::kY, .margin = {16.0f, 16.0f, 4.0f, 16.0f}});
        parts.message.apply({.grow = scene::axes::kX});
      }
    };
    struct parts_t {
      top_row top;
      avatar_button<Actions> avatar;
      nodes::Text name;
      nodes::Text status;
      std::optional<tiles_row> tiles;
      std::optional<person_row> person_tiles;
      nodes::Box<> band_1 = section_band();
      id_line id_text;
    } parts;

    head(Actions* a, info_panel* panel, const view& shown)
        : parts{.top = top_row(a, panel, shown.of_person),
                .avatar = avatar_button<Actions>(a, shown.key, shown.name, 96.0f),
                .name = nodes::Text(shown.name, 17.0f, text_colour, true),
                .status = nodes::Text(shown.status, 13.0f, dim_colour),
                .id_text = id_line(shown.key, shown.copied)} {
      auto& [top, avatar, name, status, tiles, person_tiles, band_1, id_text] = parts;
      this->setGap(2.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY});
      if (shown.of_person)
        person_tiles.emplace(a, panel);
      else
        tiles.emplace(a, shown.muted);
      for (nodes::Text* centred : {&name, &status}) {
        centred->setElided(true);
        centred->apply({.alignSelf = scene::align::kMiddle, .margin = {4.0f, 20.0f, 0.0f, 20.0f}});
      }
    }
  };
  struct members_head : nodes::Stack {
    using add_button = icon_button<not_yet<Actions>>;
    struct parts_t {
      icon_view people{icon::people{}};
      nodes::Text title;
      add_button add_member;
    } parts;
    members_head(Actions* a, std::size_t count)
        : parts{.title = nodes::Text(std::format("{} MEMBER{}", count, count == 1 ? "" : "S"), 13.0f, dim_colour, true),
                .add_member = add_button(icon::add_person{}, {a, "Adding members"})} {
      this->setHorizontal();
      this->setGap(10.0f);
      fState.apply({.fill = true, .padding = {6.0f, 10.0f, 6.0f, 16.0f}});
      parts.title.apply({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
    }
  };
  using member_list = nodes::ScrollContainer<nodes::Flow<std::vector<member_row<open_person>>>>;
  struct parts_t {
    nodes::Memo<view, head> upper;
    nodes::Box<> band_2 = section_band();
    // The members' head, as a function of how many there are.
    nodes::Memo<std::size_t, members_head> members_header;
    // The members, in a list of their own that scrolls, reconciled: its
    // place and its rows kept while they show the same.
    member_list members{nodes::Flow<std::vector<member_row<open_person>>>({.spacingY = 0.0f, .wrap = false}, {})};
    nodes::Box<> edge{band_colour};  // its left edge
  } parts;
  nodes::Memo<view, head>& upper = parts.upper;
  nodes::Box<>& band_2 = parts.band_2;
  nodes::Memo<std::size_t, members_head>& members_header = parts.members_header;
  member_list& members = parts.members;
  // The group's view, to come back to from a member's page.
  view group_view;

  static constexpr float kWidth = 340.0f;

  explicit info_panel(Actions* a) : actions(a) {
    fState.apply({.background = sidebar_colour, .masking = true});
    parts.edge.apply({.place = scene::anchor::kTopLeft, .fillY = true, .width = 1.0f});
    this->setGap(2.0f);
    upper.apply({.fillX = true, .autoSize = scene::axes::kY});
    members_header.apply({.fillX = true, .height = 48.0f});
    members.apply({.fillX = true, .grow = scene::axes::kY});
    std::get<0>(members.fChildren).apply({.fillX = true, .autoSize = scene::axes::kY});
  }

  // The chat shown: its view worked out, its members reconciled.
  void show(const conversation& one, const model& now, bool muted) {
    if (one.id.id != key || one.id.account != account)
      person.reset();
    key = one.id.id;
    account = one.id.account;
    const bool group = is_group(one);
    group_view = {one.id.id,
                  display_name(one),
                  group ? std::format("{} member{}", std::max<std::int64_t>(static_cast<std::int64_t>(one.members.size()), one.member_count),
                                      std::max<std::int64_t>(static_cast<std::int64_t>(one.members.size()), one.member_count) == 1 ? "" : "s")
                        : presence_of(now, one.id.account, contact_of(one)),
                  group,
                  muted,
                  false};
    if (group && is_matrix(one.id.account.speaks))
      group_view.copied = logic::room_link(one);
    // Its members made again only where they changed -- or another chat's
    // are shown: a big room has thousands, and every refresh rebuilt them.
    const bool same_members = members_of == one.id && members_revision == one.members_revision;
    members_of = one.id;
    members_revision = one.members_revision;
    if (!same_members) {
      shown_members.clear();
      for (const member& each : one.members)
        shown_members.emplace_back(each, presence_of(now, one.id.account, each.id));
    }
    auto& rows = std::get<0>(std::get<0>(members.fChildren).fChildren);
    if (!same_members && nodes::reconcile(
            rows, shown_members, [](const auto& each) { return each.first.id; },
            [](const member_row<open_person>& row) { return row.id; },
            [&](const auto& each) { return member_row<open_person>(each.first, each.second, open_person{this}); },
            [](const member_row<open_person>& row, const auto& each) {
              return row.who == each.first && row.how_shown == each.second;
            }))
      members.invalidateLayout();
    members_header.show(static_cast<std::size_t>(std::max<std::int64_t>(static_cast<std::int64_t>(one.members.size()), one.member_count)),
                        [this](std::size_t count) { return members_head(actions, count); });
    this->render();
  }
  void open_member(std::string id) {
    person = std::move(id);
    this->render();
  }
  void close_member() {
    person.reset();
    this->render();
  }
  // The upper part as a function of the group's view and the member open.
  void render() {
    view shown = group_view;
    if (person) {
      // Anyone's page: a member's, with how they are and their role; or,
      // for someone the chat does not list -- the other side of a direct
      // chat, a sender from further back -- by their address.
      shown = {*person, *person, std::string("not a member of this chat"), group_view.group, group_view.muted, true};
      if (const auto found = std::ranges::find(shown_members, *person, [](const auto& each) { return each.first.id; });
          found != shown_members.end()) {
        const member& who = found->first;
        shown = {who.id,
                 who.name.empty() ? who.id : who.name,
                 who.role ? std::format("{} · {}", found->second, *who.role) : found->second,
                 group_view.group,
                 group_view.muted,
                 true};
      } else if (!group_view.group && *person == key) {
        shown.name = group_view.name;
        shown.status = group_view.status;
      }
    }
    upper.show(shown, [this](const view& v) { return head(actions, this, v); });
    const bool list = shown.group && !shown.of_person;
    band_2.setVisible(list);
    members_header.setVisible(list);
    members.setVisible(list);
    this->invalidateLayout();
  }

};

}  // namespace mux::ui
