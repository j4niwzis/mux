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

  // The top bar's button this screen lights.
  static constexpr int kTab = 0;
  static constexpr float kSidebarWidth = 280.0f;
  static constexpr float kPad = 8.0f;

  explicit conversations_screen(Actions* a)
      : actions(a), line(a), empty_add("Add account", {a}) {
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
    const skia::SkRect side = skia::SkRect::MakeLTRB(box.fLeft, box.fTop, box.fLeft + kSidebarWidth, box.fBottom);
    const skia::SkRect main = skia::SkRect::MakeLTRB(side.fRight, box.fTop, box.fRight, box.fBottom);
    scene::layout(sidebar, side);

    list.apply({.width = side.width() - 2 * kPad, .height = std::max(0.0f, side.height() - 2 * kPad)});
    list.fState.arrange(kPad, kPad);
    scene::layout(list, box);

    const skia::SkRect inner = scene::inset(main, 12.0f, 12.0f);
    if (empty_title.visible()) {
      column_stack stack{form_column(inner, 460.0f, 48.0f)};
      empty_note.setMaxWidth(stack.column.width());
      stack(empty_title, 10.0f);
      stack(empty_note, 20.0f);
      stack(empty_add, 0.0f);
      return;
    }
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

// Across the top: the conversations, adding an account, and the accounts,
// the one up lit; and how many accounts are online.
template <class Actions>
struct top_bar : scene::Node {
  nodes::Box<> plate{sidebar_colour};
  nodes::Text name{"mux", 16.0f, text_colour, true};
  widgets::Button<ask<Actions, &Actions::back>> chats;
  widgets::Button<ask<Actions, &Actions::open_new_account>> add;
  widgets::Button<ask<Actions, &Actions::open_accounts>> accounts;
  nodes::Text status{"", 13.0f, dim_colour};

  static constexpr float kHeight = 48.0f;

  explicit top_bar(Actions* a) : chats("Chats", {a}), add("Add account", {a}), accounts("Accounts", {a}) {
    fState.apply({.fillX = true, .height = kHeight});
    plate.apply({.fill = true});
    chats.apply({.width = 90.0f, .height = 32.0f});
    add.apply({.width = 130.0f, .height = 32.0f});
    accounts.apply({.width = 110.0f, .height = 32.0f});
    status.setElided(true);
  }

  void forEachChild(auto&& f) {
    f(plate);
    f(name);
    f(chats);
    f(add);
    f(accounts);
    f(status);
  }

  void light(int tab) {
    chats.setPrimary(tab == 0);
    add.setPrimary(tab == 1);
    accounts.setPrimary(tab == 2);
  }

  void show(const model& now) {
    std::size_t online = 0;
    for (const auto& [id, kept] : now.accounts())
      online += is_online(kept.state) ? 1 : 0;
    const std::size_t all = now.accounts().size();
    status.setText(all == 0 ? std::string() : std::format("{} of {} online", online, all));
  }

  void layoutChildren() {
    const skia::SkRect box = scene::inset(fState.contentBox(), 12.0f, 0.0f);
    scene::layout(plate, fState.contentBox());
    float x = 0.0f;
    const auto next = [&](auto& node, float after) {
      node.fState.arrange(x, 0.0f, scene::anchor::kCentreLeft, scene::anchor::kCentreLeft);
      scene::layout(node, box);
      x += node.bounds().width() + after;
    };
    next(name, 24.0f);
    next(chats, 8.0f);
    next(add, 8.0f);
    next(accounts, 16.0f);
    status.setMaxWidth(std::max(0.0f, box.width() - x));
    status.fState.arrange(0.0f, 0.0f, scene::anchor::kCentreRight, scene::anchor::kCentreRight);
    scene::layout(status, box);
  }
};

// The top bar, the conversations under it, and over them the panel that is
// open, if one is: it slides in from the right and covers them, and slides
// back out when closed. All in this one window, switched by the program
// between events.
template <class Actions>
struct window : scene::Node {
  using panel_type = std::variant<add_account_panel<Actions>, accounts_panel<Actions>>;

  Actions* actions = nullptr;
  nodes::Box<> backdrop{background};
  top_bar<Actions> bar;
  widgets::SlideOver<conversations_screen<Actions>, panel_type> body;

  explicit window(Actions* a) : actions(a), bar(a), body(a) {
    fState.apply({.fill = true});
    backdrop.apply({.fill = true});
    body.setSheetColour(background);
    this->light();
  }
  void forEachChild(auto&& f) {
    f(backdrop);
    f(bar);
    f(body);
  }

  [[nodiscard]] conversations_screen<Actions>& main() { return body.base(); }
  // The panel that is up, not on its way out.
  [[nodiscard]] panel_type* open_panel() { return body.shown(); }

  // A panel opened, in place of the one up if there is one.
  template <class Panel>
  Panel& open() {
    auto& made = std::get<Panel>(body.open(std::in_place_type<Panel>, actions));
    this->light();
    return made;
  }
  void close() {
    body.close();
    this->light();
  }
  // From the program, between events.
  void drop_closed() { body.dropClosed(); }

  // The top bar's button of what is up, lit.
  void light() {
    const panel_type* up = body.shown();
    bar.light(up ? std::visit([](const auto& one) { return one.kTab; }, *up) : body.base().kTab);
  }

  void layoutChildren() {
    const skia::SkRect box = fState.contentBox();
    scene::layout(backdrop, box);
    bar.fState.arrange(0.0f, 0.0f);
    scene::layout(bar, box);
    body.fState.arrange(0.0f, 0.0f);
    scene::layout(body, skia::SkRect::MakeLTRB(box.fLeft, bar.bounds().fBottom, box.fRight, box.fBottom));
  }
};

}  // namespace mux::ui
