// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui: the window's screens, as skiff nodes -- the conversations of every
// account down the side with the chosen one beside them, the login screen,
// the choice of protocol, a login screen for each protocol, and the accounts
// screen.
//
// The screens are views. What a control does is ask the program, through
// `Actions`, and the program changes the screens between events: a screen
// switched or a list rebuilt inside a click would destroy the control whose
// handler is still running.
export module mux.ui;

import std;
import skia;
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
//   void open_accounts()
//   void open_new_account()          -- the choice of protocol
//   void add_xmpp()
//   void add_matrix()
//   void edit_account(std::string address)
//   void toggle_advanced()
//   void toggle_plain()
//   void submit_login()
//   void cancel_login()
//   void set_enabled(std::string address, bool enabled)
//   void remove_account(std::string address)
//   void back()

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
  bool enabled = true;
  void operator()() const { actions->set_enabled(address, !enabled); }
};
template <class Actions>
struct edit_account {
  Actions* actions = nullptr;
  std::string address;
  void operator()() const { actions->edit_account(address); }
};
template <class Actions>
struct remove_account {
  Actions* actions = nullptr;
  std::string address;
  void operator()() const { actions->remove_account(address); }
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
  nodes::Text status{"mux", 13.0f, dim_colour};
  widgets::Button<ask<Actions, &Actions::open_accounts>> accounts_button;
  nodes::ScrollContainer<nodes::Flow<std::vector<account_block<Actions>>>> list{
      nodes::Flow<std::vector<account_block<Actions>>>({.spacingY = 2.0f, .wrap = false}, {})};
  nodes::Text title{"", 20.0f, text_colour, true};
  nodes::Text topic{"", 13.0f, dim_colour};
  nodes::ScrollContainer<nodes::Flow<std::vector<message_entry>>> timeline{
      nodes::Flow<std::vector<message_entry>>({.spacingY = 10.0f, .wrap = false}, {})};
  composer<Actions> line;

  static constexpr float kSidebarWidth = 280.0f;
  static constexpr float kPad = 8.0f;

  explicit conversations_screen(Actions* a)
      : actions(a), accounts_button("Accounts", {a}), line(a) {
    fState.apply({.fill = true});
    sidebar.apply({.fill = true});
    accounts_button.apply({.width = 96.0f, .height = 28.0f});
    std::get<0>(list.fChildren).apply({.fillX = true, .autoSize = scene::axes::kY});
    std::get<0>(timeline.fChildren).apply({.fillX = true, .autoSize = scene::axes::kY});
    title.apply({.fillX = true});
    topic.apply({.fillX = true});
  }

  void forEachChild(auto&& f) {
    f(sidebar);
    f(status);
    f(accounts_button);
    f(list);
    f(title);
    f(topic);
    f(timeline);
    f(line);
  }

