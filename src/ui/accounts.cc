// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:accounts -- The accounts.
export module mux.ui:accounts;

import std;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes;
import skiff.widgets;
import mux.core;
import mux.config;
export import :add_account;

export namespace mux::ui {

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
struct account_entry : nodes::Stack {
  Actions* actions = nullptr;
  std::string address;
  bool selected = false;
  nodes::Text name;
  nodes::Text state;

  // Declared: its address over its protocol and state, on a plate lit
  // while it is the one chosen.
  account_entry(Actions* a, const config::account_t& saved, const model& now, bool is_selected)
      : actions(a), address(config::address_of(saved)), selected(is_selected),
        name(address, 15.0f, text_colour, true), state("", 13.0f, dim_colour) {
    this->setGap(4.0f);
    fState.apply({.fillX = true, .height = 52.0f, .padding = {7.0f, 10.0f, 7.0f, 10.0f}, .background = sidebar_colour, .selectedBackground = chosen_colour, .selected = selected});
    const auto [how, failed] = state_of(saved, now);
    state.setText(std::format("{} · {}", config::protocol_name(saved), how));
    state.setColour(failed ? error_colour : dim_colour);
    for (nodes::Text* each : {&name, &state}) {
      each->setElided(true);
      each->apply({.fillX = true});
    }
  }

