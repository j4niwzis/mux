// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui: the window's screens, as skiff nodes -- the conversations of every
// account down the side with the chosen one beside them, under a top bar
// that swaps in the panels for adding an account and for the accounts, all in
// the one window.
//
// The screens are views. What a control does is ask the program, through
// `Actions`, and the program changes the screens between events: a screen
// switched or a list rebuilt inside a click would destroy the control whose
// handler is still running.
export module mux.ui;

import std;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes;
import skiff.widgets;
import mux.core;
import mux.config;

export namespace mux::ui {

namespace scene = skiff::scene;
namespace nodes = skiff::nodes;
namespace widgets = skiff::widgets;

inline const skia::SkColor background = skia::colorSetARGB(255, 24, 27, 30);
inline const skia::SkColor sidebar_colour = skia::colorSetARGB(255, 32, 36, 40);
inline const skia::SkColor chosen_colour = skia::colorSetARGB(255, 52, 60, 66);
inline const skia::SkColor text_colour = skia::colorSetARGB(255, 235, 240, 243);
inline const skia::SkColor dim_colour = skia::colorSetARGB(255, 150, 162, 170);
inline const skia::SkColor accent_colour = skia::colorSetARGB(255, 102, 204, 255);
inline const skia::SkColor error_colour = skia::colorSetARGB(255, 255, 120, 110);

// The protocol an address speaks: a Matrix user ID starts with '@', and a JID
// cannot.
[[nodiscard]] inline protocol_t protocol_of(std::string_view address) {
  return config::is_matrix(address) ? protocol_t{protocol::matrix{}} : protocol_t{protocol::xmpp{}};
}

// What the screens ask of the program. Each is a request: the program acts on
// it between events.
//
//   void choose(const conversation_id&)
//   void send(const conversation_id&, std::string)
//   void back()                      -- to the conversations
//   void open_accounts()
//   void open_new_account()
//   void add_xmpp()                  -- the XMPP form, when adding
//   void add_matrix()                -- the Matrix form, when adding
//   void select_account(std::string address)
//   void toggle_advanced()
//   void toggle_plain()
//   void submit_login()
//   void flip_enabled(std::string address)
//   void remove_account(std::string address)
//   void open_drawer()
//   void show_account(std::string address)  -- its settings, from the drawer
//   void set_motion(std::string level)       -- "full", "reduced" or "none"
//   void quit()

// A request with nothing to say but itself: `ask<Actions, &Actions::back>`.
template <class Actions, auto Method>
struct ask {
  Actions* actions = nullptr;
  void operator()() const { (actions->*Method)(); }
};
// The requests about one saved account.
template <class Actions>
struct flip_account {
  Actions* actions = nullptr;
  std::string address;
  void operator()() const { actions->flip_enabled(address); }
};
template <class Actions>
struct remove_account {
  Actions* actions = nullptr;
  std::string address;
  void operator()() const { actions->remove_account(address); }
};

// ---- laying out ----------------------------------------------------------

// Nodes laid out one under another down a column, each followed by its gap;
// the hidden ones take no room.
struct column_stack {
  skia::SkRect column;
  float y = 0.0f;
  template <class Child>
  void operator()(Child& node, float after) {
    if (!node.visible())
      return;
    node.fState.arrange(0.0f, y);
    scene::layout(node, column);
    y += node.bounds().height() + after;
  }
};

// The column a form is laid out in: at most `width` wide, centred.
[[nodiscard]] inline skia::SkRect form_column(const skia::SkRect& box, float width, float top) {
  const float w = std::min(width, box.width() - 32.0f);
  return skia::SkRect::MakeXYWH(box.centerX() - w * 0.5f, box.fTop + top, w, std::max(0.0f, box.height() - top));
}

// ---- the drawer's button ------------------------------------------------------

// Three lines at the top-left of the conversation list: a press pulls the
// drawer out.
template <class Actions>
struct menu_button : scene::Node {
  Actions* actions = nullptr;

  explicit menu_button(Actions* a) : actions(a) { fState.apply({.width = 36.0f, .height = 36.0f}); }

  void drawSelf(skia::SkCanvas* canvas, float alpha) {
    skia::SkFont* font = skiff::paint::defaultFont();
    if (font == nullptr)
      return;
    const skiff::paint::Painter p(canvas, *font);
    const skia::SkRect& box = fState.fBounds;
    if (fState.fHovered || this->focused())
      p.fillRounded(box, 8.0f, chosen_colour, alpha);
    const float left = box.centerX() - 8.0f;
    for (const float dy : {-6.0f, 0.0f, 6.0f})
      p.fillRounded(skia::SkRect::MakeXYWH(left, box.centerY() + dy - 1.0f, 16.0f, 2.0f), 1.0f, text_colour, alpha);
  }

  [[nodiscard]] bool acceptsInput() const { return true; }
  [[nodiscard]] bool hoverChangesAppearance() const { return true; }
  [[nodiscard]] bool focusChangesAppearance() const { return true; }
  [[nodiscard]] bool onClick(float, float) {
    actions->open_drawer();
    return true;
  }
  [[nodiscard]] scene::Semantics semantics() const {
    scene::Semantics out;
    out.fRole = scene::semantic_role::button{};
    out.fLabel = "Menu";
    out.fActions = {scene::semantic_action::focus{}, scene::semantic_action::activate{}};
    return out;
  }
};

// ---- the conversations ------------------------------------------------------

// The line to write in: Enter sends what is in it.
template <class Actions>
struct composer : widgets::TextBox<> {
  Actions* actions = nullptr;
  std::optional<conversation_id> to;

  explicit composer(Actions* a) : widgets::TextBox<>("Write a message…"), actions(a) {}

  using widgets::TextBox<>::onKey;
  void onKey(scene::phase::target at, const scene::key::down& press, scene::Reply& reply) {
    if (press.key == scene::keys::kEnter) {
      if (to && !this->text().empty())
        actions->send(*to, this->text());
      this->setText({});
      reply.handle();
      return;
    }
    widgets::TextBox<>::onKey(at, press, reply);
  }
};

// One conversation in the list: its name, and how many are unread.
template <class Actions>
struct conversation_row : scene::Node {
  Actions* actions = nullptr;
  conversation_id id;
  bool chosen = false;
  nodes::Box<> plate{sidebar_colour};
  nodes::Text name{"", 15.0f, text_colour};
  std::optional<nodes::Text> unread;

