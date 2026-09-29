// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:add_account -- Adding an account.
export module mux.ui:add_account;

import std;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes.flow;
import skiff.nodes.text;
import mux.core;
import mux.config;
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
  // XMPP | Matrix: two segments in a thin frame.
  struct protocol_switch : nodes::Stack {
    using xmpp_segment = segment<ask<Actions, &Actions::add_xmpp>>;
    using matrix_segment = segment<ask<Actions, &Actions::add_matrix>>;
    struct parts_t {
      xmpp_segment xmpp_tab;
      matrix_segment matrix_tab;
    } parts;
    explicit protocol_switch(Actions* a)
        : parts{.xmpp_tab = xmpp_segment("XMPP", {a}), .matrix_tab = matrix_segment("Matrix", {a})} {
      this->setHorizontal();
      this->setGap(1.0f);
      fState.apply({.autoSize = scene::axes::kBoth, .padding = {1.0f, 1.0f, 1.0f, 1.0f}, .background = chosen_colour});
    }
  };
  // The proxy the new account goes through: none, or one of the profiles.
  struct proxy_row : nodes::Stack {
    struct parts_t {
      nodes::Text title{"Proxy", 13.0f, dim_colour};
      std::vector<segment<choose_new_proxy<Actions>>> choices;
    } parts;
    proxy_row() {
      this->setHorizontal();
      this->setGap(4.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY});
      parts.title.apply({.alignSelf = scene::align::kMiddle});
    }
  };
  struct parts_t {
    protocol_switch tabs;
    nodes::Text note{"", 13.0f, dim_colour};
    proxy_row proxies_row;
    account_form<Actions> form;
  } parts;
  std::vector<std::string> proxy_names;
  std::optional<std::string> proxy;
  // The form coming in when the protocol changes, fading in.
  skiff::paint::Tween swap{1.0f, 200.0f, skiff::paint::movement::subtle{}};

  add_account_pane(Actions* a, const std::vector<config::proxy_settings>& proxies)
      : actions(a),
        parts{.tabs = protocol_switch(a), .form = account_form<Actions>(std::in_place_index<0>, a, std::nullopt)} {
    fState.apply({.fill = true});
    this->setGap(12.0f);
    parts.note.setWrapped(true);
    parts.note.apply({.fillX = true});
    auto& choices = parts.proxies_row.parts.choices;
    choices.emplace_back("None", choose_new_proxy<Actions>{a, -1});
    for (std::size_t k = 0; k < proxies.size(); ++k) {
      proxy_names.push_back(proxies[k].name);
      choices.emplace_back(proxies[k].name, choose_new_proxy<Actions>{a, static_cast<int>(k)});
    }
    parts.proxies_row.setVisible(!proxies.empty());
    this->set_proxy(-1);
    this->light();
  }

  void show_xmpp() {
    parts.form.template emplace<0>(this->actions, std::nullopt);
    this->begin_swap();
    this->light();
  }
  void show_matrix() {
    parts.form.template emplace<1>(this->actions, std::nullopt);
    this->begin_swap();
    this->light();
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
    std::visit([value](auto& one) { one.fState.setAlpha(value); }, parts.form);
  }
  [[nodiscard]] bool settling() const { return swap.moving(); }
  void update(double now_ms) {
    if (swap.step(now_ms))
      this->fade();
  }

  [[nodiscard]] xmpp_form<Actions>* xmpp() { return xmpp_form_in(parts.form); }

  // The tab of the form that is up, lit, and what that protocol is.
  void light() {
    std::visit(overloaded{[this](const xmpp_form<Actions>&) {
                            parts.tabs.parts.xmpp_tab.set_active(true);
                            parts.tabs.parts.matrix_tab.set_active(false);
                            parts.note.setText("An address like user@example.com, on a server such as Prosody or ejabberd.");
                          },
                          [this](const matrix_form<Actions>&) {
                            parts.tabs.parts.xmpp_tab.set_active(false);
                            parts.tabs.parts.matrix_tab.set_active(true);
                            parts.note.setText("A user ID like @user:example.org, on a homeserver such as Synapse.");
                          }},
               parts.form);
  }
};

}  // namespace mux::ui