  void forEachChild(auto&& f) {
    f(name);
    f(state);
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
struct account_editor : nodes::Stack {
  // Its address, then on or off and Remove, in a line.
  struct head_row : nodes::Stack {
    nodes::Text heading;
    nodes::Text enabled_label{"On", 13.0f, dim_colour};
    widgets::Toggle<flip_account<Actions>> enabled;
    widgets::Button<remove_account<Actions>> remove;
    head_row(Actions* a, const config::account_t& saved)
        : heading(config::address_of(saved), 20.0f, text_colour, true),
          enabled(flip_account<Actions>{a, config::address_of(saved)}),
          remove("Remove", remove_account<Actions>{a, config::address_of(saved)}) {
      this->setHorizontal();
      this->setGap(10.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY});
      heading.setElided(true);
      heading.apply({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
      enabled_label.apply({.alignSelf = scene::align::kMiddle});
      enabled.apply({.alignSelf = scene::align::kMiddle});
      enabled.setOnNow(config::enabled_of(saved));
      remove.apply({.width = 100.0f, .height = 32.0f});
    }
    void forEachChild(auto&& f) {
      f(heading);
      f(enabled_label);
      f(enabled);
      f(remove);
    }
  } head;
  nodes::Text state{"", 13.0f, dim_colour};
  account_form<Actions> form;

  account_editor(Actions* a, const config::account_t& saved) : head(a, saved), form(form_of(a, saved)) {
    fState.apply({.fill = true});
    this->setGap(6.0f);
    state.setElided(true);
    state.apply({.fillX = true, .margin = {0.0f, 0.0f, 14.0f, 0.0f}});
  }

  void forEachChild(auto&& f) {
    f(head);
    f(state);
    f(form);
  }

  // What the model says of it now, kept current without touching the form.
  void show(const config::account_t& saved, const model& now) {
    const auto [how, failed] = state_of(saved, now);
    state.setText(std::format("{} · {}", config::protocol_name(saved), how));
    state.setColour(failed ? error_colour : dim_colour);
    head.enabled.setOn(config::enabled_of(saved));
  }

  void say(std::string text, bool error) {
    std::visit([&](auto& one) { one.say(std::move(text), error); }, form);
  }
};

// A line with a switch on its right: its text, and the switch.
template <class Act>
struct switch_row : nodes::Stack {
  nodes::Text label;
  widgets::Toggle<Act> toggle;

  // Declared: the text taking the room, the switch at the end.
  switch_row(std::string text, Act what) : label(std::move(text), 15.0f, text_colour), toggle(std::move(what)) {
    this->setHorizontal();
    this->setGap(16.0f);
    fState.apply({.fillX = true, .height = row_item<nothing>::kHeight, .padding = {0.0f, 20.0f, 0.0f, 20.0f}});
    label.setElided(true);
    label.apply({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
    toggle.apply({.alignSelf = scene::align::kMiddle});
  }
  void forEachChild(auto&& f) {
    f(label);
    f(toggle);
  }
};

// A page of an account's settings chosen from its list.
template <class Actions>
struct choose_account_page {
  Actions* actions = nullptr;
  int page = 0;
  void operator()() const { actions->account_page(page); }
};

// An account's pages, in place of the list of accounts once one is chosen: a
// line for each page of its settings, the one shown lit.
template <class Actions>
struct account_pages : nodes::Stack {
  row_item<choose_account_page<Actions>> connection;
  row_item<choose_account_page<Actions>> privacy;
  row_item<choose_account_page<Actions>> proxy;

  explicit account_pages(Actions* a)
      : connection("Connection", {a, 0}, icon::sliders{}),
        privacy("Privacy", {a, 1}, icon::eye{}),
        proxy("Proxy", {a, 2}, icon::gear{}) {
    fState.apply({.padding = {6.0f, 0.0f, 0.0f, 0.0f}});
    this->light(0);
  }
  void light(int page) {
    connection.set_lit(page == 0);
    privacy.set_lit(page == 1);
    proxy.set_lit(page == 2);
  }
  void forEachChild(auto&& f) {
    f(connection);
    f(privacy);
    f(proxy);
  }
};

// A section's title on a settings page, as Gajim sets them: small, bold, dim.
inline nodes::Text section_title(std::string text) { return nodes::Text(std::move(text), 13.0f, dim_colour, true); }

// An account's Privacy page: whether it sends read receipts.
template <class Actions>
struct account_privacy : nodes::Stack {
  nodes::Text title = section_title("PRIVACY");
  switch_row<ask<Actions, &Actions::flip_account_receipts>> receipts;
  switch_row<ask<Actions, &Actions::flip_account_typing>> typing;
  nodes::Text note{"Off, the people you talk to through this account are not told when you have read their "
                   "messages, or that you are typing. Theirs are still shown, and receipts are still kept here.",
                   13.0f, dim_colour};

  account_privacy(Actions* a, bool receipts_on, bool typing_on)
      : receipts("Send read receipts", {a}), typing("Send typing notifications", {a}) {
    this->setGap(8.0f);
    note.apply({.fillX = true});
    fState.apply({.fill = true});
    note.setWrapped(true);
    receipts.toggle.setOnNow(receipts_on);
    typing.toggle.setOnNow(typing_on);
  }
  void show(bool receipts_on, bool typing_on) {
    receipts.toggle.setOn(receipts_on);
    typing.toggle.setOn(typing_on);
  }
  void say(std::string, bool) {}
  void forEachChild(auto&& f) {
    f(title);
    f(receipts);
    f(typing);
    f(note);
  }
};

// A proxy profile chosen for the chosen account: -1 for none.
template <class Actions>
struct choose_account_proxy {
  Actions* actions = nullptr;
  int index = -1;
  void operator()() const { actions->choose_account_proxy(index); }
};

// An account's Proxy page, as Gajim's: which of the program's proxy profiles
// it connects through, or none; and the way to the profiles themselves.
template <class Actions>
struct account_proxy : nodes::Stack {
  nodes::Text title = section_title("PROXY");
  std::vector<row_item<choose_account_proxy<Actions>>> choices;
  row_item<ask<Actions, &Actions::manage_proxies>> manage;

  account_proxy(Actions* a, const std::vector<config::proxy_settings>& all, const std::optional<std::string>& current)
      : manage("Manage proxies…", {a}, icon::gear{}) {
    title.apply({.margin = {0.0f, 0.0f, 4.0f, 0.0f}});
    manage.apply({.margin = {8.0f, 0.0f, 0.0f, 0.0f}});
    fState.apply({.fill = true});
    // An empty place where the dots are, so the names line up.
    choices.emplace_back("No proxy", choose_account_proxy<Actions>{a, -1}, icon::dot{skia::colorSetARGB(0, 0, 0, 0)},
                         !current.has_value());
    for (std::size_t i = 0; i < all.size(); ++i)
      choices.emplace_back(std::format("{} ({} {}:{})", all[i].name, config::label_of(config::proxy_kind_of(all[i].kind)),
                                       all[i].host, all[i].port),
                           choose_account_proxy<Actions>{a, static_cast<int>(i)}, icon::dot{proxy_colour(all[i].name)},
                           current && *current == all[i].name);
  }
  void show(bool) {}
  void say(std::string, bool) {}
  void forEachChild(auto&& f) {
    f(title);
    f(choices);
    f(manage);
  }
};

// The saved accounts down the side, and the chosen one's settings beside
// them.
template <class Actions>
struct accounts_panel : closes_on_escape<Actions> {
  static constexpr int kTab = 2;
  static constexpr float kListWidth = 280.0f;

  std::optional<std::string> selected;
  // The proxy profiles, for adding an account through one.
  std::vector<config::proxy_settings> proxies;

  // Its ← goes back from an account's pages to the list, and from the list
  // to the chats.
  page_header<ask<Actions, &Actions::accounts_back>, ask<Actions, &Actions::accounts_back>> header;
  // Under the header: the list down the side, and beside it what is chosen.
  struct body_row : nodes::Stack {
    struct side_column : nodes::Stack {
      row_item<ask<Actions, &Actions::open_new_account>> add;
      account_pages<Actions> pages;
      nodes::Text message{"", 13.0f, error_colour};
      nodes::ScrollContainer<nodes::Flow<std::vector<account_entry<Actions>>>> list{
          nodes::Flow<std::vector<account_entry<Actions>>>({.spacingY = 0.0f, .wrap = false}, {})};
      explicit side_column(Actions* a) : add("Add account", {a}, icon::plus{}), pages(a) {
        fState.apply({.fillY = true, .width = kListWidth, .background = sidebar_colour});
        pages.setVisible(false);
        pages.apply({.fillX = true, .autoSize = scene::axes::kY});
        message.setWrapped(true);
        message.apply({.fillX = true, .margin = scene::Margin::all(8.0f)});
        list.apply({.fillX = true, .grow = scene::axes::kY});
        std::get<0>(list.fChildren).apply({.fillX = true, .autoSize = scene::axes::kY});
      }
      void forEachChild(auto&& f) {
        f(add);
        f(pages);
        f(message);
        f(list);
      }
    } side;
    struct detail_column : nodes::Stack {
      // No account chosen, or the chosen one, or adding one.
      std::variant<nodes::Text, account_editor<Actions>, add_account_pane<Actions>, account_privacy<Actions>,
                   account_proxy<Actions>>
          detail{std::in_place_index<0>, "Choose an account.", 15.0f, dim_colour};
      detail_column() {
        fState.apply({.fillY = true, .grow = scene::axes::kX, .padding = {24.0f, 28.0f, 24.0f, 28.0f}});
      }
      void forEachChild(auto&& f) { f(detail); }
    } main;
    explicit body_row(Actions* a) : side(a) {
      this->setHorizontal();
      fState.apply({.fillX = true, .grow = scene::axes::kY});
    }
    void forEachChild(auto&& f) {
      f(side);
      f(main);
    }
  } body;
  row_item<ask<Actions, &Actions::open_new_account>>& add = body.side.add;
  account_pages<Actions>& pages = body.side.pages;
  nodes::Text& message = body.side.message;
  decltype(body.side.list)& list = body.side.list;
  decltype(body.main.detail)& detail = body.main.detail;

  // What is beside the list coming in when another is chosen, fading in.
  skiff::paint::Tween swap{1.0f, 200.0f, skiff::paint::movement::subtle{}};
  void begin_swap() {
    swap.jump(0.0f);
    swap.setTarget(1.0f);
    this->fade();
  }
  void fade() {
    const float value = swap.value();
    std::visit([value](auto& one) { one.fState.setAlpha(value); }, detail);
  }
  [[nodiscard]] bool settling() const { return swap.moving(); }
  void update(double now_ms) {
    if (swap.step(now_ms))
      this->fade();
  }

  explicit accounts_panel(Actions* a)
      : closes_on_escape<Actions>(a), header("Accounts", {a}, {a}, true, false), body(a) {
    this->fState.apply({.fill = true});
  }

  void forEachChild(auto&& f) {
    f(header);
    f(body);
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
    if (chosen) {
      if (auto* up = this->editor())
        up->show(*chosen, now);
    } else if (!this->adding()) {
      // Nothing to show beside the list: with no account at all, adding one.
      selected.reset();
      if (saved.empty())
        this->show_adding();
      else
        detail.template emplace<0>("Choose an account.", 15.0f, dim_colour);
    }
    this->invalidateLayout();
  }

  // An account's settings, brought up afresh.
  // One of the chosen account's pages beside the list: 0 Connection, 1
  // Privacy, 2 Proxy.
  void show_page(int page, const config::account_t& one, const model& now,
                 const std::vector<config::proxy_settings>& proxies = {}) {
    pages.light(page);
    if (page == 1) {
      detail.template emplace<3>(this->actions, config::read_receipts_of(one), config::send_typing_of(one));
    } else if (page == 2) {
      detail.template emplace<4>(this->actions, proxies, config::proxy_of(one));
    } else {
      detail.template emplace<1>(this->actions, one);
      std::get<1>(detail).show(one, now);
    }
    this->begin_swap();
    this->invalidateLayout();
  }
  [[nodiscard]] account_privacy<Actions>* privacy() {
    return std::visit(overloaded{[](account_privacy<Actions>& one) { return &one; },
                                 [](auto&) -> account_privacy<Actions>* { return nullptr; }},
                      detail);
  }
  [[nodiscard]] account_proxy<Actions>* proxy() {
    return std::visit(overloaded{[](account_proxy<Actions>& one) { return &one; },
                                 [](auto&) -> account_proxy<Actions>* { return nullptr; }},
                      detail);
  }
  [[nodiscard]] bool pages_open() const { return pages.visible(); }

  void select(const config::account_t& one, const model& now) {
    add.set_lit(false);
    selected = config::address_of(one);
    this->show_pages(true);
    this->show_page(0, one, now);
  }

  // Adding an account, beside the list.
  // The account list, or the chosen account's pages, down the side.
  void show_pages(bool shown) {
    pages.setVisible(shown);
    add.setVisible(!shown);
    list.setVisible(!shown);
    header.title.setText(shown && selected ? *selected : std::string("Accounts"));
    this->invalidateLayout();
  }
  // Back to the list of accounts, nothing chosen.
  void close_pages() {
    selected.reset();
    this->show_pages(false);
    detail.template emplace<0>("Choose an account.", 15.0f, dim_colour);
    this->begin_swap();
  }

  void show_adding() {
    this->show_pages(false);
    selected.reset();
    add.set_lit(true);
    detail.template emplace<2>(this->actions, proxies);
    this->begin_swap();
    this->invalidateLayout();
  }

  [[nodiscard]] account_editor<Actions>* editor() {
    return std::visit(overloaded{[](account_editor<Actions>& one) { return &one; },
                                 [](auto&) -> account_editor<Actions>* { return nullptr; }},
                      detail);
  }
  [[nodiscard]] add_account_pane<Actions>* adding() {
    return std::visit(overloaded{[](add_account_pane<Actions>& one) { return &one; },
                                 [](auto&) -> add_account_pane<Actions>* { return nullptr; }},
                      detail);
  }
  [[nodiscard]] xmpp_form<Actions>* xmpp() {
    return std::visit(overloaded{[](account_editor<Actions>& one) { return xmpp_form_in(one.form); },
                                 [](add_account_pane<Actions>& one) { return one.xmpp(); },
                                 [](auto&) -> xmpp_form<Actions>* { return nullptr; }},
                      detail);
  }

  void say(std::string text) {
    message.setText(std::move(text));
    this->invalidateLayout();
  }

};

}  // namespace mux::ui