  conversation_row(Actions* a, const conversation& one, bool is_chosen)
      : actions(a), id(one.id), chosen(is_chosen) {
    fState.apply({.fillX = true, .height = 44.0f});
    plate.apply({.fill = true, .cornerRadius = 6.0f});
    plate.setColour(chosen ? chosen_colour : sidebar_colour);
    name.setText(one.name.empty() ? one.id.id : one.name);
    name.setBold(one.unread > 0);
    name.setElided(true);
    if (one.unread > 0)
      unread.emplace(std::to_string(one.unread), 13.0f, accent_colour, true);
  }

  void forEachChild(auto&& f) {
    f(plate);
    f(name);
    f(unread);
  }
  void layoutChildren() {
    const skia::SkRect box = fState.contentBox();
    scene::layout(plate, box);
    const skia::SkRect line = scene::inset(box, 10.0f, 0.0f);
    float right = line.fRight;
    if (unread) {
      unread->fState.arrange(0.0f, 0.0f, scene::anchor::kCentreRight, scene::anchor::kCentreRight);
      scene::layout(*unread, line);
      right = unread->bounds().fLeft - 8.0f;
    }
    name.setMaxWidth(std::max(0.0f, right - line.fLeft));
    name.fState.arrange(0.0f, 0.0f, scene::anchor::kCentreLeft, scene::anchor::kCentreLeft);
    scene::layout(name, line);
  }

  [[nodiscard]] bool acceptsInput() const { return true; }
  [[nodiscard]] bool onClick(float, float) {
    actions->choose(id);
    return true;
  }
  [[nodiscard]] scene::Semantics semantics() const {
    scene::Semantics out;
    out.fRole = scene::semantic_role::list_item{};
    out.fLabel = name.text();
    out.fSelected = chosen;
    out.fActions = {scene::semantic_action::focus{}, scene::semantic_action::activate{}};
    return out;
  }
};

// An account's address, and its conversations under it.
template <class Actions>
using account_block = nodes::Flow<nodes::Text, std::vector<conversation_row<Actions>>>;

// One message: who, what, and its reactions.
using message_entry = nodes::Flow<nodes::Text, nodes::Text, std::optional<nodes::Text>>;

template <class Actions>
struct conversations_screen : scene::Node {
  Actions* actions = nullptr;
  std::optional<conversation_id> chosen;

  nodes::Box<> sidebar{sidebar_colour};
  menu_button<Actions> menu;
  nodes::Text name{"mux", 17.0f, text_colour, true};
  nodes::Text no_chats{"No conversations yet.", 13.0f, dim_colour};
  nodes::ScrollContainer<nodes::Flow<std::vector<account_block<Actions>>>> list{
      nodes::Flow<std::vector<account_block<Actions>>>({.spacingY = 2.0f, .wrap = false}, {})};
  nodes::Text title{"", 20.0f, text_colour, true};
  nodes::Text topic{"", 13.0f, dim_colour};
  nodes::ScrollContainer<nodes::Flow<std::vector<message_entry>>> timeline{
      nodes::Flow<std::vector<message_entry>>({.spacingY = 10.0f, .wrap = false}, {})};
  composer<Actions> line;
  // What the main area says with no account at all.
  nodes::Text empty_title{"No accounts yet", 22.0f, text_colour, true};
  nodes::Text empty_note{"Add an XMPP or a Matrix account, and its conversations will be here.", 14.0f, dim_colour};
  widgets::Button<ask<Actions, &Actions::open_new_account>> empty_add;

  static constexpr float kSidebarWidth = 280.0f;
  static constexpr float kHeader = 52.0f;
  static constexpr float kPad = 8.0f;

  explicit conversations_screen(Actions* a)
      : actions(a), menu(a), line(a), empty_add("Add account", {a}) {
    fState.apply({.fill = true});
    sidebar.apply({.fill = true});
    empty_add.setPrimary(true);
    empty_add.apply({.width = 140.0f, .height = 36.0f});
    empty_note.setWrapped(true);
    std::get<0>(list.fChildren).apply({.fillX = true, .autoSize = scene::axes::kY});
    std::get<0>(timeline.fChildren).apply({.fillX = true, .autoSize = scene::axes::kY});
    title.apply({.fillX = true});
    topic.apply({.fillX = true});
  }

  void forEachChild(auto&& f) {
    f(sidebar);
    f(menu);
    f(name);
    f(no_chats);
    f(list);
    f(title);
    f(topic);
    f(timeline);
    f(line);
    f(empty_title);
    f(empty_note);
    f(empty_add);
  }

  void layoutChildren() {
    const skia::SkRect box = fState.contentBox();
    // On a narrow window the list takes most of it.
    const float side_width = std::min(kSidebarWidth, box.width() * 0.45f);
    const skia::SkRect side = skia::SkRect::MakeLTRB(box.fLeft, box.fTop, box.fLeft + side_width, box.fBottom);
    const skia::SkRect main = skia::SkRect::MakeLTRB(side.fRight, box.fTop, box.fRight, box.fBottom);
    scene::layout(sidebar, side);

    // The header: the drawer's button, and the name beside it.
    const skia::SkRect head = skia::SkRect::MakeLTRB(side.fLeft + kPad, side.fTop, side.fRight - kPad, side.fTop + kHeader);
    menu.fState.arrange(0.0f, 0.0f, scene::anchor::kCentreLeft, scene::anchor::kCentreLeft);
    scene::layout(menu, head);
    name.fState.arrange(menu.bounds().width() + 10.0f, 0.0f, scene::anchor::kCentreLeft, scene::anchor::kCentreLeft);
    scene::layout(name, head);

    const skia::SkRect below = skia::SkRect::MakeLTRB(side.fLeft + kPad, head.fBottom, side.fRight - kPad, side.fBottom - kPad);
    no_chats.fState.arrange(8.0f, 8.0f);
    scene::layout(no_chats, below);
    list.apply({.width = below.width(), .height = std::max(0.0f, below.height())});
    list.fState.arrange(0.0f, 0.0f);
    scene::layout(list, below);

    // No account at all: what to do about it, in the middle of the rest, a
    // little above its centre.
    if (empty_title.visible()) {
      column_stack stack{form_column(main, 420.0f, std::max(24.0f, main.height() * 0.3f))};
      empty_note.setMaxWidth(stack.column.width());
      stack(empty_title, 10.0f);
      stack(empty_note, 20.0f);
      stack(empty_add, 0.0f);
      return;
    }

    const skia::SkRect inner = scene::inset(main, 12.0f, 12.0f);
    title.fState.arrange(0.0f, 0.0f);
    scene::layout(title, inner);
    topic.fState.arrange(0.0f, title.bounds().height() + 4.0f);
    scene::layout(topic, inner);
    const float top = topic.bounds().fBottom + 8.0f;
    const float composer_height = line.fState.height();
    line.fState.arrange(0.0f, 0.0f, scene::anchor::kBottomLeft, scene::anchor::kBottomLeft);
    line.apply({.fillX = true});
    scene::layout(line, inner);
    timeline.apply({.width = inner.width(), .height = std::max(0.0f, inner.fBottom - composer_height - 8.0f - top)});
    timeline.fState.arrange(inner.fLeft - box.fLeft, top - box.fTop);
    scene::layout(timeline, box);
  }

