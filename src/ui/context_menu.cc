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
import mux.logic.emoji;
export import :settings;

export namespace mux::ui {

// Every emoji, to react with: as tdesktop's panel -- a search at its top,
// the groups as tabs by their first emoji, and a grid of the one chosen or
// of what the search finds; a press reacts with it.
template <class Actions>
struct emoji_panel : nodes::Stack {
  Actions* actions;
  // One emoji of the grid.
  struct cell : nodes::Stack {
    Actions* actions;
    std::string glyph;
    nodes::Text face;
    cell(Actions* a, std::string g) : actions(a), glyph(g), face(std::move(g), 20.0f, text_colour) {
      this->setHorizontal();
      fStack.justify = nodes::justify::middle{};
      fState.apply({.width = 30.0f, .height = 30.0f, .cornerRadius = 6.0f, .hoverBackground = chosen_colour});
      face.apply({.alignSelf = scene::align::kMiddle});
    }
    void forEachChild(auto&& f) { f(face); }
    [[nodiscard]] bool acceptsInput() const { return true; }
    [[nodiscard]] bool onClick(float, float) {
      actions->menu_react(glyph);
      return true;
    }
  };
  // A group's tab: its first emoji.
  struct tab : nodes::Stack {
    emoji_panel* panel;
    std::size_t group;
    nodes::Text face;
    tab(emoji_panel* p, std::size_t g)
        : panel(p), group(g), face(logic::emoji_text(logic::emoji_groups[g].entries.front()), 16.0f, text_colour) {
      this->setHorizontal();
      fStack.justify = nodes::justify::middle{};
      fState.apply({.width = 25.0f, .height = 26.0f, .cornerRadius = 6.0f, .hoverBackground = chosen_colour,
                    .selectedBackground = tile_colour});
      face.apply({.alignSelf = scene::align::kMiddle});
    }
    void forEachChild(auto&& f) { f(face); }
    [[nodiscard]] bool acceptsInput() const { return true; }
    [[nodiscard]] bool onClick(float, float) {
      panel->show_group(group);
      return true;
    }
  };
  struct searched {
    emoji_panel* panel;
    void operator()(std::string_view text) const { panel->search(text); }
  };
  widgets::TextBox<searched> field;
  struct tabs_row : nodes::Stack {
    std::vector<tab> each;
    void forEachChild(auto&& f) { f(each); }
  } tabs;
  nodes::ScrollContainer<nodes::Flow<std::vector<cell>>> grid{nodes::Flow<std::vector<cell>>(
      {.direction = nodes::direction::horizontal{}, .spacingX = 0.0f, .spacingY = 0.0f, .wrap = true}, {})};

  explicit emoji_panel(Actions* a) : actions(a), field("Search emoji", {this}) {
    this->setGap(4.0f);
    fState.apply({.fillX = true, .height = 300.0f, .padding = {2.0f, 8.0f, 2.0f, 8.0f}});
    field.setSearchIcon(true);
    field.apply({.fillX = true, .height = 32.0f});
    tabs.setHorizontal();
    tabs.apply({.fillX = true, .autoSize = scene::axes::kY});
    for (std::size_t g = 0; g < std::size(logic::emoji_groups); ++g)
      tabs.each.emplace_back(this, g);
    grid.apply({.fillX = true, .grow = scene::axes::kY});
    this->show_group(0);
  }
  void forEachChild(auto&& f) {
    f(field);
    f(tabs);
    f(grid);
  }
  void show(const std::vector<const logic::emoji_entry*>& found) {
    auto& cells = std::get<0>(std::get<0>(grid.fChildren).fChildren);
    cells.clear();
    for (const logic::emoji_entry* one : found)
      cells.emplace_back(actions, logic::emoji_text(*one));
    grid.invalidateLayout();
    grid.scrollTo(0.0f);
  }
  void show_group(std::size_t group) {
    for (tab& each : tabs.each)
      each.fState.apply({.selected = each.group == group});
    this->show(logic::emoji_of_group(group));
  }
  void search(std::string_view query) {
    if (query.empty())
      this->show_group(0);
    else
      this->show(logic::emoji_found(query));
  }
};

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
    // The six, and at their end the way to every emoji, as tdesktop's.
    struct expand_emoji {
      card* of;
      void operator()() const { of->expand(); }
    };
    struct quick_row : nodes::Stack {
      std::vector<quick_reaction> each;
      icon_button<expand_emoji> more;
      quick_row(Actions* a, card* of) : more(icon::down{}, {of}) {
        this->setHorizontal();
        this->setGap(2.0f);
        fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {2.0f, 8.0f, 4.0f, 8.0f}});
        for (const char* key : {"👍", "❤️", "😂", "😮", "😢", "🙏"})
          each.emplace_back(a, key);
        more.apply({.width = 30.0f, .height = 34.0f});
      }
      void forEachChild(auto&& f) {
        f(each);
        f(more);
      }
    } quick;
    Actions* actions_of = nullptr;
    // Every emoji, in place of the rest of the menu once asked for.
    std::optional<emoji_panel<Actions>> emoji;
    void expand() {
      if (emoji)
        return;
      emoji.emplace(actions_of);
      quick.more.setVisible(false);
      for (scene::Node* item : std::initializer_list<scene::Node*>{&reply, &edit, &pin, &copy, &copy_link, &save,
                                                                   &forward, &remove, &seen_band, &seen})
        item->setVisible(false);
      for (auto& name : seen_names)
        name.setVisible(false);
      this->invalidateLayout();
    }
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
        : quick(a, this), actions_of(a), reply("Reply", {a}, icon::back{}), edit("Edit", {a}, icon::sliders{}),
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
      f(emoji);
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

  // Where it was asked for: the pointer.
  float at_x = 0.0f, at_y = 0.0f;
  explicit context_menu(Actions* a, const menu_facts& facts) : menu(a, facts), actions(a), at_x(facts.x), at_y(facts.y) {
    fState.apply({.fill = true});
    menu.apply({.x = at_x, .y = at_y});
  }
  // As tdesktop's popup menu: at the pointer, going down and right -- up
  // where it would pass the window's bottom, left where it would pass its
  // right -- and kept inside the window.
  void layoutChildren() {
    scene::layoutChildrenInContentBox(*this);
    const skia::SkRect box = fState.contentBox();
    const float w = menu.bounds().width(), h = menu.bounds().height();
    constexpr float kEdge = 8.0f;
    float x = at_x, y = at_y;
    if (box.fTop + y + h > box.fBottom - kEdge)
      y = at_y - h;
    if (box.fLeft + x + w > box.fRight - kEdge)
      x = at_x - w;
    x = std::clamp(x, kEdge, std::max(kEdge, box.width() - w - kEdge));
    y = std::clamp(y, kEdge, std::max(kEdge, box.height() - h - kEdge));
    if (x != placed_x || y != placed_y) {
      placed_x = x;
      placed_y = y;
      menu.apply({.x = x, .y = y});
      scene::layoutChildrenInContentBox(*this);
    }
  }
  float placed_x = -1.0f, placed_y = -1.0f;
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
