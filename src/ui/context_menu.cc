// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:context_menu -- A message's menu.
export module mux.ui:context_menu;

import std;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes;
import skiff.widgets;
import mux.core;
import mux.config;
export import :settings;

export namespace mux::ui {

// What is done with a message from its menu, as the program keeps it.
template <class Actions>
struct context_menu : scene::Node {
  struct card : nodes::Stack {
    row_item<ask<Actions, &Actions::menu_reply>> reply;
    row_item<ask<Actions, &Actions::menu_edit>> edit;
    row_item<not_yet<Actions>> pin;
    row_item<ask<Actions, &Actions::menu_copy>> copy;
    row_item<ask<Actions, &Actions::menu_copy_link>> copy_link;
    row_item<ask<Actions, &Actions::menu_save>> save;
    row_item<not_yet<Actions>> forward;
    row_item<ask<Actions, &Actions::menu_delete>> remove;
    // Quick reactions, as tdesktop's menu has them at its top.
    struct quick_reaction : nodes::Stack {
      Actions* actions;
      std::string key;
      nodes::Text face;
      quick_reaction(Actions* a, std::string k) : actions(a), key(k), face(std::move(k), 18.0f, text_colour) {
        this->setHorizontal();
        fStack.justify = nodes::justify::middle{};
        fState.apply({.width = 34.0f, .height = 34.0f, .cornerRadius = 17.0f, .hoverBackground = chosen_colour});
        face.apply({.alignSelf = scene::align::kMiddle});
      }
      void forEachChild(auto&& f) { f(face); }
      [[nodiscard]] bool acceptsInput() const { return true; }
      [[nodiscard]] bool hoverChangesAppearance() const { return true; }
      [[nodiscard]] bool onClick(float, float) {
        actions->menu_react(key);
        return true;
      }
    };
    struct quick_row : nodes::Stack {
      std::vector<quick_reaction> each;
      explicit quick_row(Actions* a) {
        this->setHorizontal();
        this->setGap(2.0f);
        fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {2.0f, 8.0f, 4.0f, 8.0f}});
        for (const char* key : {"👍", "❤️", "😂", "😮", "😢", "🙏"})
          each.emplace_back(a, key);
      }
      void forEachChild(auto&& f) { f(each); }
    } quick;
    nodes::Box<> quick_band{band_colour};
    // Who has seen it, as Telegram's menu says at its top: how many, and
    // their names under it.
    row_item<nothing> seen;
    std::vector<nodes::Text> seen_names;
    nodes::Box<> seen_band{band_colour};
    // As tdesktop's, in its order, what does not apply to the message left
    // out: Reply, Edit, Pin, Copy, Copy Message Link, Save As, Forward,
    // Delete; and who has seen it, at the foot.
    card(Actions* a, const menu_facts& facts)
        : quick(a), reply("Reply", {a}, icon::back{}), edit("Edit", {a}, icon::sliders{}),
          pin("Pin", {a, "Pinning messages"}, icon::check{}),
          copy(facts.selection ? "Copy Selected Text" : "Copy Text", {a}, icon::clip{}),
          copy_link("Copy Message Link", {a}, icon::info{}), save("Save As…", {a}, icon::send{}),
          forward("Forward", {a, "Forwarding"}, icon::send{}), remove("Delete", {a}, icon::close{}),
          seen(facts.seen.empty() ? std::string("Not seen yet") : std::format("Seen by {}", facts.seen.size()), {},
               icon::check{}) {
      const std::vector<std::string>& readers = facts.seen;
      quick_band.apply({.fillX = true, .height = 1.0f, .margin = {0.0f, 0.0f, 4.0f, 0.0f}});
      edit.setVisible(facts.own && !facts.text.empty() && !facts.media);
      copy.setVisible(!facts.copied.empty());
      copy_link.setVisible(!facts.link.empty());
      save.setVisible(facts.media.has_value());
      remove.setVisible(facts.own);
      for (std::size_t i = 0; i < readers.size() && i < 10; ++i) {
        seen_names.emplace_back(readers[i], 13.0f, dim_colour);
        seen_names.back().setElided(true);
        seen_names.back().apply({.fillX = true, .margin = {0.0f, 16.0f, 2.0f, 64.0f}});
      }
      if (readers.size() > 10) {
        seen_names.emplace_back(std::format("and {} more", readers.size() - 10), 13.0f, dim_colour);
        seen_names.back().apply({.fillX = true, .margin = {0.0f, 16.0f, 2.0f, 64.0f}});
      }
      seen_band.apply({.fillX = true, .height = 1.0f, .margin = {4.0f, 0.0f, 4.0f, 0.0f}});
      fState.apply({.width = 230.0f, .autoSize = scene::axes::kY, .padding = {6.0f, 0.0f, 6.0f, 0.0f}, .cornerRadius = 10.0f, .background = sidebar_colour, .border = scene::Border{band_colour, 1.0f},
                    .shadow = scene::Shadow{skia::colorSetARGB(70, 0, 0, 0), 3.0f}});
    }
    void forEachChild(auto&& f) {
      f(quick);
      f(quick_band);
      f(reply);
      f(edit);
      f(pin);
      f(copy);
      f(copy_link);
      f(save);
      f(forward);
      f(remove);
      f(seen_band);
      f(seen);
      f(seen_names);
    }
  } menu;
  Actions* actions = nullptr;

  explicit context_menu(Actions* a, const menu_facts& facts) : menu(a, facts), actions(a) {
    const float x = facts.x, y = facts.y;
    fState.apply({.fill = true});
    menu.apply({.x = x, .y = y});
  }
  void forEachChild(auto&& f) { f(menu); }
  // A press off the menu closes it.
  [[nodiscard]] bool acceptsInput() const { return true; }
  using Node::onPointer;
  void onPointer(scene::phase::target, const scene::pointer::down&, scene::PointerReply& reply) {
    actions->close_menu();
    reply.handle();
  }
};

}  // namespace mux::ui