  // The model as it is now: the list, and the chosen conversation.
  void show(const model& now) {
    auto& blocks = std::get<0>(std::get<0>(list.fChildren).fChildren);
    blocks.clear();
    for (const auto& [id, kept] : now.accounts()) {
      std::vector<conversation_row<Actions>> rows;
      for (const auto& [key, one] : kept.conversations)
        rows.emplace_back(actions, one, chosen && *chosen == one.id);
      auto& block = blocks.emplace_back(nodes::FlowOptions{.spacingY = 2.0f, .wrap = false},
                                        scene::make<nodes::Text>({.fillX = true}, id.address, 12.0f, dim_colour, true),
                                        std::move(rows));
      block.apply({.fillX = true, .autoSize = scene::axes::kY});
    }
    const bool none = now.accounts().empty();
    for (scene::Node* shown : std::initializer_list<scene::Node*>{&title, &topic, &timeline, &line})
      shown->setVisible(!none);
    std::size_t conversations = 0;
    for (const auto& [id, kept] : now.accounts())
      conversations += kept.conversations.size();
    no_chats.setVisible(conversations == 0);
    empty_title.setVisible(none);
    empty_note.setVisible(none);
    empty_add.setVisible(none);
    this->invalidateLayout();
    this->show_conversation(now);
  }

  void show_conversation(const model& now) {
    auto& entries = std::get<0>(std::get<0>(timeline.fChildren).fChildren);
    entries.clear();
    line.to = chosen;
    const conversation* one = chosen ? now.find(*chosen) : nullptr;
    if (!one) {
      title.setText("Choose a conversation");
      topic.setText("");
      return;
    }
    title.setText(one->name.empty() ? one->id.id : one->name);
    std::string about = one->topic.value_or("");
    if (one->encrypted)
      about = about.empty() ? "encrypted" : about + " · encrypted";
    if (!one->typing.empty())
      about += (about.empty() ? "" : " · ") + std::to_string(one->typing.size()) + " typing";
    topic.setText(about);
    for (const message& said : one->timeline) {
      const std::string who =
          said.sender + std::visit(overloaded{[](const delivery::sending&) { return " · sending"; },
                                              [](const delivery::failed&) { return " · not sent"; },
                                              [](const auto&) { return ""; }},
                                   said.delivery);
      std::string body = said.redacted ? "(removed)" : said.body.plain;
      if (said.edited)
        body += " (edited)";
      std::optional<nodes::Text> reactions;
      if (!said.reactions.empty()) {
        std::string line_of;
        for (const auto& [key, who_reacted] : said.reactions)
          line_of += std::format("{} {}  ", key, who_reacted.size());
        reactions.emplace(scene::make<nodes::Text>({.fillX = true}, line_of, 13.0f, dim_colour));
      }
      nodes::Text text = scene::make<nodes::Text>({.fillX = true}, body, 15.0f, text_colour);
      text.setWrapped(true);
      auto& entry = entries.emplace_back(
          nodes::FlowOptions{.spacingY = 2.0f, .wrap = false},
          scene::make<nodes::Text>({.fillX = true}, who, 12.0f, said.outgoing ? accent_colour : dim_colour, true),
          std::move(text), std::move(reactions));
      entry.apply({.fillX = true, .autoSize = scene::axes::kY});
    }
    // The newest is at the bottom, and that is where the reader is.
    timeline.scrollTo(std::numeric_limits<float>::max());
  }
};

// ---- a form row: a caption and a field -----------------------------------

struct field : scene::Node {
  nodes::Text caption;
  widgets::TextBox<> box;

  field(std::string label, std::string placeholder, std::string text = {})
      : caption(std::move(label), 13.0f, dim_colour), box(std::move(placeholder)) {
    fState.apply({.fillX = true, .height = 64.0f});
    box.apply({.fillX = true});
    box.setText(std::move(text));
  }
  void forEachChild(auto&& f) {
    f(caption);
    f(box);
  }
  void layoutChildren() {
    const skia::SkRect area = fState.contentBox();
    caption.fState.arrange(0.0f, 0.0f);
    scene::layout(caption, area);
    box.fState.arrange(0.0f, caption.bounds().height() + 4.0f);
    scene::layout(box, area);
  }
};

// ---- the account forms ---------------------------------------------------------

// What every account form ends with: what went wrong or what is happening,
// and its buttons. Enter in any of the form's fields submits it.
template <class Actions>
struct form_end {
  nodes::Text message{"", 13.0f, error_colour};
  widgets::Button<ask<Actions, &Actions::submit_login>> submit;
  widgets::Button<ask<Actions, &Actions::back>> close;

  form_end(Actions* a, bool editing) : submit(editing ? "Save" : "Log in", {a}), close("Close", {a}) {
    submit.setPrimary(true);
    submit.apply({.width = 120.0f, .height = 36.0f});
    close.apply({.width = 120.0f, .height = 36.0f});
    message.setWrapped(true);
    message.apply({.fillX = true});
  }

  void each(auto&& f) {
    f(message);
    f(submit);
    f(close);
  }

  void say(std::string text, bool error) {
    message.setText(std::move(text));
    message.setColour(error ? error_colour : dim_colour);
  }

