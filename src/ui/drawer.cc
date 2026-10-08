// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:drawer -- The drawer.
export module mux.ui:drawer;

import std;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes.box;
import skiff.nodes.flow;
import skiff.nodes.text;
import mux.core;
import mux.config;
import :base;
import :icons;
import :controls;
import :accounts;

export namespace mux::ui {

// ---- the drawer -------------------------------------------------------------------

// An account in the drawer: a round avatar with its initials, its address,
// and its protocol and state. A press makes it the current account, whose
// chats are the ones shown; the current one is lit and ticked.
template <class Actions>
struct drawer_account : nodes::Stack {
  // What its handlers ask for, returned.
  using Answer = ::mux::ui::request::switch_account;
  std::string address;
  bool current = false;
  struct parts_t {
    avatar_mark face;
    two_lines texts;
    // The account whose chats are shown: a tick at the end.
    icon_mark tick;
  } parts;

  // Declared: the avatar, the address over its state, the tick.
  drawer_account(const ui_needs<Actions>& n, const config::account_t& saved, const model& now, bool is_current)
      : address(config::address_of(saved)), current(is_current),
        parts{.face = avatar_mark(address, address, 38.0f), .texts = two_lines(*n.colours, address, "", 14.0f, 3.0f),
              .tick = icon_mark(*n.colours, icon::check{})} {
    const palette& colours = *n.colours;
    this->setHorizontal();
    this->setGap(14.0f);
    fState.apply({.fillX = true, .height = 56.0f, .padding = {0.0f, 16.0f, 0.0f, 16.0f}, .hoverBackground = colours.chosen, .selectedBackground = colours.chosen, .focusBackground = colours.chosen, .selected = current});
    const auto [how, failed] = state_of(saved, now);
    parts.texts.parts.state.setText(std::format("{} · {}", config::protocol_name(saved), how));
    parts.texts.parts.state.setColour(failed ? colours.error : colours.dim);
    parts.tick.setColour(colours.accent);
    parts.tick.setVisible(current);
  }

  [[nodiscard]] bool acceptsInput() const { return true; }
  [[nodiscard]] bool hoverChangesAppearance() const { return true; }
  std::optional<Answer> onClick(float, float) {
    return ::mux::ui::request::switch_account{address};
  }
  [[nodiscard]] scene::Semantics semantics() const {
    scene::Semantics out;
    out.fRole = scene::semantic_role::list_item{};
    out.fLabel = address;
    out.fActions = {scene::semantic_action::focus{}, scene::semantic_action::activate{}};
    return out;
  }
};

// What the drawer holds, as Telegram's does: the accounts with Manage
// accounts under them, then Settings and Quit, each a full-width line with
// its icon.
template <class Actions>
struct drawer_panel : nodes::Stack {
  using manage_row = row_item<sends<::mux::ui::request::open_accounts>>;
  using new_chat_row = row_item<sends<::mux::ui::request::open_new_chat>>;
  using new_room_row = row_item<sends<::mux::ui::request::open_new_room>>;
  using settings_row = row_item<sends<::mux::ui::request::open_settings>>;
  using quit_row = row_item<sends<::mux::ui::request::quit>>;
  // What it was handed, for the accounts it lists.
  ui_needs<Actions> needs_;
  struct parts_t {
    nodes::Text title;
    std::vector<drawer_account<Actions>> accounts;
    manage_row manage;
    nodes::Box<> rule_1;
    new_chat_row new_chat;
    new_room_row new_room;
    settings_row settings;
    quit_row quit;
  } parts;

  drawer_panel(const ui_needs<Actions>& n)
      : needs_(n),
        parts{.title = nodes::Text("mux", 20.0f, n.colours->text, true),
              .manage = manage_row(*n.colours, "Manage accounts", {}, icon::person{}),
              .rule_1 = nodes::Box<>(n.colours->chosen),
              .new_chat = new_chat_row(*n.colours, "Start new chat", {}, icon::person{}),
              .new_room = new_room_row(*n.colours, "New room", {}, icon::people{}),
              .settings = settings_row(*n.colours, "Settings", {}, icon::gear{}),
              .quit = quit_row(*n.colours, "Quit", {}, icon::power{})} {
    parts.title.apply({.margin = {18.0f, 20.0f, 14.0f, 20.0f}});
    parts.manage.apply({.margin = {0.0f, 0.0f, 6.0f, 0.0f}});
    parts.rule_1.apply({.margin = {0.0f, 0.0f, 6.0f, 0.0f}});
    fState.apply({.fill = true});
    parts.rule_1.apply({.fillX = true, .height = 1.0f});
  }

  template <std::ranges::input_range Saved>
  void show(const Saved& saved, const model& now, std::string_view current) {
    parts.accounts.clear();
    for (const config::account_t& one : saved)
      parts.accounts.emplace_back(needs_, one, now, config::address_of(one) == current);
    this->invalidateLayout();
  }

};

}  // namespace mux::ui
