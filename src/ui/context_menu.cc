// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:context_menu -- A message's menu.
export module mux.ui:context_menu;

import std;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes.box;
import skiff.nodes.flow;
import skiff.nodes.scroll;
import skiff.nodes.text;
import skiff.widgets.textbox;
import mux.core;
import mux.config;
import mux.logic.emoji;
import :base;
import :icons;
import :controls;
import :themes;
import :timeline;
import :conversations;
import :forms;

export namespace mux::ui {

// Every emoji, as tdesktop's panel lists them (chat_helpers.style): a search
// at its top; the groups one under another in one list that scrolls, each a
// semibold header over its emoji, 37 across (desiredSize); and a footer of
// the groups' tabs, 36 high, that brings each into view and is lit for the
// one in view. A press on an emoji gives it to Pick: a reaction, from a
// message's menu; text in the input, from the input's own button.
template <class Pick>
struct emoji_panel : nodes::Stack {
  Pick pick;
  static constexpr float kCell = 37.0f;
  // One emoji of the list.
  struct cell : nodes::Stack {
    emoji_panel* panel;
    std::string glyph;
    struct parts_t {
      nodes::Text face;
    } parts;
    cell(emoji_panel* p, std::string g)
        : panel(p), glyph(g), parts{.face = nodes::Text(std::move(g), 22.0f, text_colour)} {
      this->setHorizontal();
      fStack.justify = nodes::justify::middle{};
      fState.apply({.width = kCell, .height = kCell, .cornerRadius = 6.0f, .hoverBackground = chosen_colour});
      parts.face.apply({.alignSelf = scene::align::kMiddle});
    }
    [[nodiscard]] bool acceptsInput() const { return true; }
    [[nodiscard]] bool hoverChangesAppearance() const { return true; }
    [[nodiscard]] bool onClick(float, float) {
      panel->pick(glyph);
      return true;
    }
  };
  // A group: its name over its emoji (headerTop 10, headerLeft 14).
  struct section : nodes::Stack {
    using cells_t = nodes::Flow<std::vector<cell>>;
    struct parts_t {
      nodes::Text title;
      cells_t cells{{.direction = nodes::direction::horizontal{}, .spacingX = 0.0f, .spacingY = 0.0f, .wrap = true}, {}};
    } parts;
    section(emoji_panel* p, std::string_view name, const std::vector<const alef::emoji*>& all)
        : parts{.title = nodes::Text(std::string(name), 13.0f, dim_colour, true)} {
      fState.apply({.fillX = true, .autoSize = scene::axes::kY});
      parts.title.apply({.margin = {10.0f, 0.0f, 6.0f, 7.0f}});
      parts.cells.apply({.fillX = true, .autoSize = scene::axes::kY});
      auto& cells = std::get<0>(parts.cells.fChildren);
      cells.reserve(all.size());
      for (const alef::emoji* one : all)
        cells.emplace_back(p, logic::emoji_text(*one));
    }
  };
  // A group's tab in the footer: its first emoji (iconArea 28).
  struct tab : nodes::Stack {
    emoji_panel* panel;
    std::size_t group;
    struct parts_t {
      nodes::Text face;
    } parts;
    tab(emoji_panel* p, std::size_t g)
        : panel(p), group(g),
          parts{.face = nodes::Text(logic::emoji_text(alef::emoji_groups[g].all.front()), 16.0f, text_colour)} {
      this->setHorizontal();
      fStack.justify = nodes::justify::middle{};
      fState.apply({.width = 28.0f, .height = 28.0f, .alignSelf = scene::align::kMiddle, .cornerRadius = 6.0f,
                    .hoverBackground = chosen_colour, .selectedBackground = tile_colour});
      parts.face.apply({.alignSelf = scene::align::kMiddle});
    }
    [[nodiscard]] bool acceptsInput() const { return true; }
    [[nodiscard]] bool onClick(float, float) {
      panel->bring(group);
      return true;
    }
  };
  struct searched {
    emoji_panel* panel;
    void operator()(std::string_view text) const { panel->search(text); }
  };
  struct footer_row : nodes::Stack {
    struct parts_t {
      std::vector<tab> each;
    } parts;
  };
  using field_t = widgets::TextBox<searched>;
  using list_t = nodes::ScrollContainer<nodes::Flow<std::vector<section>>>;
  struct parts_t {
    field_t field;
    list_t list{nodes::Flow<std::vector<section>>({.spacingY = 0.0f, .wrap = false}, {})};
    footer_row footer;
  } parts;
  // Whether a search is shown, not the groups: no tab is lit then.
  bool searching = false;