  void place(column_stack& stack) {
    stack(message, 12.0f);
    submit.fState.arrange(0.0f, stack.y);
    scene::layout(submit, stack.column);
    close.fState.arrange(submit.bounds().width() + 12.0f, stack.y);
    scene::layout(close, stack.column);
  }
};

// Text typed into an optional: nothing when the field is empty.
[[nodiscard]] inline std::optional<std::string> typed_or_nothing(const std::string& text) {
  if (text.empty())
    return std::nullopt;
  return text;
}

// What "Advanced" folds out on an XMPP form: the resource, where to connect,
// and PLAIN without TLS. Its height is what its last layout took.
template <class Actions>
struct xmpp_advanced : scene::Node {
  field resource{"Resource", "mux", "mux"};
  field host{"Host", "from the domain's SRV records"};
  field port{"Port", "5222"};
  widgets::Toggle<ask<Actions, &Actions::toggle_plain>> plain;
  nodes::Text plain_label{"Allow PLAIN without TLS. Only for a test server on this machine: never over a network.",
                          13.0f, error_colour};
  // Its height unfolded, as the last layout found it.
  float full = 0.0f;

  explicit xmpp_advanced(Actions* a) : plain({a}) {
    fState.apply({.fillX = true});
    plain_label.setWrapped(true);
  }

  void measure(const skia::SkRect&) { fState.fHeight = full; }

  void forEachChild(auto&& f) {
    f(resource);
    f(host);
    f(port);
    f(plain);
    f(plain_label);
  }

  void layoutChildren() {
    const skia::SkRect box = fState.contentBox();
    column_stack stack{skia::SkRect::MakeXYWH(box.fLeft, box.fTop, box.width(), 10000.0f)};
    stack(resource, 8.0f);
    stack(host, 8.0f);
    stack(port, 8.0f);
    plain.fState.arrange(0.0f, stack.y);
    scene::layout(plain, stack.column);
    const float beside = plain.bounds().width() + 10.0f;
    plain_label.setMaxWidth(std::max(0.0f, stack.column.width() - beside));
    plain_label.fState.arrange(beside, stack.y);
    scene::layout(plain_label, stack.column);
    full = stack.y + std::max(plain.bounds().height(), plain_label.bounds().height()) + 12.0f;
  }
};

// An XMPP account's settings: its JID and password, and "Advanced" folds out
// the rest. It fills the column it is given.
template <class Actions>
struct xmpp_form : scene::Node {
  Actions* actions = nullptr;
  // The address the account was saved under, when this is an edit of one.
  std::optional<std::string> editing;
  bool advanced = false;

  field address{"Address (JID)", "user@example.com"};
  field password{"Password", "Password"};
  widgets::Button<ask<Actions, &Actions::toggle_advanced>> advanced_button;
  widgets::Collapsible<xmpp_advanced<Actions>> more;
  form_end<Actions> end;

  xmpp_form(Actions* a, const std::optional<config::xmpp_account>& from)
      : actions(a), advanced_button("Advanced", {a}), more(a), end(a, from.has_value()) {
    fState.apply({.fill = true});
    password.box.setMasked(true);
    advanced_button.apply({.width = 120.0f, .height = 36.0f});
    if (from) {
      editing = from->address;
      address.box.setText(from->address);
      password.box.setText(from->password);
      more.child().resource.box.setText(from->resource);
      if (from->host)
        more.child().host.box.setText(*from->host);
      if (from->port)
        more.child().port.box.setText(std::to_string(*from->port));
      more.child().plain.setOn(from->plain_without_tls);
      advanced = from->resource != "mux" || from->host || from->port || from->plain_without_tls;
    }
    more.setOpenNow(advanced);
  }

  void forEachChild(auto&& f) {
    f(address);
    f(password);
    f(advanced_button);
    f(more);
    end.each(f);
  }

  // Folded out or away: smoothly, where things move.
  void show_advanced(bool shown) {
    advanced = shown;
    more.setOpen(shown);
  }

  void flip_plain() { more.child().plain.setOn(!more.child().plain.on()); }

  // The account as typed, or what is wrong with it. What is folded away is
  // kept as it is: folding is not clearing.
  [[nodiscard]] std::expected<config::xmpp_account, std::string> account() const {
    config::xmpp_account out{.address = address.box.text(),
                             .password = password.box.text(),
                             .resource = more.child().resource.box.text(),
                             .host = typed_or_nothing(more.child().host.box.text()),
                             .plain_without_tls = more.child().plain.on()};
    if (const std::string& text = more.child().port.box.text(); !text.empty()) {
      std::int64_t number = 0;
      const auto [last, failed] = std::from_chars(text.data(), text.data() + text.size(), number);
      if (failed != std::errc{} || last != text.data() + text.size())
        return std::unexpected("A port is a number from 1 to 65535");
      out.port = number;
    }
    if (auto wrong = config::check(out))
      return std::unexpected(*wrong);
    return out;
  }

  void say(std::string text, bool error) { end.say(std::move(text), error); }

  using Node::onKey;
  void onKey(scene::phase::bubble, const scene::key::down& press, scene::Reply& reply) {
    if (press.key == scene::keys::kEnter) {
      actions->submit_login();
      reply.handle();
    }
  }

  void layoutChildren() {
    column_stack stack{fState.contentBox()};
    stack(address, 12.0f);
    stack(password, 12.0f);
    stack(advanced_button, 12.0f);
    stack(more, 0.0f);
    end.place(stack);
  }
};

// A Matrix account's settings: its user ID and password, the homeserver
// (found through the server's .well-known when left empty) and what this
// device is called.
template <class Actions>
struct matrix_form : scene::Node {
  Actions* actions = nullptr;
  std::optional<std::string> editing;

  field user_id{"User ID", "@user:example.org"};
  field password{"Password", "Password"};
  field homeserver{"Homeserver", "found through the server's .well-known"};
  field device_name{"Device name", "mux", "mux"};
  form_end<Actions> end;

  matrix_form(Actions* a, const std::optional<config::matrix_account>& from)
      : actions(a), end(a, from.has_value()) {
    fState.apply({.fill = true});
    password.box.setMasked(true);
    if (from) {
      editing = from->user_id;
      user_id.box.setText(from->user_id);
      password.box.setText(from->password);
      if (from->homeserver)
        homeserver.box.setText(*from->homeserver);
      device_name.box.setText(from->device_name);
    }
  }

