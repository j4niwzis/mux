// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:context_menu -- A message's menu.
export module mux.ui:context_menu;

import std;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes.box;
import skiff.nodes.flow;
import skiff.nodes.image;
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
import :avatars;
import :timeline;
import :conversations;
import :forms;

export namespace mux::ui {

// The emoji picked lately, newest first, as tdesktop keeps them (at most
// 42); and what is told when one is picked, so that the program keeps the
// list in its file.
inline std::vector<std::string>& recent_emoji() {
  static std::vector<std::string> kept;
  return kept;
}
// Whether the list changed since the program last kept it: the program
// reads it, and keeps the list.
inline bool& recent_emoji_changed() {
  static bool changed = false;
  return changed;
}
// The custom emoji of the chat the panel is opened over, as the program says.
inline std::vector<emote>& chat_emotes() {
  static std::vector<emote> kept;
  return kept;
}
// And its stickers.
inline std::vector<emote>& chat_stickers() {
  static std::vector<emote> kept;
  return kept;
}

// The input's popup's pages.
namespace popup_page {
struct emoji {};
struct stickers {};
struct gifs {};
}  // namespace popup_page
using popup_page_t = std::variant<popup_page::emoji, popup_page::stickers, popup_page::gifs>;

// The chat's stickers, as tdesktop's tab: a grid of them; a press sends one.
template <class Actions>
struct sticker_grid : nodes::Stack {
  struct cell : nodes::Stack {
    Actions* actions;
    emote sticker;
    struct parts_t {
      nodes::Image<from_avatars> picture;
    } parts;
    cell(Actions* a, emote one)
        : actions(a), sticker(one), parts{.picture = nodes::Image<from_avatars>({one.url})} {
      fState.apply({.width = 80.0f, .height = 80.0f, .margin = {2.0f, 2.0f, 2.0f, 2.0f}, .cornerRadius = 6.0f,
                    .hoverBackground = chosen_colour});
      parts.picture.apply({.fill = true});
    }
    [[nodiscard]] bool acceptsInput() const { return true; }
    [[nodiscard]] bool hoverChangesAppearance() const { return true; }
    [[nodiscard]] bool onClick(float, float) {
      actions->send_sticker(sticker);
      return true;
    }
  };
  using cells_t = nodes::Flow<std::vector<cell>>;
  struct parts_t {
    nodes::Text empty;
    nodes::ScrollContainer<cells_t> list{
        cells_t({.direction = nodes::direction::horizontal{}, .spacingX = 0.0f, .spacingY = 0.0f, .wrap = true}, {})};
  } parts;
  explicit sticker_grid(Actions* a)
      : parts{.empty = nodes::Text("No stickers here. A room's sticker packs, and yours, show here.", 13.0f, dim_colour)} {
    fState.apply({.padding = {4.0f, 4.0f, 4.0f, 4.0f}});
    parts.empty.apply({.margin = {12.0f, 12.0f, 0.0f, 12.0f}});
    parts.list.apply({.fillX = true, .grow = scene::axes::kY});
    std::get<0>(parts.list.fChildren).apply({.fillX = true, .autoSize = scene::axes::kY});
    auto& cells = std::get<0>(std::get<0>(parts.list.fChildren).fChildren);
    for (const emote& one : chat_stickers())
      cells.emplace_back(a, one);
    parts.empty.setVisible(chat_stickers().empty());
  }
};
inline void remember_emoji(const std::string& glyph) {
  constexpr std::size_t kKept = 42;
  auto& all = recent_emoji();
  std::erase(all, glyph);
  all.insert(all.begin(), glyph);
  if (all.size() > kKept)
    all.resize(kKept);
  recent_emoji_changed() = true;
}

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
    // The emoji it shows, where it is one of the table's: its tones are
    // found through it.
    const alef::emoji* source = nullptr;
    // A custom emoji's picture on the server: what a reaction with it is,
    // while what goes into the text is its :shortcode:.
    std::string picture_url;
    struct parts_t {
      std::optional<nodes::Image<from_avatars>> picture;
      nodes::Text face;
    } parts;
    cell(emoji_panel* p, std::string g, const alef::emoji* from = nullptr)
        : panel(p), glyph(g), source(from), parts{.face = nodes::Text(std::move(g), 22.0f, text_colour)} {
      this->setHorizontal();
      fStack.justify = nodes::justify::middle{};
      fState.apply({.width = kCell, .height = kCell, .cornerRadius = 6.0f, .hoverBackground = chosen_colour});
      parts.face.apply({.alignSelf = scene::align::kMiddle});
    }
    [[nodiscard]] bool acceptsInput() const { return true; }
    [[nodiscard]] bool hoverChangesAppearance() const { return true; }
    // A custom emoji: its picture in place of a glyph.
    cell(emoji_panel* p, const emote& custom) : cell(p, ":" + custom.shortcode + ":") {
      picture_url = custom.url;
      parts.face.setVisible(false);
      parts.picture.emplace(from_avatars{custom.url});
      parts.picture->apply({.width = 26.0f, .height = 26.0f, .alignSelf = scene::align::kMiddle});
    }
    [[nodiscard]] bool onClick(float, float) {
      const std::string chosen = glyph;
      const std::string key = picture_url.empty() ? glyph : picture_url;
      if (picture_url.empty())
        remember_emoji(chosen);
      panel->tones_done = true;
      panel->pick(chosen, key);
      return true;
    }
    // The other button: its skin tones, over it, where it has any.
    using Node::onPointer;
    void onPointer(scene::phase::bubble, const scene::pointer::down& press, scene::PointerReply& reply) {
      if (press.button != 3 || !source)
        return;
      if (panel->show_tones(*source, this->bounds()))
        reply.handle();
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
        cells.emplace_back(p, logic::emoji_text(*one), one);
    }
    // The chat's custom emoji, as pictures.
    section(emoji_panel* p, std::string_view name, const std::vector<emote>& custom)
        : parts{.title = nodes::Text(std::string(name), 13.0f, dim_colour, true)} {
      fState.apply({.fillX = true, .autoSize = scene::axes::kY});
      parts.title.apply({.margin = {10.0f, 0.0f, 6.0f, 7.0f}});
      parts.cells.apply({.fillX = true, .autoSize = scene::axes::kY});
      auto& cells = std::get<0>(parts.cells.fChildren);
      cells.reserve(custom.size());
      for (const emote& one : custom)
        cells.emplace_back(p, one);
    }
    // The recently used: emoji as they were picked, text already.
    section(emoji_panel* p, std::string_view name, const std::vector<std::string>& glyphs)
        : parts{.title = nodes::Text(std::string(name), 13.0f, dim_colour, true)} {
      fState.apply({.fillX = true, .autoSize = scene::axes::kY});
      parts.title.apply({.margin = {10.0f, 0.0f, 6.0f, 7.0f}});
      parts.cells.apply({.fillX = true, .autoSize = scene::axes::kY});
      auto& cells = std::get<0>(parts.cells.fChildren);
      cells.reserve(glyphs.size());
      for (const std::string& one : glyphs)
        cells.emplace_back(p, one);
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
  // An emoji and its five tones in a row over it, as tdesktop's; a pick
  // closes it.
  struct tone_strip : nodes::Stack {
    struct parts_t {
      std::vector<cell> each;
    } parts;
    tone_strip(emoji_panel* p, const alef::emoji& base, const std::vector<const alef::emoji*>& tones, float x,
               float y) {
      this->setHorizontal();
      const float wide = kCell * static_cast<float>(tones.size() + 1) + 8.0f;
      fState.apply({.place = scene::anchor::kTopLeft, .x = x, .y = y, .width = wide, .height = kCell + 8.0f,
                    .padding = {4.0f, 4.0f, 4.0f, 4.0f}, .cornerRadius = 8.0f, .background = sidebar_colour,
                    .border = scene::Border{band_colour, 1.0f},
                    .shadow = scene::Shadow{skia::colorSetARGB(70, 0, 0, 0), 3.0f}});
      parts.each.reserve(tones.size() + 1);
      parts.each.emplace_back(p, logic::emoji_text(base));
      for (const alef::emoji* one : tones)
        parts.each.emplace_back(p, logic::emoji_text(*one));
    }
    [[nodiscard]] bool acceptsInput() const { return true; }
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
    // Over the rest: an emoji's tones, while they are asked for.
    std::optional<tone_strip> tones;
  } parts;
  // A pick made: the tones, if open, closed at the next frame -- not now,
  // from inside one of their own cells.
  bool tones_done = false;
  // Whether a search is shown, not the groups: no tab is lit then.
  bool searching = false;
  // Where the groups begin in the list: after the recently used, if any.
  std::size_t first_group = 0;

  // Sized by where it is shown.
  explicit emoji_panel(Pick what) : pick(std::move(what)), parts{.field = field_t("Search emoji", {this})} {
    auto& [field, list, footer, tones] = parts;
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
    all.reserve(alef::emoji_groups.size() + 1);
    first_group = 0;
    {
      // tdesktop's: what was picked lately first, then its default list
      // (lib_ui's GetDefaultRecent), up to the section's number -- so the
      // section is full from the first run, as Telegram's.
      std::vector<std::string> shown = recent_emoji();
      for (const char* one : {"😂", "😘", "❤️", "😍", "😊", "😁", "👍", "☺️", "😔", "😄", "😭", "💋",
                              "😒", "😳", "😜", "🙈", "😉", "😃", "😢", "😝", "😱", "😡", "😏", "😞",
                              "😅", "😚", "🙊", "😌", "😀", "😋", "😆", "👌", "😐", "😕"})
        if (shown.size() < 42 && std::ranges::find(shown, std::string(one)) == shown.end())
          shown.emplace_back(one);
      all.emplace_back(this, "Recently used", shown);
      ++first_group;
    }
    if (!chat_emotes().empty()) {
      all.emplace_back(this, "Custom", chat_emotes());
      ++first_group;
    }
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
    const std::size_t at = group + first_group;
    if (at >= all.size() || all[at].bounds().isEmpty())
      return;
    auto& list = parts.list;
    list.scrollTo(std::max(0.0f, list.current() + (all[at].bounds().fTop - list.bounds().fTop)));
  }
  // An emoji's tones over its cell, kept inside the panel; nothing for one
  // that takes none.
  bool show_tones(const alef::emoji& base, const skia::SkRect& at) {
    const auto found = logic::tones_of(base);
    if (found.empty())
      return false;
    const skia::SkRect box = this->bounds();
    const float wide = kCell * static_cast<float>(found.size() + 1) + 8.0f;
    const float x = std::clamp(at.fLeft - box.fLeft - 4.0f, 0.0f, std::max(0.0f, box.width() - wide));
    const float y = std::max(0.0f, at.fTop - box.fTop - kCell - 12.0f);
    parts.tones.emplace(this, base, found, x, y);
    tones_done = false;
    this->invalidateLayout();
    return true;
  }
  // The tab of the group at the top of the list lit.
  void update(double) {
    if (tones_done) {
      tones_done = false;
      if (parts.tones) {
        parts.tones.reset();
        this->invalidateLayout();
      }
    }
    auto& all = this->sections();
    std::size_t lit = 0;
    const float top = parts.list.bounds().fTop + 1.0f;
    for (std::size_t s = first_group; s < all.size(); ++s)
      if (!all[s].bounds().isEmpty() && all[s].bounds().fTop <= top)
        lit = s - first_group;
    for (tab& each : parts.footer.parts.each)
      if (const bool on = !searching && each.group == lit; on != each.fState.selected())
        each.fState.apply({.selected = on});
  }
};

// What the menu's emoji do: react with it.
template <class Actions>
struct react_with {
  Actions* actions = nullptr;
  void operator()(const std::string&, const std::string& key) const { actions->menu_react(key); }
};
// What the input's emoji do: go into what is written.
template <class Actions>
struct insert_emoji_into {
  Actions* actions = nullptr;
  void operator()(const std::string& text, const std::string&) const { actions->insert_emoji(text); }
};

// The GIFs saved, as tdesktop's GIF tab shows them: a grid of them playing,
// newest first; a press sends one into the chat.
template <class Actions>
struct gif_grid : nodes::Stack {
  struct gif_cell : nodes::Stack {
    Actions* actions;
    std::string path;
    std::string key;
    struct parts_t {
      nodes::Image<from_moving_whole> picture;
    } parts;
    gif_cell(Actions* a, std::string p)
        : actions(a), path(p), key("gif:" + p),
          parts{.picture = nodes::Image<from_moving_whole>({"gif:" + p})} {
      fState.apply({.width = 104.0f, .height = 104.0f, .margin = {2.0f, 2.0f, 2.0f, 2.0f}, .cornerRadius = 6.0f,
                    .background = tile_colour, .masking = true});
      parts.picture.apply({.fill = true, .cornerRadius = 6.0f});
    }
    [[nodiscard]] bool acceptsInput() const { return true; }
    [[nodiscard]] bool onClick(float, float) {
      actions->send_gif(path);
      return true;
    }
    // Drawn again each frame while it moves, for its next frame.
    [[nodiscard]] bool settling() const { return animations().has(key); }
    void update(double) {
      if (animations().has(key))
        parts.picture.markDamaged();
    }
  };
  using cells_t = nodes::Flow<std::vector<gif_cell>>;
  struct parts_t {
    nodes::Text empty;
    nodes::ScrollContainer<cells_t> list{
        cells_t({.direction = nodes::direction::horizontal{}, .spacingX = 0.0f, .spacingY = 0.0f, .wrap = true}, {})};
  } parts;
  Actions* actions = nullptr;