  // Sized by where it is shown.
  explicit emoji_panel(Pick what) : pick(std::move(what)), parts{.field = field_t("Search emoji", {this})} {
    auto& [field, list, footer] = parts;
    this->setGap(4.0f);
    fState.apply({.padding = {7.0f, 0.0f, 4.0f, 7.0f}});
    field.setSearchIcon(true);
    field.apply({.fillX = true, .height = 32.0f, .margin = {0.0f, 7.0f, 0.0f, 0.0f}});
    list.apply({.fillX = true, .grow = scene::axes::kY});
    std::get<0>(list.fChildren).apply({.fillX = true, .autoSize = scene::axes::kY});
    footer.setHorizontal();
    footer.setGap(4.0f);
    footer.apply({.fillX = true, .height = 36.0f});
    for (std::size_t g = 0; g < alef::emoji_groups.size(); ++g)
      footer.parts.each.emplace_back(this, g);
    this->show_all();
  }
  [[nodiscard]] std::vector<section>& sections() { return std::get<0>(std::get<0>(parts.list.fChildren).fChildren); }
  // Every group, one under another.
  void show_all() {
    auto& all = this->sections();
    all.clear();
    all.reserve(alef::emoji_groups.size());
    for (std::size_t g = 0; g < alef::emoji_groups.size(); ++g)
      all.emplace_back(this, alef::emoji_groups[g].name, logic::emoji_of_group(g));
    searching = false;
    parts.list.invalidateLayout();
    parts.list.scrollTo(0.0f);
  }
  void search(std::string_view query) {
    if (query.empty()) {
      this->show_all();
      return;
    }
    auto& all = this->sections();
    all.clear();
    all.emplace_back(this, "Search results", logic::emoji_found(query));
    searching = true;
    parts.list.invalidateLayout();
    parts.list.scrollTo(0.0f);
  }
  // A group brought to the top of the list, as its tab does.
  void bring(std::size_t group) {
    if (searching)
      parts.field.setText({});
    auto& all = this->sections();
    if (group >= all.size() || all[group].bounds().isEmpty())
      return;
    auto& list = parts.list;
    list.scrollTo(std::max(0.0f, list.current() + (all[group].bounds().fTop - list.bounds().fTop)));
  }
  // The tab of the group at the top of the list lit.
  void update(double) {
    auto& all = this->sections();
    std::size_t lit = 0;
    const float top = parts.list.bounds().fTop + 1.0f;
    for (std::size_t g = 0; g < all.size(); ++g)
      if (!all[g].bounds().isEmpty() && all[g].bounds().fTop <= top)
        lit = g;
    for (tab& each : parts.footer.parts.each)
      if (const bool on = !searching && each.group == lit; on != each.fState.selected())
        each.fState.apply({.selected = on});
  }
};

// What the menu's emoji do: react with it.
template <class Actions>
struct react_with {
  Actions* actions = nullptr;
  void operator()(const std::string& glyph) const { actions->menu_react(glyph); }
};
// What the input's emoji do: go into what is written.
template <class Actions>
struct insert_emoji_into {
  Actions* actions = nullptr;
  void operator()(const std::string& glyph) const { actions->insert_emoji(glyph); }
};

// The input's emoji, as tdesktop's panel: a card over the chat, 345 wide
// (emojiPanWidth), 278 to 640 high, rounded 8, its bottom right at the top
// right of the button that opened it; a press off it closes it. It stays
// open while emoji are picked, and the input keeps the keys.
template <class Actions>
struct emoji_popup : scene::Node {
  struct card_t : nodes::Stack {
    using panel_t = emoji_panel<insert_emoji_into<Actions>>;
    struct parts_t {
      panel_t panel;
    } parts;
    explicit card_t(Actions* a) : parts{.panel = panel_t(insert_emoji_into<Actions>{a})} {
      fState.apply({.width = 345.0f, .height = 360.0f, .cornerRadius = 8.0f, .background = sidebar_colour,
                    .border = scene::Border{band_colour, 1.0f},
                    .shadow = scene::Shadow{skia::colorSetARGB(70, 0, 0, 0), 3.0f}});
      parts.panel.apply({.fill = true});
    }
    [[nodiscard]] bool acceptsInput() const { return true; }
  };
  struct parts_t {
    card_t card;
  } parts;
  Actions* actions = nullptr;
  // Where the button that opened it is: its right, its top.
  float right = 0.0f, bottom = 0.0f;
  float placed_x = -1.0f, placed_y = -1.0f, placed_h = -1.0f;