  void forEachChild(auto&& f) {
    f(user_id);
    f(password);
    f(homeserver);
    f(device_name);
    end.each(f);
  }

  [[nodiscard]] std::expected<config::matrix_account, std::string> account() const {
    config::matrix_account out{.user_id = user_id.box.text(),
                               .password = password.box.text(),
                               .homeserver = typed_or_nothing(homeserver.box.text()),
                               .device_name = device_name.box.text()};
    if (auto wrong = config::check(out))
      return std::unexpected(*wrong);
    return out;
  }

  void say(std::string text, bool error) { end.say(std::move(text), error); }

  using Node::onKey;
  void onKey(scene::phase::bubble, const scene::key::down& press, scene::Reply& reply) {
    if (press.key == scene::keys::kEnter) {
      actions->submit_login();
      reply.handle();
    }
  }

  void layoutChildren() {
    column_stack stack{fState.contentBox()};
    stack(user_id, 12.0f);
    stack(password, 12.0f);
    stack(homeserver, 12.0f);
    stack(device_name, 12.0f);
    end.place(stack);
  }
};

// Either form, as the panels hold them.
template <class Actions>
using account_form = std::variant<xmpp_form<Actions>, matrix_form<Actions>>;

// The form of an account's own protocol, filled in from it.
template <class Actions>
[[nodiscard]] account_form<Actions> form_of(Actions* a, const config::account_t& saved) {
  return std::visit(overloaded{[a](const config::xmpp_account& one) {
                                 return account_form<Actions>(std::in_place_index<0>, a, one);
                               },
                               [a](const config::matrix_account& one) {
                                 return account_form<Actions>(std::in_place_index<1>, a, one);
                               }},
                    saved);
}

// The XMPP form among them, when that is the one up.
template <class Actions>
[[nodiscard]] xmpp_form<Actions>* xmpp_form_in(account_form<Actions>& form) {
  return std::visit(overloaded{[](xmpp_form<Actions>& one) { return &one; },
                               [](matrix_form<Actions>&) -> xmpp_form<Actions>* { return nullptr; }},
                    form);
}

// A form laid out in the column under `top`.
template <class Actions>
void place_form(account_form<Actions>& form, const skia::SkRect& column, float top) {
  std::visit(
      [&](auto& one) {
        one.fState.arrange(0.0f, 0.0f);
        scene::layout(one, skia::SkRect::MakeLTRB(column.fLeft, column.fTop + top, column.fRight, column.fBottom));
      },
      form);
}

// Esc closes a panel: back to the conversations.
template <class Actions>
struct closes_on_escape : scene::Node {
  Actions* actions = nullptr;
  explicit closes_on_escape(Actions* a) : actions(a) {}

  using Node::onKey;
  void onKey(scene::phase::bubble, const scene::key::down& press, scene::Reply& reply) {
    if (press.key == scene::keys::kEscape) {
      actions->back();
      reply.handle();
    }
  }
};

// ---- adding an account ------------------------------------------------------------

// Opened from the top bar, in place of the conversations: XMPP or Matrix at
// the top, and that protocol's form under it.
template <class Actions>
struct add_account_panel : closes_on_escape<Actions> {
  static constexpr int kTab = 1;
  static constexpr float kWidth = 440.0f;

  nodes::Text title{"Add an account", 22.0f, text_colour, true};
  widgets::Button<ask<Actions, &Actions::add_xmpp>> xmpp_tab;
  widgets::Button<ask<Actions, &Actions::add_matrix>> matrix_tab;
  nodes::Text note{"", 13.0f, dim_colour};
  account_form<Actions> form;

  explicit add_account_panel(Actions* a)
      : closes_on_escape<Actions>(a), xmpp_tab("XMPP", {a}), matrix_tab("Matrix", {a}),
        form(std::in_place_index<0>, a, std::nullopt) {
    this->fState.apply({.fill = true});
    xmpp_tab.apply({.width = 110.0f, .height = 32.0f});
    matrix_tab.apply({.width = 110.0f, .height = 32.0f});
    note.setWrapped(true);
    this->light();
  }

  void forEachChild(auto&& f) {
    f(title);
    f(xmpp_tab);
    f(matrix_tab);
    f(note);
    f(form);
  }

  void show_xmpp() {
    form.template emplace<0>(this->actions, std::nullopt);
    this->light();
  }
  void show_matrix() {
    form.template emplace<1>(this->actions, std::nullopt);
    this->light();
  }
  [[nodiscard]] xmpp_form<Actions>* xmpp() { return xmpp_form_in(form); }

  // The tab of the form that is up, lit, and what that protocol is.
  void light() {
    std::visit(overloaded{[this](const xmpp_form<Actions>&) {
                            xmpp_tab.setPrimary(true);
                            matrix_tab.setPrimary(false);
                            note.setText("An address like user@example.com, on a server such as Prosody or ejabberd.");
                          },
                          [this](const matrix_form<Actions>&) {
                            xmpp_tab.setPrimary(false);
                            matrix_tab.setPrimary(true);
                            note.setText("A user ID like @user:example.org, on a homeserver such as Synapse.");
                          }},
               form);
    this->invalidateLayout();
  }

  void layoutChildren() {
    column_stack stack{form_column(this->fState.contentBox(), kWidth, 32.0f)};
    stack(title, 16.0f);
    xmpp_tab.fState.arrange(0.0f, stack.y);
    scene::layout(xmpp_tab, stack.column);
    matrix_tab.fState.arrange(xmpp_tab.bounds().width() + 8.0f, stack.y);
    scene::layout(matrix_tab, stack.column);
    stack.y += xmpp_tab.bounds().height() + 10.0f;
    note.setMaxWidth(stack.column.width());
    stack(note, 16.0f);
    place_form(form, stack.column, stack.y);
  }
};

// ---- the accounts -------------------------------------------------------------------

// What the model says of a saved account, in a few words, and whether that
// is a failure.
[[nodiscard]] inline std::pair<std::string, bool> state_of(const config::account_t& one, const model& now) {
  const std::string& address = config::address_of(one);
  if (!config::enabled_of(one))
    return {"off", false};
  const auto found = now.accounts().find(account_id{protocol_of(address), address});
  if (found == now.accounts().end())
    return {"offline", false};
  bool failed = false;
  std::string said = std::visit(overloaded{[](const connection::offline&) { return std::string("offline"); },
                                           [](const connection::connecting&) { return std::string("connecting…"); },
                                           [](const connection::online&) { return std::string("online"); },
                                           [&failed](const connection::failed& why) {
                                             failed = true;
                                             return "failed: " + why.error;
                                           }},
                                found->second.state);
  return {std::move(said), failed};
}

// One account in the list: its address, protocol and state. A click shows
// its settings beside the list.
template <class Actions>
struct account_entry : scene::Node {
  Actions* actions = nullptr;
  std::string address;
  bool selected = false;
  nodes::Box<> plate{sidebar_colour};
  nodes::Text name;
  nodes::Text state;

