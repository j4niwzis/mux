// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:add_account -- Adding an account.
export module mux.ui:add_account;

import std;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes.flow;
import skiff.nodes.text;
import mux.core;
import mux.config;
import mux.protocols;
import :base;
import :controls;
import :forms;

export namespace mux::ui {

// ---- adding an account ------------------------------------------------------------

// A proxy chosen for an account being added: -1 for none.
template <class Actions>
struct choose_new_proxy {
  Actions* actions = nullptr;
  int index = -1;
  void operator()() const { actions->choose_new_proxy(index); }
};

// Adding an account, beside the list of them: XMPP or Matrix at the top, and
// that protocol's form under it.
template <class Actions>
struct add_account_pane : nodes::Stack {
  Actions* actions = nullptr;
  // A segment a protocol, in a thin frame -- from the list, each named as
  // its protocol names itself.
  struct pick_protocol {
    Actions* actions = nullptr;
    protocol_t speaks;
    void operator()() const { actions->add_account_of(speaks); }
  };
  struct protocol_switch : nodes::Stack {
    struct parts_t {
      std::vector<segment<pick_protocol>> each;
    } parts;
    template <class... Tags>
    void make(const palette& colours, Actions* a, protocol_list<Tags...>) {
      (parts.each.emplace_back(colours, std::string(protocol_name(config::kept_of<Tags>{})), pick_protocol{a, protocol_t{Tags{}}}), ...);
    }
    protocol_switch(const palette& colours, Actions* a) {
      this->make(colours, a, protocols{});
      this->setHorizontal();
      this->setGap(1.0f);
      fState.apply({.autoSize = scene::axes::kBoth, .padding = {1.0f, 1.0f, 1.0f, 1.0f}, .background = colours.chosen});
    }
  };
  // The proxy the new account goes through: none, or one of the profiles --
  // and, always there, a way to make one.
  struct proxy_row : nodes::Stack {
    using add_button = segment<ask<Actions, &Actions::manage_proxies>>;
    struct parts_t {
      nodes::Text title;
      std::vector<segment<choose_new_proxy<Actions>>> choices;
      add_button add;
    } parts;
    proxy_row(const palette& colours, Actions* a)
        : parts{.title = nodes::Text("Proxy", 13.0f, colours.dim), .add = add_button(colours, "Add proxy\u2026", {a})} {
      this->setHorizontal();
      this->setGap(4.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY});
      parts.title.apply({.alignSelf = scene::align::kMiddle});
    }
  };
  // The colours it is made in, for the proxies it lists as they change.
  const palette* colours_ = nullptr;
  struct parts_t {
    protocol_switch tabs;
    nodes::Text note;
    proxy_row proxies_row;
    account_form<Actions> form;
  } parts;
  std::vector<std::string> proxy_names;
  std::optional<std::string> proxy;
  // The form coming in when the protocol changes, fading in.
  skiff::paint::Tween swap{1.0f, 200.0f, skiff::paint::movement::subtle{}};

  add_account_pane(const ui_needs<Actions>& n, const std::vector<config::proxy_settings>& proxies)
      : add_account_pane(n, n.actions, proxies) {}
  add_account_pane(const ui_needs<Actions>& n, Actions* a, const std::vector<config::proxy_settings>& proxies)
      : actions(a),
        colours_(n.colours),
        parts{.tabs = protocol_switch(*n.colours, a),
              .note = nodes::Text("", 13.0f, n.colours->dim),
              .proxies_row = proxy_row(*n.colours, a),
              .form = account_form<Actions>(std::in_place_index<0>, a, *n.colours, std::nullopt)} {
    fState.apply({.fill = true});
    this->setGap(12.0f);
    parts.note.setWrapped(true);
    parts.note.apply({.fillX = true});
    this->set_proxies(proxies);
    this->light();
  }

  // A protocol's form, blank, in place of the one up.
  void show(const protocol_t& speaks) {
    spl::visit([this](auto of) {
      parts.form.template emplace<form_of_t<decltype(of), Actions>>(this->actions, *colours_, std::nullopt);
    }, speaks);
    this->begin_swap();
    this->light();
  }
  // The profiles to choose from, as they are now: one made or dropped in the
  // settings meanwhile is there, and what was chosen stays so while it is.
  void set_proxies(const std::vector<config::proxy_settings>& proxies) {
    std::vector<std::string> names;
    for (const auto& one : proxies)
      names.push_back(one.name);
    auto& choices = parts.proxies_row.parts.choices;
    if (names == proxy_names && !choices.empty())
      return;
    const auto chosen = proxy;
    proxy_names = std::move(names);
    choices.clear();
    choices.emplace_back(*colours_, "None", choose_new_proxy<Actions>{actions, -1});
    for (std::size_t k = 0; k < proxy_names.size(); ++k)
      choices.emplace_back(*colours_, proxy_names[k], choose_new_proxy<Actions>{actions, static_cast<int>(k)});
    const auto at = chosen ? std::ranges::find(proxy_names, *chosen) : proxy_names.end();
    this->set_proxy(at == proxy_names.end() ? -1 : static_cast<int>(at - proxy_names.begin()));
    this->invalidateLayout();
  }
  // The proxy chosen for the new account: -1 for none.
  void set_proxy(int index) {
    proxy.reset();
    if (index >= 0 && static_cast<std::size_t>(index) < proxy_names.size())
      proxy = proxy_names[static_cast<std::size_t>(index)];
    auto& choices = parts.proxies_row.parts.choices;
    for (std::size_t i = 0; i < choices.size(); ++i)
      choices[i].set_active(static_cast<int>(i) - 1 == index);
  }
  void begin_swap() {
    swap.jump(0.0f);
    swap.setTarget(1.0f);
    this->fade();
  }
  void fade() {
    const float value = swap.value();
    spl::visit([value](auto& one) { one.fState.setAlpha(value); }, parts.form);
  }
  [[nodiscard]] bool settling() const { return swap.moving(); }
  void update(double now_ms) {
    if (swap.step(now_ms))
      this->fade();
  }

  // The tab of the form that is up, lit, and what that protocol is: its form
  // says.
  void light() {
    auto& each = parts.tabs.parts.each;
    for (std::size_t i = 0; i < each.size(); ++i)
      each[i].set_active(i == parts.form.index());
    parts.note.setText(std::string(spl::visit([](const auto& one) { return one.note; }, parts.form)));
  }
};

}  // namespace mux::ui