  explicit gif_grid(Actions* a)
      : parts{.empty = nodes::Text("No saved GIFs yet. Save one from a GIF's menu.", 13.0f, dim_colour)}, actions(a) {
    auto& [empty, list] = parts;
    fState.apply({.padding = {4.0f, 4.0f, 4.0f, 4.0f}});
    empty.apply({.margin = {12.0f, 12.0f, 0.0f, 12.0f}});
    list.apply({.fillX = true, .grow = scene::axes::kY});
    std::get<0>(list.fChildren).apply({.fillX = true, .autoSize = scene::axes::kY});
  }
  // The saved ones, as the program lists them: newest first.
  void show(const std::vector<std::string>& paths) {
    auto& cells = std::get<0>(std::get<0>(parts.list.fChildren).fChildren);
    cells.clear();
    cells.reserve(paths.size());
    for (const std::string& one : paths)
      cells.emplace_back(actions, one);
    parts.empty.setVisible(paths.empty());
    parts.list.invalidateLayout();
    parts.list.scrollTo(0.0f);
  }
};

// The input's emoji, as tdesktop's panel: a card over the chat, 345 wide
// (emojiPanWidth), 278 to 640 high, rounded 8, its bottom right at the top
// right of the button that opened it; a press off it closes it. It stays
// open while emoji are picked, and the input keeps the keys.
template <class Actions>
struct emoji_popup : scene::Node {
  struct card_t : nodes::Stack {
    using panel_t = emoji_panel<insert_emoji_into<Actions>>;
    // Emoji, stickers or GIFs, as tdesktop's tabs at the panel's top.
    struct tab : nodes::Stack {
      card_t* card;
      popup_page_t page;
      struct parts_t {
        nodes::Text label;
      } parts;
      tab(card_t* c, popup_page_t p, std::string name)
          : card(c), page(p), parts{.label = nodes::Text(std::move(name), 13.0f, text_colour, true)} {
        this->setHorizontal();
        fStack.justify = nodes::justify::middle{};
        fState.apply({.width = 80.0f, .height = 28.0f, .cornerRadius = 6.0f, .hoverBackground = chosen_colour,
                      .selectedBackground = tile_colour});
        parts.label.apply({.alignSelf = scene::align::kMiddle});
      }
      [[nodiscard]] bool acceptsInput() const { return true; }
      [[nodiscard]] bool onClick(float, float) {
        card->show(page);
        return true;
      }
    };
    struct tabs_row : nodes::Stack {
      struct parts_t {
        tab emoji;
        tab stickers;
        tab gifs;
      } parts;
      explicit tabs_row(card_t* c)
          : parts{.emoji = tab(c, popup_page::emoji{}, "Emoji"),
                  .stickers = tab(c, popup_page::stickers{}, "Stickers"),
                  .gifs = tab(c, popup_page::gifs{}, "GIFs")} {
        this->setHorizontal();
        this->setGap(4.0f);
        fState.apply({.fillX = true, .height = 36.0f, .padding = {4.0f, 8.0f, 4.0f, 8.0f}});
      }
    };
    struct parts_t {
      tabs_row tabs;
      panel_t panel;
      sticker_grid<Actions> stickers;
      gif_grid<Actions> gifs;
    } parts;
    Actions* actions = nullptr;
    explicit card_t(Actions* a)
        : parts{.tabs = tabs_row(this),
                .panel = panel_t(insert_emoji_into<Actions>{a}),
                .stickers = sticker_grid<Actions>(a),
                .gifs = gif_grid<Actions>(a)},
          actions(a) {
      fState.apply({.width = 345.0f, .height = 360.0f, .cornerRadius = 8.0f, .background = sidebar_colour,
                    .border = scene::Border{band_colour, 1.0f},
                    .shadow = scene::Shadow{skia::colorSetARGB(70, 0, 0, 0), 3.0f}});
      parts.panel.apply({.fillX = true, .grow = scene::axes::kY});
      parts.stickers.apply({.fillX = true, .grow = scene::axes::kY});
      parts.gifs.apply({.fillX = true, .grow = scene::axes::kY});
      this->show(popup_page::emoji{});
    }
    // One tab's page shown, the others hidden; the GIFs asked of the program
    // as their tab opens, for what was saved since.
    void show(const popup_page_t& page) {
      const auto [emoji, stickers, gifs] =
          std::visit(overloaded{[](popup_page::emoji) { return std::array{true, false, false}; },
                                [](popup_page::stickers) { return std::array{false, true, false}; },
                                [](popup_page::gifs) { return std::array{false, false, true}; }},
                     page);
      parts.panel.setVisible(emoji);
      parts.stickers.setVisible(stickers);
      parts.gifs.setVisible(gifs);
      parts.tabs.parts.emoji.fState.apply({.selected = emoji});
      parts.tabs.parts.stickers.fState.apply({.selected = stickers});
      parts.tabs.parts.gifs.fState.apply({.selected = gifs});
      if (gifs)
        actions->show_gifs();
      this->invalidateLayout();
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
          : actions(a), key(k), parts{.face = nodes::Text(std::move(k), 22.0f, text_colour)} {
        auto& face = parts.face;
        this->setHorizontal();
        fStack.justify = nodes::justify::middle{};
        // tdesktop's reactionCornerSize (36 by 32) and reactionCornerImage (22).
        fState.apply({.width = 36.0f, .height = 32.0f, .cornerRadius = 16.0f, .hoverBackground = chosen_colour});
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
        // As wide as what is in it: the menu is sized by it, not it by the
        // menu -- a menu of a set width had the arrow run out past its edge.
        this->setHorizontal();
        fState.apply({.autoSize = scene::axes::kBoth, .padding = {2.0f, 6.0f, 4.0f, 6.0f}});
        for (const char* key : {"👍", "❤️", "😂", "😮", "😢", "🙏"})
          each.emplace_back(a, key);
        more.apply({.width = 28.0f, .height = 32.0f, .cornerRadius = 14.0f});
      }
    };
    Actions* actions_of = nullptr;
    using reply_row = row_item<ask<Actions, &Actions::menu_reply>>;
    using edit_row = row_item<ask<Actions, &Actions::menu_edit>>;
    using later_row = row_item<not_yet<Actions>>;
    using copy_row = row_item<ask<Actions, &Actions::menu_copy>>;
    using link_row = row_item<ask<Actions, &Actions::menu_copy_link>>;
    using save_row = row_item<ask<Actions, &Actions::menu_save>>;
    using gif_row = row_item<ask<Actions, &Actions::menu_save_gif>>;
    using pin_row = row_item<ask<Actions, &Actions::menu_pin>>;
    using reactions_row = row_item<ask<Actions, &Actions::menu_reactions>>;
    using forward_row = row_item<ask<Actions, &Actions::menu_forward>>;
    using source_row = row_item<ask<Actions, &Actions::menu_view_source>>;
    using delete_row = row_item<ask<Actions, &Actions::menu_delete>>;
    // As tdesktop's, in its order: the quick reactions; every emoji, in
    // place of the rest once asked for; Reply, Edit, Pin, Copy, Copy
    // Message Link, Save As, Forward, Delete; and who has seen it -- how
    // many, and their names under it -- at the foot.
    struct parts_t {
      quick_row quick;
      nodes::Box<> quick_band{band_colour};
      reply_row reply;
      edit_row edit;
      pin_row pin;
      copy_row copy;
      link_row copy_link;
      save_row save;
      gif_row save_gif;
      reactions_row reactions;
      forward_row forward;
      source_row source;
      delete_row remove;
      nodes::Box<> seen_band{band_colour};
      row_item<nothing> seen;
      std::vector<nodes::Text> seen_names;
      // Every emoji, once asked for: over the items, out of their flow, and
      // after them, so drawn on top of them and pressed first.
      std::optional<emoji_panel<react_with<Actions>>> emoji;
    } parts;
    void expand() {
      auto& [quick, quick_band, reply, edit, pin, copy, copy_link, save, save_gif, reactions, forward, source,
             remove, seen_band, seen, seen_names, emoji] = parts;
      if (emoji)
        return;
      // As Telegram's: the list takes the room the items had under the
      // quick reactions, and unrolls down over them from the top; they stay
      // under it until it is down, and the menu keeps its size -- grown only
      // where the items left too little room for a list.
      const skia::SkRect box = fState.contentBox();
      const float under = box.fBottom - quick_band.bounds().fBottom;
      rolled = std::max(under, kEmojiLeast);
      fState.apply({.minHeight = this->bounds().height() + (rolled - under)});
      emoji.emplace(react_with<Actions>{actions_of});
      emoji->apply({.place = scene::anchor::kTopLeft,
                     .y = quick_band.bounds().fBottom - box.fTop,
                     .fillX = true,
                     .height = 0.0f,
                     .background = sidebar_colour,
                     .masking = true});
      unroll.jump(0.0f);
      unroll.setTarget(rolled);
      quick.parts.more.setVisible(false);
      this->invalidateLayout();
    }
    // The items, once the list is down over them: gone, the menu keeping
    // its size by its least height.
    void hide_items() {
      auto& [quick, quick_band, reply, edit, pin, copy, copy_link, save, save_gif, reactions, forward, source, remove,
             seen_band, seen, seen_names, emoji] = parts;
      for (scene::Node* item : std::initializer_list<scene::Node*>{&reply, &edit, &pin, &copy, &copy_link, &save,
                                                                   &save_gif, &reactions, &forward, &source,
                                                                   &remove, &seen_band, &seen})
        item->setVisible(false);
      for (auto& name : seen_names)
        name.setVisible(false);
      this->invalidateLayout();
    }
    static constexpr float kEmojiLeast = 220.0f;
    float rolled = 0.0f;
    skiff::paint::Tween unroll{0.0f, 220.0f};
    [[nodiscard]] bool settling() const { return unroll.moving(); }
    void update(double now_ms) {
      if (unroll.step(now_ms) && parts.emoji) {
        parts.emoji->apply({.height = unroll.value()});
        if (!unroll.moving())
          this->hide_items();
        this->invalidateLayout();
      }
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
                .pin = pin_row(facts.pinned ? "Unpin" : "Pin", {a}, icon::check{}),
                .copy = copy_row(facts.selection ? "Copy Selected Text" : "Copy Text", {a}, icon::clip{}),
                .copy_link = link_row("Copy Message Link", {a}, icon::info{}),
                .save = save_row("Save As…", {a}, icon::send{}),
                .save_gif = gif_row("Save GIF", {a}, icon::check{}),
                .reactions = reactions_row(facts.reaction_count == 1 ? std::string("1 reaction")
                                                                     : std::format("{} reactions", facts.reaction_count),
                                           {a}, icon::people{}),
                .forward = forward_row("Forward", {a}, icon::send{}),
                .source = source_row("View Source", {a}, icon::info{}),
                .remove = delete_row("Delete", {a}, icon::close{}),
                .seen = row_item<nothing>(facts.seen.empty() ? std::string("Not seen yet")
                                                             : std::format("Seen by {}", facts.seen.size()),
                                          {}, icon::check{})} {
      auto& [quick, quick_band, reply, edit, pin, copy, copy_link, save, save_gif, reactions, forward, source,
             remove, seen_band, seen, seen_names, emoji] = parts;
      const std::vector<std::string>& readers = facts.seen;
      quick_band.apply({.fillX = true, .height = 1.0f, .margin = {0.0f, 0.0f, 4.0f, 0.0f}});
      // A menu's rows as tdesktop's menuWithIcons: 8 over and under the
      // 13px normalFont's line -- 33 high -- the icon 15 in, the text 54 in.
      const auto compact = [](auto& row) {
        row.apply({.height = 33.0f, .padding = {0.0f, 17.0f, 0.0f, 15.0f}});
        row.setGap(15.0f);
        row.parts.mark.apply({.width = 24.0f, .height = 24.0f});
        row.parts.label.setFontSize(13.0f);
      };
      compact(reply);
      compact(edit);
      compact(pin);
      compact(copy);
      compact(copy_link);
      compact(save);
      compact(save_gif);
      compact(reactions);
      compact(forward);
      compact(source);
      compact(remove);
      compact(seen);
      edit.setVisible(facts.own && !facts.text.empty() && !facts.media);
      copy.setVisible(!facts.copied.empty());
      copy_link.setVisible(!facts.link.empty());
      save.setVisible(facts.media.has_value());
      save_gif.setVisible(facts.media.has_value() && facts.moving);
      remove.setVisible(facts.own);
      pin.setVisible(facts.pinnable);
      source.setVisible(facts.pinnable);
      // Who reacted, as Telegram's menu lists them: wherever there are any.
      reactions.setVisible(facts.reaction_count > 0);
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
      // As wide as its widest -- the quick reactions -- and no narrower than a
      // menu reads well at; the items fill that width.
      fState.apply({.autoSize = scene::axes::kBoth, .minWidth = 220.0f, .padding = {6.0f, 0.0f, 6.0f, 0.0f}, .cornerRadius = 10.0f, .background = sidebar_colour, .border = scene::Border{band_colour, 1.0f},
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
