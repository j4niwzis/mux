// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:drawer -- The drawer.
export module mux.ui:drawer;

import std;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes;
import skiff.widgets;
import mux.core;
import mux.config;
export import :accounts;

export namespace mux::ui {

// ---- the drawer -------------------------------------------------------------------

// The levels of motion, as the accounts file names them.
inline constexpr std::array<std::string_view, 3> kMotions{"full", "reduced", "none"};

// A level of motion chosen in the settings.
template <class Actions>
struct choose_motion {
  Actions* actions = nullptr;
  std::string_view level;
  void operator()() const { actions->set_motion(std::string(level)); }
};

// An account in the drawer: a round avatar with its initials, its address,
// and its protocol and state. A press makes it the current account, whose
// chats are the ones shown; the current one is lit and ticked.
template <class Actions>
struct drawer_account : nodes::Stack {
  Actions* actions = nullptr;
  std::string address;
  bool current = false;
  avatar_mark face;
  two_lines texts;
  // The account whose chats are shown: a tick at the end.
  icon_mark tick{icon::check{}};

  // Declared: the avatar, the address over its state, the tick.
  drawer_account(Actions* a, const config::account_t& saved, const model& now, bool is_current)
      : actions(a), address(config::address_of(saved)), current(is_current), face(address, address, 38.0f),
        texts(address, "", 14.0f, 3.0f) {
    this->setHorizontal();
    this->setGap(14.0f);
    fState.apply({.fillX = true, .height = 56.0f, .padding = {0.0f, 16.0f, 0.0f, 16.0f}, .hoverBackground = chosen_colour, .selectedBackground = chosen_colour, .focusBackground = chosen_colour, .selected = current});
    const auto [how, failed] = state_of(saved, now);
    texts.state.setText(std::format("{} · {}", config::protocol_name(saved), how));
    texts.state.setColour(failed ? error_colour : dim_colour);
    tick.colour = accent_colour;
    tick.setVisible(current);
  }

  void forEachChild(auto&& f) {
    f(face);
    f(texts);
    f(tick);
  }

  [[nodiscard]] bool acceptsInput() const { return true; }
  [[nodiscard]] bool hoverChangesAppearance() const { return true; }
  [[nodiscard]] bool onClick(float, float) {
    actions->switch_account(address);
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

// What the drawer holds, as Telegram's does: the accounts with Manage
// accounts under them, then Settings and Quit, each a full-width line with
// its icon.
template <class Actions>
struct drawer_panel : nodes::Stack {
  nodes::Text title{"mux", 20.0f, text_colour, true};
  std::vector<drawer_account<Actions>> accounts;
  row_item<ask<Actions, &Actions::open_accounts>> manage;
  nodes::Box<> rule_1{chosen_colour};
  row_item<ask<Actions, &Actions::open_settings>> settings;
  row_item<ask<Actions, &Actions::quit>> quit;

  explicit drawer_panel(Actions* a)
      : manage("Manage accounts", {a}, icon::person{}),
        settings("Settings", {a}, icon::gear{}),
        quit("Quit", {a}, icon::power{}) {
    title.apply({.margin = {18.0f, 20.0f, 14.0f, 20.0f}});
    manage.apply({.margin = {0.0f, 0.0f, 6.0f, 0.0f}});
    rule_1.apply({.margin = {0.0f, 0.0f, 6.0f, 0.0f}});
    fState.apply({.fill = true});
    rule_1.apply({.fillX = true, .height = 1.0f});
  }

  void forEachChild(auto&& f) {
    f(title);
    f(accounts);
    f(manage);
    f(rule_1);
    f(settings);
    f(quit);
  }

  void show(Actions* a, const std::vector<config::account_t>& saved, const model& now, std::string_view current) {
    accounts.clear();
    for (const config::account_t& one : saved)
      accounts.emplace_back(a, one, now, config::address_of(one) == current);
    this->invalidateLayout();
  }

};

}  // namespace mux::ui
