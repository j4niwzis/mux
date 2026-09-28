// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui: the window's screens, as skiff nodes -- the conversations of every
// account down the side with the chosen one beside them, the login screen,
// and the accounts screen.
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

// The protocol an address speaks.
[[nodiscard]] inline protocol_t protocol_of(std::string_view address) {
  return config::is_matrix(address) ? protocol_t{protocol::matrix{}} : protocol_t{protocol::xmpp{}};
}

// What the screens ask of the program. Each is a request: the program acts on
// it between events.
//
//   void choose(const conversation_id&)
//   void send(const conversation_id&, std::string)
//   void open_accounts()
//   void open_login(std::optional<std::string> editing)
//   void toggle_advanced()
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
  void operator()() const { actions->open_login(address); }
};
template <class Actions>
struct remove_account {
  Actions* actions = nullptr;
  std::string address;
  void operator()() const { actions->remove_account(address); }
};
template <class Actions>
struct add_account {
  Actions* actions = nullptr;
  void operator()() const { actions->open_login(std::nullopt); }
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

// An account typed in: the address says the protocol, and "Advanced" folds
// out where to connect. Enter in either field logs in.
template <class Actions>
struct login_screen : scene::Node {
  Actions* actions = nullptr;
  // The address the account was saved under, when this is an edit of one.
  std::optional<std::string> editing;
  bool advanced = false;
  bool can_cancel = false;

  nodes::Text title{"Add an account", 22.0f, text_colour, true};
  field address{"Address", "user@example.com or @user:example.org"};
  nodes::Text protocol{"", 13.0f, dim_colour};
  field password{"Password", "Password"};
  widgets::Button<ask<Actions, &Actions::toggle_advanced>> advanced_button;
  field host{"Host (XMPP)", "from the domain's SRV records"};
  field port{"Port (XMPP)", "5222"};
  field homeserver{"Homeserver (Matrix)", "from the server's .well-known"};
  nodes::Text message{"", 13.0f, error_colour};
  widgets::Button<ask<Actions, &Actions::submit_login>> log_in;
  widgets::Button<ask<Actions, &Actions::cancel_login>> cancel;

  static constexpr float kWidth = 420.0f;

  login_screen(Actions* a, const std::optional<config::saved_account>& from, bool cancellable)
      : actions(a), can_cancel(cancellable),
        advanced_button("Advanced", {a}),
        log_in("Log in", {a}),
        cancel("Cancel", {a}) {
    fState.apply({.fill = true});
    password.box.setMasked(true);
    log_in.setPrimary(true);
    log_in.apply({.width = 120.0f, .height = 36.0f});
    cancel.apply({.width = 120.0f, .height = 36.0f});
    advanced_button.apply({.width = 120.0f, .height = 36.0f});
    cancel.setVisible(can_cancel);
    message.setWrapped(true);
    message.apply({.fillX = true});
    if (from) {
      editing = from->address;
      title.setText("Edit account");
      address.box.setText(from->address);
      password.box.setText(from->password);
      if (from->host)
        host.box.setText(*from->host);
      if (from->port)
        port.box.setText(std::to_string(*from->port));
      if (from->homeserver)
        homeserver.box.setText(*from->homeserver);
      advanced = from->host || from->port || from->homeserver;
    }
    this->show_advanced(advanced);
    this->update(0.0);
  }

  void forEachChild(auto&& f) {
    f(title);
    f(address);
    f(protocol);
    f(password);
    f(advanced_button);
    f(host);
    f(port);
    f(homeserver);
    f(message);
    f(log_in);
    f(cancel);
  }

  void show_advanced(bool shown) {
    advanced = shown;
    const bool matrix = config::is_matrix(address.box.text());
    host.setVisible(shown && !matrix);
    port.setVisible(shown && !matrix);
    homeserver.setVisible(shown && matrix);
    this->invalidateLayout();
  }

  // What the address says, kept current as it is typed.
  void update(double) {
    const std::string& typed = address.box.text();
    const std::string said = typed.empty() ? "" : config::is_matrix(typed) ? "Matrix" : "XMPP";
    if (said != protocol.text()) {
      protocol.setText(said);
      this->show_advanced(advanced);
    }
  }