  account_entry(Actions* a, const config::account_t& saved, const model& now, bool is_selected)
      : actions(a), address(config::address_of(saved)), selected(is_selected),
        name(address, 15.0f, text_colour, true), state("", 13.0f, dim_colour) {
    fState.apply({.fillX = true, .height = 52.0f});
    plate.apply({.fill = true, .cornerRadius = 6.0f});
    plate.setColour(selected ? chosen_colour : sidebar_colour);
    const auto [how, failed] = state_of(saved, now);
    state.setText(std::format("{} · {}", config::protocol_name(saved), how));
    state.setColour(failed ? error_colour : dim_colour);
    name.setElided(true);
    state.setElided(true);
  }

  void forEachChild(auto&& f) {
    f(plate);
    f(name);
    f(state);
  }
  void layoutChildren() {
    const skia::SkRect box = fState.contentBox();
    scene::layout(plate, box);
    const skia::SkRect inner = scene::inset(box, 10.0f, 7.0f);
    name.setMaxWidth(inner.width());
    name.fState.arrange(0.0f, 0.0f);
    scene::layout(name, inner);
    state.setMaxWidth(inner.width());
    state.fState.arrange(0.0f, name.bounds().height() + 4.0f);
    scene::layout(state, inner);
  }

  [[nodiscard]] bool acceptsInput() const { return true; }
  [[nodiscard]] bool onClick(float, float) {
    actions->select_account(address);
    return true;
  }
  [[nodiscard]] scene::Semantics semantics() const {
    scene::Semantics out;
    out.fRole = scene::semantic_role::list_item{};
    out.fLabel = address;
    out.fSelected = selected;
    out.fActions = {scene::semantic_action::focus{}, scene::semantic_action::activate{}};
    return out;
  }
};

// The chosen account: on or off, removed, and its own protocol's form.
template <class Actions>
struct account_editor : scene::Node {
  nodes::Text heading;
  nodes::Text state{"", 13.0f, dim_colour};
  widgets::Toggle<flip_account<Actions>> enabled;
  nodes::Text enabled_label{"On", 13.0f, dim_colour};
  widgets::Button<remove_account<Actions>> remove;
  account_form<Actions> form;

  account_editor(Actions* a, const config::account_t& saved)
      : heading(config::address_of(saved), 20.0f, text_colour, true),
        enabled(flip_account<Actions>{a, config::address_of(saved)}),
        remove("Remove", remove_account<Actions>{a, config::address_of(saved)}),
        form(form_of(a, saved)) {
    fState.apply({.fill = true});
    heading.setElided(true);
    state.setElided(true);
    enabled.setOn(config::enabled_of(saved));
    remove.apply({.width = 100.0f, .height = 32.0f});
  }

  void forEachChild(auto&& f) {
    f(heading);
    f(state);
    f(enabled);
    f(enabled_label);
    f(remove);
    f(form);
  }

  // What the model says of it now, kept current without touching the form.
  void show(const config::account_t& saved, const model& now) {
    const auto [how, failed] = state_of(saved, now);
    state.setText(std::format("{} · {}", config::protocol_name(saved), how));
    state.setColour(failed ? error_colour : dim_colour);
    enabled.setOn(config::enabled_of(saved));
  }

  void say(std::string text, bool error) {
    std::visit([&](auto& one) { one.say(std::move(text), error); }, form);
  }

  void layoutChildren() {
    const skia::SkRect box = fState.contentBox();
    remove.fState.arrange(0.0f, 0.0f, scene::anchor::kTopRight, scene::anchor::kTopRight);
    scene::layout(remove, box);
    enabled.fState.arrange(-(remove.bounds().width() + 16.0f), 5.0f, scene::anchor::kTopRight,
                           scene::anchor::kTopRight);
    scene::layout(enabled, box);
    enabled_label.fState.arrange(enabled.bounds().fLeft - box.fLeft - 30.0f, 7.0f);
    scene::layout(enabled_label, box);
    heading.setMaxWidth(std::max(0.0f, enabled_label.bounds().fLeft - box.fLeft - 12.0f));
    heading.fState.arrange(0.0f, 0.0f);
    scene::layout(heading, box);
    state.setMaxWidth(box.width());
    state.fState.arrange(0.0f, heading.bounds().height() + 6.0f);
    scene::layout(state, box);
    place_form(form, box, state.bounds().fBottom - box.fTop + 20.0f);
  }
};

// Opened from the top bar: the saved accounts down the side, and the chosen
// one's settings beside them.
template <class Actions>
struct accounts_panel : closes_on_escape<Actions> {
  static constexpr int kTab = 2;
  static constexpr float kListWidth = 280.0f;
  static constexpr float kPad = 8.0f;

  std::optional<std::string> selected;
  nodes::Box<> side{sidebar_colour};
  nodes::ScrollContainer<nodes::Flow<std::vector<account_entry<Actions>>>> list{
      nodes::Flow<std::vector<account_entry<Actions>>>({.spacingY = 2.0f, .wrap = false}, {})};
  widgets::Button<ask<Actions, &Actions::open_new_account>> add;
  nodes::Text message{"", 13.0f, error_colour};
  // No account chosen, or the chosen one.
  std::variant<nodes::Text, account_editor<Actions>> detail{std::in_place_index<0>, "No accounts yet.", 15.0f,
                                                           dim_colour};