  emoji_popup(Actions* a, float at_right, float at_bottom)
      : parts{.card = card_t(a)}, actions(a), right(at_right), bottom(at_bottom) {
    fState.apply({.fill = true});
  }
  void layoutChildren() {
    const skia::SkRect box = fState.contentBox();
    constexpr float kWidth = 345.0f, kEdge = 10.0f;
    const float room = std::max(0.0f, bottom - box.fTop - kEdge);
    const float h = std::min(std::clamp(box.height() * 0.6f, 278.0f, 640.0f), room);
    const float x = std::clamp(right - kWidth, kEdge, std::max(kEdge, box.width() - kWidth - kEdge));
    const float y = std::max(box.fTop + kEdge, bottom - h) - box.fTop;
    if (x != placed_x || y != placed_y || h != placed_h) {
      placed_x = x;
      placed_y = y;
      placed_h = h;
      parts.card.apply({.x = x, .y = y, .height = h});
    }
    scene::layoutChildrenInContentBox(*this);
  }
  // A press off it closes it.
  [[nodiscard]] bool acceptsInput() const { return true; }
  using Node::onPointer;
  void onPointer(scene::phase::target, const scene::pointer::down&, scene::PointerReply& reply) {
    actions->close_emoji();
    reply.handle();
  }
};

// What is done with a message from its menu, as the program keeps it.
template <class Actions>
struct context_menu : scene::Node {
  struct card : nodes::Stack {
    // Quick reactions, as tdesktop's menu has them at its top.
    struct quick_reaction : nodes::Stack {
      Actions* actions;
      std::string key;
      struct parts_t {
        nodes::Text face;
      } parts;
      quick_reaction(Actions* a, std::string k)
          : actions(a), key(k), parts{.face = nodes::Text(std::move(k), 18.0f, text_colour)} {
        auto& face = parts.face;
        this->setHorizontal();
        fStack.justify = nodes::justify::middle{};
        fState.apply({.width = 34.0f, .height = 34.0f, .cornerRadius = 17.0f, .hoverBackground = chosen_colour});
        face.apply({.alignSelf = scene::align::kMiddle});
      }
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
      struct parts_t {
        std::vector<quick_reaction> each;
        icon_button<expand_emoji> more;
      } parts;
      quick_row(Actions* a, card* of) : parts{.more = icon_button<expand_emoji>(icon::down{}, {of})} {
        auto& [each, more] = parts;
        this->setHorizontal();
        this->setGap(2.0f);
        fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {2.0f, 8.0f, 4.0f, 8.0f}});
        for (const char* key : {"👍", "❤️", "😂", "😮", "😢", "🙏"})
          each.emplace_back(a, key);
        more.apply({.width = 30.0f, .height = 34.0f});
      }
    };
    Actions* actions_of = nullptr;
    using reply_row = row_item<ask<Actions, &Actions::menu_reply>>;
    using edit_row = row_item<ask<Actions, &Actions::menu_edit>>;
    using later_row = row_item<not_yet<Actions>>;
    using copy_row = row_item<ask<Actions, &Actions::menu_copy>>;
    using link_row = row_item<ask<Actions, &Actions::menu_copy_link>>;
    using save_row = row_item<ask<Actions, &Actions::menu_save>>;
    using delete_row = row_item<ask<Actions, &Actions::menu_delete>>;
    // As tdesktop's, in its order: the quick reactions; every emoji, in
    // place of the rest once asked for; Reply, Edit, Pin, Copy, Copy
    // Message Link, Save As, Forward, Delete; and who has seen it -- how
    // many, and their names under it -- at the foot.
    struct parts_t {
      quick_row quick;
      nodes::Box<> quick_band{band_colour};
      std::optional<emoji_panel<react_with<Actions>>> emoji;
      reply_row reply;
      edit_row edit;
      later_row pin;
      copy_row copy;
      link_row copy_link;
      save_row save;
      later_row forward;
      delete_row remove;
      nodes::Box<> seen_band{band_colour};
      row_item<nothing> seen;
      std::vector<nodes::Text> seen_names;
    } parts;
    void expand() {
      auto& [quick, quick_band, emoji, reply, edit, pin, copy, copy_link, save, forward, remove, seen_band, seen,
             seen_names] = parts;
      if (emoji)
        return;
      emoji.emplace(react_with<Actions>{actions_of});
      emoji->apply({.fillX = true, .height = 300.0f});
      quick.parts.more.setVisible(false);
      for (scene::Node* item : std::initializer_list<scene::Node*>{&reply, &edit, &pin, &copy, &copy_link, &save,
                                                                   &forward, &remove, &seen_band, &seen})
        item->setVisible(false);
      for (auto& name : seen_names)
        name.setVisible(false);
      this->invalidateLayout();
    }
    // The keys, as tdesktop's menu takes them: Up and Down through its
    // items, round; Enter does what is lit (the item's own); Esc closes it.
    [[nodiscard]] bool focusable() const { return true; }
    using Node::onKey;
    void onKey(scene::phase::bubble, const scene::key::down& press, scene::Reply& reply) {
      namespace keys = scene::keys;
      if (press.key == keys::kUp || press.key == keys::kDown) {
        reply.moveFocus(press.key == keys::kUp);
      } else if (press.key == keys::kEscape) {
        actions_of->close_menu();
        reply.handle();
      }
    }
    // What does not apply to the message left out.
    card(Actions* a, const menu_facts& facts)
        : actions_of(a),
          parts{.quick = quick_row(a, this),
                .reply = reply_row("Reply", {a}, icon::back{}),
                .edit = edit_row("Edit", {a}, icon::sliders{}),
                .pin = later_row("Pin", {a, "Pinning messages"}, icon::check{}),
                .copy = copy_row(facts.selection ? "Copy Selected Text" : "Copy Text", {a}, icon::clip{}),
                .copy_link = link_row("Copy Message Link", {a}, icon::info{}),
                .save = save_row("Save As…", {a}, icon::send{}),
                .forward = later_row("Forward", {a, "Forwarding"}, icon::send{}),
                .remove = delete_row("Delete", {a}, icon::close{}),
                .seen = row_item<nothing>(facts.seen.empty() ? std::string("Not seen yet")
                                                             : std::format("Seen by {}", facts.seen.size()),
                                          {}, icon::check{})} {
      auto& [quick, quick_band, emoji, reply, edit, pin, copy, copy_link, save, forward, remove, seen_band, seen,
             seen_names] = parts;
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
  };
  struct parts_t {
    card menu;
  } parts;
  Actions* actions = nullptr;

  // Where it was asked for: the pointer.
  float at_x = 0.0f, at_y = 0.0f;
  explicit context_menu(Actions* a, const menu_facts& facts)
      : parts{.menu = card(a, facts)}, actions(a), at_x(facts.x), at_y(facts.y) {
    fState.apply({.fill = true});
    parts.menu.apply({.x = at_x, .y = at_y});
  }
  // As tdesktop's popup menu: at the pointer, going down and right -- up
  // where it would pass the window's bottom, left where it would pass its
  // right -- and kept inside the window.
  void layoutChildren() {
    scene::layoutChildrenInContentBox(*this);
    const skia::SkRect box = fState.contentBox();
    auto& menu = parts.menu;
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
  // A press off the menu closes it.
  [[nodiscard]] bool acceptsInput() const { return true; }
  using Node::onPointer;
  void onPointer(scene::phase::target, const scene::pointer::down&, scene::PointerReply& reply) {
    actions->close_menu();
    reply.handle();
  }
};

}  // namespace mux::ui