  // The account as typed, or what is wrong with it.
  [[nodiscard]] std::expected<config::saved_account, std::string> account() const {
    config::saved_account out;
    out.address = address.box.text();
    out.password = password.box.text();
    const bool matrix = config::is_matrix(out.address);
    if (advanced && !matrix) {
      if (!host.box.text().empty())
        out.host = host.box.text();
      if (!port.box.text().empty()) {
        std::int64_t number = 0;
        const std::string& text = port.box.text();
        const auto [end, failed] = std::from_chars(text.data(), text.data() + text.size(), number);
        if (failed != std::errc{} || end != text.data() + text.size())
          return std::unexpected("A port is a number from 1 to 65535");
        out.port = number;
      }
    }
    if (advanced && matrix && !homeserver.box.text().empty())
      out.homeserver = homeserver.box.text();
    if (auto wrong = config::check(out))
      return std::unexpected(*wrong);
    return out;
  }

  void say(std::string text, bool error) {
    message.setText(std::move(text));
    message.setColour(error ? error_colour : dim_colour);
  }

  // Enter in a field logs in: seen here, on its way back up from the field.
  using Node::onKey;
  void onKey(scene::phase::bubble, const scene::key::down& press, scene::Reply& reply) {
    if (press.key == scene::keys::kEnter) {
      actions->submit_login();
      reply.handle();
    }
  }

  void layoutChildren() {
    const skia::SkRect box = fState.contentBox();
    const float width = std::min(kWidth, box.width() - 32.0f);
    const skia::SkRect column = skia::SkRect::MakeXYWH(box.centerX() - width * 0.5f, box.fTop + 48.0f, width, box.height() - 48.0f);
    float y = 0.0f;
    const auto stack = [&](auto& node, float after) {
      if (!node.visible())
        return;
      node.fState.arrange(0.0f, y);
      scene::layout(node, column);
      y += node.bounds().height() + after;
    };
    stack(title, 20.0f);
    stack(address, 0.0f);
    stack(protocol, 12.0f);
    stack(password, 12.0f);
    stack(advanced_button, 12.0f);
    stack(host, 8.0f);
    stack(port, 8.0f);
    stack(homeserver, 8.0f);
    stack(message, 12.0f);
    log_in.fState.arrange(0.0f, y);
    scene::layout(log_in, column);
    cancel.fState.arrange(log_in.bounds().width() + 12.0f, y);
    scene::layout(cancel, column);
  }
};

// ---- the accounts -------------------------------------------------------------

// One saved account: its address and state, on or off, edit and remove.
template <class Actions>
struct account_line : scene::Node {
  nodes::Text address;
  nodes::Text state;
  widgets::Toggle<flip_account<Actions>> enabled;
  widgets::Button<edit_account<Actions>> edit;
  widgets::Button<remove_account<Actions>> remove;

  account_line(Actions* a, const config::saved_account& saved, std::string how, bool failed)
      : address(saved.address, 15.0f, text_colour, true),
        state(std::move(how), 13.0f, failed ? error_colour : dim_colour),
        enabled(flip_account<Actions>{a, saved.address, saved.enabled}),
        edit("Edit", edit_account<Actions>{a, saved.address}),
        remove("Remove", remove_account<Actions>{a, saved.address}) {
    fState.apply({.fillX = true, .height = 56.0f});
    enabled.setOn(saved.enabled);
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
  widgets::Button<add_account<Actions>> add;
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
  void show(const std::vector<config::saved_account>& saved, const model& now) {
    auto& lines = std::get<0>(std::get<0>(list.fChildren).fChildren);
    lines.clear();
    for (const config::saved_account& one : saved) {
      std::string how = one.enabled ? "offline" : "disabled";
      bool failed = false;
      if (auto found = now.accounts().find(account_id{protocol_of(one.address), one.address});
          one.enabled && found != now.accounts().end()) {
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
  std::variant<conversations_screen<Actions>, login_screen<Actions>, accounts_screen<Actions>> body;

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