  void layoutChildren() {
    const skia::SkRect box = fState.contentBox();
    const skia::SkRect side = skia::SkRect::MakeLTRB(box.fLeft, box.fTop, box.fLeft + kSidebarWidth, box.fBottom);
    const skia::SkRect main = skia::SkRect::MakeLTRB(side.fRight, box.fTop, box.fRight, box.fBottom);
    scene::layout(sidebar, side);

    const skia::SkRect head = skia::SkRect::MakeLTRB(side.fLeft + kPad, side.fTop + kPad, side.fRight - kPad, side.fTop + 40.0f);
    status.fState.arrange(0.0f, 0.0f, scene::anchor::kCentreLeft, scene::anchor::kCentreLeft);
    scene::layout(status, head);
    accounts_button.fState.arrange(0.0f, 0.0f, scene::anchor::kCentreRight, scene::anchor::kCentreRight);
    scene::layout(accounts_button, head);
    list.apply({.width = side.width() - 2 * kPad, .height = std::max(0.0f, side.fBottom - head.fBottom - 2 * kPad)});
    list.fState.arrange(side.fLeft + kPad - box.fLeft, head.fBottom + kPad - box.fTop);
    scene::layout(list, box);

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
    std::size_t online = 0;
    for (const auto& [id, kept] : now.accounts()) {
      if (is_online(kept.state))
        ++online;
      std::vector<conversation_row<Actions>> rows;
      for (const auto& [key, one] : kept.conversations)
        rows.emplace_back(actions, one, chosen && *chosen == one.id);
      auto& block = blocks.emplace_back(nodes::FlowOptions{.spacingY = 2.0f, .wrap = false},
                                        scene::make<nodes::Text>({.fillX = true}, id.address, 12.0f, dim_colour, true),
                                        std::move(rows));
      block.apply({.fillX = true, .autoSize = scene::axes::kY});
    }
    const std::size_t accounts = now.accounts().size();
    status.setText(std::format("{} of {} account{} online", online, accounts, accounts == 1 ? "" : "s"));
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

// ---- logging in ---------------------------------------------------------------

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

// Which protocol a new account speaks: each has its own screen.
template <class Actions>
struct protocol_choice : scene::Node {
  nodes::Text title{"Add an account", 22.0f, text_colour, true};
  widgets::Button<ask<Actions, &Actions::add_xmpp>> xmpp;
  nodes::Text xmpp_note{"An address like user@example.com, on a server such as Prosody or ejabberd.", 13.0f, dim_colour};
  widgets::Button<ask<Actions, &Actions::add_matrix>> matrix;
  nodes::Text matrix_note{"A user ID like @user:example.org, on a homeserver such as Synapse or Conduit.", 13.0f,
                          dim_colour};
  widgets::Button<ask<Actions, &Actions::open_accounts>> cancel;

  static constexpr float kWidth = 420.0f;

  protocol_choice(Actions* a, bool cancellable) : xmpp("XMPP", {a}), matrix("Matrix", {a}), cancel("Cancel", {a}) {
    fState.apply({.fill = true});
    xmpp.apply({.fillX = true, .height = 40.0f});
    matrix.apply({.fillX = true, .height = 40.0f});
    cancel.apply({.width = 120.0f, .height = 36.0f});
    cancel.setVisible(cancellable);
    for (nodes::Text* note : {&xmpp_note, &matrix_note}) {
      note->setWrapped(true);
      note->apply({.fillX = true});
    }
  }

  void forEachChild(auto&& f) {
    f(title);
    f(xmpp);
    f(xmpp_note);
    f(matrix);
    f(matrix_note);
    f(cancel);
  }

  void layoutChildren() {
    column_stack stack{form_column(fState.contentBox(), kWidth, 48.0f)};
    stack(title, 20.0f);
    stack(xmpp, 6.0f);
    stack(xmpp_note, 20.0f);
    stack(matrix, 6.0f);
    stack(matrix_note, 24.0f);
    stack(cancel, 0.0f);
  }
};

// What every login form ends with: what went wrong or what is happening, and
// its buttons. Enter in any of the form's fields logs in.
template <class Actions>
struct form_end {
  nodes::Text message{"", 13.0f, error_colour};
  widgets::Button<ask<Actions, &Actions::submit_login>> log_in;
  // Back to the choice of protocol when adding, back to the accounts when
  // editing.
  widgets::Button<ask<Actions, &Actions::cancel_login>> cancel;

  form_end(Actions* a, bool editing, bool cancellable)
      : log_in(editing ? "Save" : "Log in", {a}), cancel(editing ? "Cancel" : "Back", {a}) {
    log_in.setPrimary(true);
    log_in.apply({.width = 120.0f, .height = 36.0f});
    cancel.apply({.width = 120.0f, .height = 36.0f});
    cancel.setVisible(cancellable);
    message.setWrapped(true);
    message.apply({.fillX = true});
  }

  void each(auto&& f) {
    f(message);
    f(log_in);
    f(cancel);
  }

  void say(std::string text, bool error) {
    message.setText(std::move(text));
    message.setColour(error ? error_colour : dim_colour);
  }

  void place(column_stack& stack) {
    stack(message, 12.0f);
    log_in.fState.arrange(0.0f, stack.y);
    scene::layout(log_in, stack.column);
    cancel.fState.arrange(log_in.bounds().width() + 12.0f, stack.y);
    scene::layout(cancel, stack.column);
  }
};

// Text typed into an optional: nothing when the field is empty.
[[nodiscard]] inline std::optional<std::string> typed_or_nothing(const std::string& text) {
  if (text.empty())
    return std::nullopt;
  return text;
}

// An XMPP account: its JID and password, and "Advanced" folds out the
// resource and where to connect.
template <class Actions>
struct xmpp_login : scene::Node {
  Actions* actions = nullptr;
  // The address the account was saved under, when this is an edit of one.
  std::optional<std::string> editing;
  bool advanced = false;

  nodes::Text title;
  field address{"Address (JID)", "user@example.com"};
  field password{"Password", "Password"};
  widgets::Button<ask<Actions, &Actions::toggle_advanced>> advanced_button;
  field resource{"Resource", "mux", "mux"};
  field host{"Host", "from the domain's SRV records"};
  field port{"Port", "5222"};
  widgets::Toggle<ask<Actions, &Actions::toggle_plain>> plain;
  nodes::Text plain_label{"Allow PLAIN without TLS. Only for a test server on this machine: never over a network.",
                          13.0f, error_colour};
  form_end<Actions> end;

  static constexpr float kWidth = 420.0f;

  xmpp_login(Actions* a, const std::optional<config::xmpp_account>& from, bool cancellable)
      : actions(a),
        title(from ? "Edit XMPP account" : "Add an XMPP account", 22.0f, text_colour, true),
        advanced_button("Advanced", {a}),
        plain({a}),
        end(a, from.has_value(), cancellable) {
    fState.apply({.fill = true});
    password.box.setMasked(true);
    advanced_button.apply({.width = 120.0f, .height = 36.0f});
    plain_label.setWrapped(true);
    if (from) {
      editing = from->address;
      address.box.setText(from->address);
      password.box.setText(from->password);
      resource.box.setText(from->resource);
      if (from->host)
        host.box.setText(*from->host);
      if (from->port)
        port.box.setText(std::to_string(*from->port));
      plain.setOn(from->plain_without_tls);
      advanced = from->resource != "mux" || from->host || from->port || from->plain_without_tls;
    }
    this->show_advanced(advanced);
  }

  void forEachChild(auto&& f) {
    f(title);
    f(address);
    f(password);
    f(advanced_button);
    f(resource);
    f(host);
    f(port);
    f(plain);
    f(plain_label);
    end.each(f);
  }

  void show_advanced(bool shown) {
    advanced = shown;
    resource.setVisible(shown);
    host.setVisible(shown);
    port.setVisible(shown);
    plain.setVisible(shown);
    plain_label.setVisible(shown);
    this->invalidateLayout();
  }

  void flip_plain() { plain.setOn(!plain.on()); }

  // The account as typed, or what is wrong with it. What is folded away is
  // kept as it is: folding is not clearing.
  [[nodiscard]] std::expected<config::xmpp_account, std::string> account() const {
    config::xmpp_account out{.address = address.box.text(),
                             .password = password.box.text(),
                             .resource = resource.box.text(),
                             .host = typed_or_nothing(host.box.text()),
                             .plain_without_tls = plain.on()};
    if (const std::string& text = port.box.text(); !text.empty()) {
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
    column_stack stack{form_column(fState.contentBox(), kWidth, 48.0f)};
    stack(title, 20.0f);
    stack(address, 12.0f);
    stack(password, 12.0f);
    stack(advanced_button, 12.0f);
    stack(resource, 8.0f);
    stack(host, 8.0f);
    stack(port, 8.0f);
    if (plain.visible()) {
      plain.fState.arrange(0.0f, stack.y);
      scene::layout(plain, stack.column);
      const float beside = plain.bounds().width() + 10.0f;
      plain_label.setMaxWidth(std::max(0.0f, stack.column.width() - beside));
      plain_label.fState.arrange(beside, stack.y);
      scene::layout(plain_label, stack.column);
      stack.y += std::max(plain.bounds().height(), plain_label.bounds().height()) + 12.0f;
    }
    end.place(stack);
  }
};

// A Matrix account: its user ID and password, the homeserver (found through
// the server's .well-known when left empty) and what this device is called.
template <class Actions>
struct matrix_login : scene::Node {
  Actions* actions = nullptr;
  std::optional<std::string> editing;

  nodes::Text title;
  field user_id{"User ID", "@user:example.org"};
  field password{"Password", "Password"};
  field homeserver{"Homeserver", "found through the server's .well-known"};
  field device_name{"Device name", "mux", "mux"};
  form_end<Actions> end;

  static constexpr float kWidth = 420.0f;

  matrix_login(Actions* a, const std::optional<config::matrix_account>& from, bool cancellable)
      : actions(a),
        title(from ? "Edit Matrix account" : "Add a Matrix account", 22.0f, text_colour, true),
        end(a, from.has_value(), cancellable) {
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
    f(title);
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
    column_stack stack{form_column(fState.contentBox(), kWidth, 48.0f)};
    stack(title, 20.0f);
    stack(user_id, 12.0f);
    stack(password, 12.0f);
    stack(homeserver, 12.0f);
    stack(device_name, 12.0f);
    end.place(stack);
  }
};

// ---- the accounts -------------------------------------------------------------

// One saved account: its address, protocol and state, on or off, edit and remove.
template <class Actions>
struct account_line : scene::Node {
  nodes::Text address;
  nodes::Text state;
  widgets::Toggle<flip_account<Actions>> enabled;
  widgets::Button<edit_account<Actions>> edit;
  widgets::Button<remove_account<Actions>> remove;

  account_line(Actions* a, const config::account_t& saved, std::string how, bool failed)
      : address(config::address_of(saved), 15.0f, text_colour, true),
        state(std::format("{} · {}", config::protocol_name(saved), how), 13.0f, failed ? error_colour : dim_colour),
        enabled(flip_account<Actions>{a, config::address_of(saved), config::enabled_of(saved)}),
        edit("Edit", edit_account<Actions>{a, config::address_of(saved)}),
        remove("Remove", remove_account<Actions>{a, config::address_of(saved)}) {
    fState.apply({.fillX = true, .height = 56.0f});
    enabled.setOn(config::enabled_of(saved));
    edit.apply({.width = 80.0f, .height = 32.0f});
    remove.apply({.width = 90.0f, .height = 32.0f});
    state.setElided(true);
  }

  void forEachChild(auto&& f) {
    f(address);
    f(state);
    f(enabled);
    f(edit);
    f(remove);
  }
  void layoutChildren() {
    const skia::SkRect box = fState.contentBox();
    remove.fState.arrange(0.0f, 0.0f, scene::anchor::kCentreRight, scene::anchor::kCentreRight);
    scene::layout(remove, box);
    edit.fState.arrange(-(remove.bounds().width() + 8.0f), 0.0f, scene::anchor::kCentreRight, scene::anchor::kCentreRight);
    scene::layout(edit, box);
    enabled.fState.arrange(-(remove.bounds().width() + edit.bounds().width() + 24.0f), 0.0f,
                           scene::anchor::kCentreRight, scene::anchor::kCentreRight);
    scene::layout(enabled, box);
    const float room = std::max(0.0f, enabled.bounds().fLeft - box.fLeft - 12.0f);
    address.setMaxWidth(room);
    address.fState.arrange(0.0f, 6.0f);
    scene::layout(address, box);
    state.setMaxWidth(room);
    state.fState.arrange(0.0f, address.bounds().height() + 8.0f);
    scene::layout(state, box);
  }
};

template <class Actions>
struct accounts_screen : scene::Node {
  Actions* actions = nullptr;
  nodes::Text title{"Accounts", 22.0f, text_colour, true};
  nodes::Text empty{"No accounts yet.", 14.0f, dim_colour};
  nodes::ScrollContainer<nodes::Flow<std::vector<account_line<Actions>>>> list{
      nodes::Flow<std::vector<account_line<Actions>>>({.spacingY = 8.0f, .wrap = false}, {})};
  nodes::Text message{"", 13.0f, error_colour};
  widgets::Button<ask<Actions, &Actions::open_new_account>> add;
  widgets::Button<ask<Actions, &Actions::back>> back;

  static constexpr float kWidth = 640.0f;

  explicit accounts_screen(Actions* a)
      : actions(a), add("Add account", {a}), back("Back", {a}) {
    fState.apply({.fill = true});
    add.setPrimary(true);
    add.apply({.width = 140.0f, .height = 36.0f});
    back.apply({.width = 100.0f, .height = 36.0f});
    std::get<0>(list.fChildren).apply({.fillX = true, .autoSize = scene::axes::kY});
    message.setWrapped(true);
    message.apply({.fillX = true});
  }

  void forEachChild(auto&& f) {
    f(title);
    f(empty);
    f(list);
    f(message);
    f(add);
    f(back);
  }

  // The saved accounts, with what the model says of each.
  void show(const std::vector<config::account_t>& saved, const model& now) {
    auto& lines = std::get<0>(std::get<0>(list.fChildren).fChildren);
    lines.clear();
    for (const config::account_t& one : saved) {
      const std::string& address = config::address_of(one);
      const bool enabled = config::enabled_of(one);
      std::string how = enabled ? "offline" : "disabled";
      bool failed = false;
      if (auto found = now.accounts().find(account_id{protocol_of(address), address});
          enabled && found != now.accounts().end()) {
        how = std::visit(overloaded{[](const connection::offline&) { return std::string("offline"); },
                                    [](const connection::connecting&) { return std::string("connecting…"); },
                                    [](const connection::online&) { return std::string("online"); },
                                    [&failed](const connection::failed& why) {
                                      failed = true;
                                      return "failed: " + why.error;
                                    }},
                         found->second.state);
      }
      lines.emplace_back(actions, one, std::move(how), failed);
    }
    empty.setVisible(saved.empty());
    this->invalidateLayout();
  }

  void say(std::string text) { message.setText(std::move(text)); }

  void layoutChildren() {
    const skia::SkRect box = fState.contentBox();
    const float width = std::min(kWidth, box.width() - 32.0f);
    const skia::SkRect column = skia::SkRect::MakeXYWH(box.centerX() - width * 0.5f, box.fTop + 40.0f, width, box.height() - 80.0f);
    title.fState.arrange(0.0f, 0.0f);
    scene::layout(title, column);
    float y = title.bounds().height() + 16.0f;
    add.fState.arrange(0.0f, y);
    scene::layout(add, column);
    back.fState.arrange(add.bounds().width() + 12.0f, y);
    scene::layout(back, column);
    y += add.bounds().height() + 12.0f;
    if (!message.text().empty()) {
      message.fState.arrange(0.0f, y);
      scene::layout(message, column);
      y += message.bounds().height() + 8.0f;
    }
    empty.fState.arrange(0.0f, y);
    scene::layout(empty, column);
    list.apply({.width = column.width(), .height = std::max(0.0f, column.height() - y)});
    list.fState.arrange(column.fLeft - box.fLeft, column.fTop - box.fTop + y);
    scene::layout(list, box);
  }
};

// ---- the window -----------------------------------------------------------------

// Whichever screen is up. Switched by the program between events.
template <class Actions>
struct window : scene::Node {
  nodes::Box<> backdrop{background};
  std::variant<conversations_screen<Actions>, protocol_choice<Actions>, xmpp_login<Actions>, matrix_login<Actions>,
               accounts_screen<Actions>>
      body;

  explicit window(Actions* a) : body(std::in_place_index<0>, a) {
    fState.apply({.fill = true});
    backdrop.apply({.fill = true});
  }
  void forEachChild(auto&& f) {
    f(backdrop);
    f(body);
  }
};

}  // namespace mux::ui