  explicit accounts_panel(Actions* a) : closes_on_escape<Actions>(a), add("Add account", {a}) {
    this->fState.apply({.fill = true});
    side.apply({.fill = true});
    add.apply({.width = kListWidth - 2 * kPad, .height = 34.0f});
    std::get<0>(list.fChildren).apply({.fillX = true, .autoSize = scene::axes::kY});
    message.setWrapped(true);
  }

  void forEachChild(auto&& f) {
    f(side);
    f(list);
    f(add);
    f(message);
    f(detail);
  }

  // The saved accounts, with what the model says of each; the chosen one's
  // form is kept as it is, typing and all.
  void show(const std::vector<config::account_t>& saved, const model& now) {
    auto& entries = std::get<0>(std::get<0>(list.fChildren).fChildren);
    entries.clear();
    const config::account_t* chosen = nullptr;
    for (const config::account_t& one : saved) {
      const bool is_it = selected && config::address_of(one) == *selected;
      if (is_it)
        chosen = &one;
      entries.emplace_back(this->actions, one, now, is_it);
    }
    if (!chosen) {
      selected.reset();
      detail.template emplace<0>(saved.empty() ? "No accounts yet." : "Choose an account.", 15.0f, dim_colour);
    } else {
      std::visit(overloaded{[&](account_editor<Actions>& editor) { editor.show(*chosen, now); },
                            [](nodes::Text&) {}},
                 detail);
    }
    this->invalidateLayout();
  }

  // An account's settings, brought up afresh.
  void select(const config::account_t& one, const model& now) {
    selected = config::address_of(one);
    detail.template emplace<1>(this->actions, one);
    std::get<1>(detail).show(one, now);
  }

  [[nodiscard]] account_editor<Actions>* editor() {
    return std::visit(overloaded{[](account_editor<Actions>& one) { return &one; },
                                 [](nodes::Text&) -> account_editor<Actions>* { return nullptr; }},
                      detail);
  }
  [[nodiscard]] xmpp_form<Actions>* xmpp() {
    auto* chosen = this->editor();
    return chosen ? xmpp_form_in(chosen->form) : nullptr;
  }

  void say(std::string text) {
    message.setText(std::move(text));
    this->invalidateLayout();
  }

  void layoutChildren() {
    const skia::SkRect box = this->fState.contentBox();
    const skia::SkRect left = skia::SkRect::MakeLTRB(box.fLeft, box.fTop, box.fLeft + kListWidth, box.fBottom);
    scene::layout(side, left);
    add.fState.arrange(kPad, kPad);
    scene::layout(add, left);
    float y = add.bounds().fBottom - box.fTop + kPad;
    if (!message.text().empty()) {
      message.setMaxWidth(kListWidth - 2 * kPad);
      message.fState.arrange(kPad, y);
      scene::layout(message, left);
      y = message.bounds().fBottom - box.fTop + kPad;
    }
    list.apply({.width = kListWidth - 2 * kPad, .height = std::max(0.0f, box.height() - y - kPad)});
    list.fState.arrange(kPad, y);
    scene::layout(list, box);

    const skia::SkRect right = skia::SkRect::MakeLTRB(left.fRight, box.fTop, box.fRight, box.fBottom);
    const skia::SkRect column = form_column(right, 520.0f, 32.0f);
    std::visit(
        [&](auto& one) {
          one.fState.arrange(0.0f, 0.0f);
          scene::layout(one, column);
        },
        detail);
  }
};

// ---- the window -----------------------------------------------------------------

// ---- the drawer -------------------------------------------------------------------

// The levels of motion, as the accounts file names them.
inline constexpr std::array<std::string_view, 3> kMotions{"full", "reduced", "none"};

// A level of motion chosen from the drawer.
template <class Actions>
struct choose_motion {
  Actions* actions = nullptr;
  std::string_view level;
  void operator()() const { actions->set_motion(std::string(level)); }
};

// One line of the drawer: flat text that lights under the pointer, and a
// tick before it when it is the one in use.
template <class Act>
struct drawer_item : scene::Node {
  Act act;
  bool checked = false;
  nodes::Text label;

  drawer_item(std::string text, Act what) : act(std::move(what)), label(std::move(text), 15.0f, text_colour) {
    fState.apply({.fillX = true, .height = 40.0f});
  }

  void set_checked(bool on) {
    checked = on;
    this->markDamaged();
  }

  void forEachChild(auto&& f) { f(label); }
  void layoutChildren() {
    label.fState.arrange(34.0f, 0.0f, scene::anchor::kCentreLeft, scene::anchor::kCentreLeft);
    scene::layout(label, fState.contentBox());
  }
  void drawSelf(skia::SkCanvas* canvas, float alpha) {
    skia::SkFont* font = skiff::paint::defaultFont();
    if (font == nullptr)
      return;
    const skiff::paint::Painter p(canvas, *font);
    const skia::SkRect& box = fState.fBounds;
    if (fState.fHovered || this->focused())
      p.fillRounded(box, 8.0f, chosen_colour, alpha);
    if (checked)
      p.fillRounded(skia::SkRect::MakeXYWH(box.fLeft + 14.0f, box.centerY() - 4.0f, 8.0f, 8.0f), 4.0f,
                    accent_colour, alpha);
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
    out.fLabel = label.text();
    out.fSelected = checked;
    out.fActions = {scene::semantic_action::focus{}, scene::semantic_action::activate{}};
    return out;
  }
};

// An account in the drawer: its address, protocol and state; a press shows
// its settings.
template <class Actions>
struct drawer_account : scene::Node {
  Actions* actions = nullptr;
  std::string address;
  nodes::Text name;
  nodes::Text state;

  drawer_account(Actions* a, const config::account_t& saved, const model& now)
      : actions(a), address(config::address_of(saved)), name(address, 14.0f, text_colour, true),
        state("", 12.0f, dim_colour) {
    fState.apply({.fillX = true, .height = 50.0f});
    const auto [how, failed] = state_of(saved, now);
    state.setText(std::format("{} · {}", config::protocol_name(saved), how));
    state.setColour(failed ? error_colour : dim_colour);
    name.setElided(true);
    state.setElided(true);
  }

