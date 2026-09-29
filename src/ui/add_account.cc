// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:add_account -- Adding an account.
export module mux.ui:add_account;

import std;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes;
import skiff.widgets;
import mux.core;
import mux.config;
export import :forms;

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
    segment<ask<Actions, &Actions::add_xmpp>> xmpp_tab;
    segment<ask<Actions, &Actions::add_matrix>> matrix_tab;
    explicit protocol_switch(Actions* a) : xmpp_tab("XMPP", {a}), matrix_tab("Matrix", {a}) {
      this->setHorizontal();
      this->setGap(1.0f);
      fState.apply({.autoSize = scene::axes::kBoth, .padding = {1.0f, 1.0f, 1.0f, 1.0f}});
    }
    void forEachChild(auto&& f) {
      f(xmpp_tab);
      f(matrix_tab);
    }
    void drawSelf(skia::SkCanvas* canvas, float alpha) {
      if (skia::SkFont* font = skiff::paint::defaultFont())
        skiff::paint::Painter(canvas, *font).fillRounded(fState.fBounds, 0.0f, chosen_colour, alpha);
    }
  } tabs;
  nodes::Text note{"", 13.0f, dim_colour};
  // The proxy the new account goes through: none, or one of the profiles.
  struct proxy_row : nodes::Stack {
    nodes::Text title{"Proxy", 13.0f, dim_colour};
    std::vector<segment<choose_new_proxy<Actions>>> choices;
    proxy_row() {
      this->setHorizontal();
      this->setGap(4.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY});
      title.apply({.alignSelf = scene::align::kMiddle});
    }
    void forEachChild(auto&& f) {
      f(title);
      f(choices);
    }
  } proxies_row;
  account_form<Actions> form;
  std::vector<std::string> proxy_names;
  std::optional<std::string> proxy;
  // The form coming in when the protocol changes, fading in.
  skiff::paint::Tween swap{1.0f, 200.0f, skiff::paint::movement::subtle{}};

  add_account_pane(Actions* a, const std::vector<config::proxy_settings>& proxies)
      : actions(a), tabs(a), form(std::in_place_index<0>, a, std::nullopt) {
    fState.apply({.fill = true});
    this->setGap(12.0f);
    note.setWrapped(true);
    note.apply({.fillX = true});
    proxies_row.choices.emplace_back("None", choose_new_proxy<Actions>{a, -1});
    for (std::size_t k = 0; k < proxies.size(); ++k) {
      proxy_names.push_back(proxies[k].name);
      proxies_row.choices.emplace_back(proxies[k].name, choose_new_proxy<Actions>{a, static_cast<int>(k)});
    }
    proxies_row.setVisible(!proxies.empty());
    this->set_proxy(-1);
    this->light();
  }

  void forEachChild(auto&& f) {
    f(tabs);
    f(note);
    f(proxies_row);
    f(form);
  }

  void show_xmpp() {
    form.template emplace<0>(this->actions, std::nullopt);
    this->begin_swap();
    this->light();
  }
  void show_matrix() {
    form.template emplace<1>(this->actions, std::nullopt);
    this->begin_swap();
    this->light();
  }
  // The proxy chosen for the new account: -1 for none.
  void set_proxy(int index) {
    proxy.reset();
    if (index >= 0 && static_cast<std::size_t>(index) < proxy_names.size())
      proxy = proxy_names[static_cast<std::size_t>(index)];
    for (std::size_t i = 0; i < proxies_row.choices.size(); ++i)
      proxies_row.choices[i].set_active(static_cast<int>(i) - 1 == index);
  }
  void begin_swap() {
    swap.jump(0.0f);
    swap.setTarget(1.0f);
    this->fade();
  }
  void fade() {
    const float value = swap.value();
    std::visit([value](auto& one) { one.fState.setAlpha(value); }, form);
  }
  [[nodiscard]] bool settling() const { return swap.moving(); }
  void update(double now_ms) {
    if (swap.step(now_ms))
      this->fade();
  }

  [[nodiscard]] xmpp_form<Actions>* xmpp() { return xmpp_form_in(form); }

  // The tab of the form that is up, lit, and what that protocol is.
  void light() {
    std::visit(overloaded{[this](const xmpp_form<Actions>&) {
                            tabs.xmpp_tab.set_active(true);
                            tabs.matrix_tab.set_active(false);
                            note.setText("An address like user@example.com, on a server such as Prosody or ejabberd.");
                          },
                          [this](const matrix_form<Actions>&) {
                            tabs.xmpp_tab.set_active(false);
                            tabs.matrix_tab.set_active(true);
                            note.setText("A user ID like @user:example.org, on a homeserver such as Synapse.");
                          }},
               form);
  }
};

}  // namespace mux::ui