  void forEachChild(auto&& f) {
    f(name);
    f(state);
  }
  void layoutChildren() {
    const skia::SkRect inner = scene::inset(fState.contentBox(), 14.0f, 7.0f);
    name.setMaxWidth(inner.width());
    name.fState.arrange(0.0f, 0.0f);
    scene::layout(name, inner);
    state.setMaxWidth(inner.width());
    state.fState.arrange(0.0f, name.bounds().height() + 3.0f);
    scene::layout(state, inner);
  }
  void drawSelf(skia::SkCanvas* canvas, float alpha) {
    skia::SkFont* font = skiff::paint::defaultFont();
    if (font == nullptr || !(fState.fHovered || this->focused()))
      return;
    const skiff::paint::Painter p(canvas, *font);
    p.fillRounded(fState.fBounds, 8.0f, chosen_colour, alpha);
  }

  [[nodiscard]] bool acceptsInput() const { return true; }
  [[nodiscard]] bool hoverChangesAppearance() const { return true; }
  [[nodiscard]] bool onClick(float, float) {
    actions->show_account(address);
    return true;
  }
  [[nodiscard]] scene::Semantics semantics() const {
    scene::Semantics out;
    out.fRole = scene::semantic_role::list_item{};
    out.fLabel = address;
    out.fActions = {scene::semantic_action::focus{}, scene::semantic_action::activate{}};
    return out;
  }
};

// What the drawer holds, as Telegram's does: the accounts, then what to do
// about them, how much moves, and Quit.
template <class Actions>
struct drawer_panel : scene::Node {
  nodes::Text title{"mux", 20.0f, text_colour, true};
  std::vector<drawer_account<Actions>> accounts;
  nodes::Box<> rule_1{chosen_colour};
  drawer_item<ask<Actions, &Actions::open_new_account>> add;
  drawer_item<ask<Actions, &Actions::open_accounts>> manage;
  nodes::Box<> rule_2{chosen_colour};
  nodes::Text motion_title{"Motion", 12.0f, dim_colour, true};
  drawer_item<choose_motion<Actions>> full;
  drawer_item<choose_motion<Actions>> reduced;
  drawer_item<choose_motion<Actions>> none;
  nodes::Box<> rule_3{chosen_colour};
  drawer_item<ask<Actions, &Actions::quit>> quit;

  explicit drawer_panel(Actions* a)
      : add("Add account", {a}),
        manage("Manage accounts", {a}),
        full("Full", {a, kMotions[0]}),
        reduced("Reduced", {a, kMotions[1]}),
        none("None", {a, kMotions[2]}),
        quit("Quit", {a}) {
    fState.apply({.fill = true});
    for (nodes::Box<>* rule : {&rule_1, &rule_2, &rule_3})
      rule->apply({.fillX = true, .height = 1.0f});
  }

  void forEachChild(auto&& f) {
    f(title);
    f(accounts);
    f(rule_1);
    f(add);
    f(manage);
    f(rule_2);
    f(motion_title);
    f(full);
    f(reduced);
    f(none);
    f(rule_3);
    f(quit);
  }

  void show(Actions* a, const std::vector<config::account_t>& saved, const model& now) {
    accounts.clear();
    for (const config::account_t& one : saved)
      accounts.emplace_back(a, one, now);
    this->invalidateLayout();
  }
  void show_motion(std::string_view level) {
    full.set_checked(level == kMotions[0]);
    reduced.set_checked(level == kMotions[1]);
    none.set_checked(level == kMotions[2]);
  }

  void layoutChildren() {
    column_stack stack{scene::inset(fState.contentBox(), 8.0f, 0.0f)};
    stack.y = 16.0f;
    title.fState.arrange(14.0f, stack.y);
    scene::layout(title, stack.column);
    stack.y += title.bounds().height() + 14.0f;
    for (auto& one : accounts)
      stack(one, 2.0f);
    stack.y += accounts.empty() ? 0.0f : 6.0f;
    stack(rule_1, 6.0f);
    stack(add, 0.0f);
    stack(manage, 6.0f);
    stack(rule_2, 10.0f);
    motion_title.fState.arrange(14.0f, stack.y);
    scene::layout(motion_title, stack.column);
    stack.y += motion_title.bounds().height() + 4.0f;
    stack(full, 0.0f);
    stack(reduced, 0.0f);
    stack(none, 6.0f);
    stack(rule_3, 6.0f);
    stack(quit, 0.0f);
  }
};

// ---- the window -------------------------------------------------------------------

// The conversations; over them the panel that is open, if one is, sliding in
// from the right and back out when closed; and over both, the drawer, pulled
// out from the left. All in this one window, switched by the program between
// events.
template <class Actions>
struct window : scene::Node {
  using panel_type = std::variant<add_account_panel<Actions>, accounts_panel<Actions>>;
  using screens = widgets::SlideOver<conversations_screen<Actions>, panel_type>;

  Actions* actions = nullptr;
  nodes::Box<> backdrop{background};
  widgets::Drawer<screens, drawer_panel<Actions>> frame;

  explicit window(Actions* a)
      : actions(a), frame(std::piecewise_construct, std::forward_as_tuple(a), std::forward_as_tuple(a)) {
    fState.apply({.fill = true});
    backdrop.apply({.fill = true});
    frame.base().setSheetColour(background);
    frame.setSheetColour(sidebar_colour);
  }
  void forEachChild(auto&& f) {
    f(backdrop);
    f(frame);
  }

  [[nodiscard]] conversations_screen<Actions>& main() { return frame.base().base(); }
  // The panel that is up, not on its way out.
  [[nodiscard]] panel_type* open_panel() { return frame.base().shown(); }

  // A panel opened, in place of the one up if there is one.
  template <class Panel>
  Panel& open() {
    return std::get<Panel>(frame.base().open(std::in_place_type<Panel>, actions));
  }
  void close() { frame.base().close(); }
  // From the program, between events.
  void drop_closed() { frame.base().dropClosed(); }

  void open_drawer() { frame.open(); }
  void close_drawer() { frame.close(); }

  void show(const std::vector<config::account_t>& saved, const model& now) {
    frame.content().show(actions, saved, now);
  }
  void show_motion(std::string_view level) { frame.content().show_motion(level); }

  void layoutChildren() {
    const skia::SkRect box = fState.contentBox();
    scene::layout(backdrop, box);
    frame.fState.arrange(0.0f, 0.0f);
    scene::layout(frame, box);
  }
};

}  // namespace mux::ui
