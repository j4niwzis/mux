// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:context_menu -- A message's menu.
export module mux.ui:context_menu;

import std;
import splice;
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
import :names;

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
using popup_page_t = splice::variant<popup_page::emoji, popup_page::stickers, popup_page::gifs>;

// What the mouse rests on in a panel of emoji or stickers, shown large over
// it, as Telegram's preview: its picture (or its glyph) and its name. Cells
// say it here as the mouse stays on them, and let it go as it leaves; the
// panel shows what is said -- no pointer between them.
struct previewed {
  std::string key;    // a picture's source, or a glyph
  std::string label;  // its :shortcode:, or nothing
  bool picture = false;
  friend bool operator==(const previewed&, const previewed&) = default;
};
inline std::optional<previewed>& previewed_emote() {
  static std::optional<previewed> now;
  return now;
}
// How long the mouse rests on one before it is shown large.
inline constexpr double kPreviewAfterMs = 450.0;
// A cell's resting: said once the mouse has stayed long enough, let go as
// it leaves. Kept by each cell.
struct dwell {
  std::optional<double> since;
  bool said = false;
  // While it counts, frames are wanted.
  [[nodiscard]] bool counting(bool hovered) const { return hovered && !said; }
  void step(bool hovered, double now, const previewed& what) {
    if (!hovered) {
      since.reset();
      if (said && previewed_emote() == what)
        previewed_emote().reset();
      said = false;
      return;
    }
    if (!since)
      since = now;
    if (!said && now - *since >= kPreviewAfterMs) {
      said = true;
      previewed_emote() = what;
    }
  }
};
// The preview, over a panel: a dark plate, the picture or glyph large, its
// name under it.
struct emote_preview : nodes::Stack {
  static constexpr float kSide = 200.0f;
  struct parts_t {
    std::optional<nodes::Image<from_avatars>> picture;
    nodes::Text glyph;
    nodes::Text label;
  } parts;
  explicit emote_preview(const previewed& shown)
      : parts{.glyph = nodes::Text(shown.picture ? std::string() : shown.key, 120.0f, text_colour),
              .label = nodes::Text(shown.label, 14.0f, text_colour)} {
    this->setGap(8.0f);
    fStack.justify = nodes::justify::middle{};
    fState.apply({.place = scene::anchor::kCentre, .autoSize = scene::axes::kBoth,
                  .padding = {16.0f, 16.0f, 16.0f, 16.0f}, .cornerRadius = 14.0f,
                  .background = (sidebar_colour & 0x00FFFFFFu) | (0xF0u << 24)});
    if (shown.picture) {
      parts.picture.emplace(from_avatars{shown.key});
      parts.picture->apply({.width = kSide, .height = kSide, .alignSelf = scene::align::kMiddle});
    }
    parts.glyph.setVisible(!shown.picture);
    parts.glyph.apply({.alignSelf = scene::align::kMiddle});
    parts.label.setVisible(!shown.label.empty());
    parts.label.apply({.alignSelf = scene::align::kMiddle});
  }
};
// A panel's preview kept to what the cells say: made anew as it changes.
inline bool follow_preview(std::optional<emote_preview>& shown, std::optional<previewed>& of) {
  if (of == previewed_emote())
    return false;
  of = previewed_emote();
  shown.reset();
  if (of)
    shown.emplace(*of);
  return true;
}

// The stickers sent lately, newest first, as tdesktop's Recent (at most 20):
// kept while the program runs.
inline std::vector<emote>& recent_stickers() {
  static std::vector<emote> kept;
  return kept;
}
// The favourites, as tdesktop's Favorite stickers: from a sticker's menu,
// in any chat; newest first.
inline std::vector<emote>& favourite_stickers() {
  static std::vector<emote> kept;
  return kept;
}
// Whether either list changed since the program last kept them.
inline bool& stickers_changed() {
  static bool changed = false;
  return changed;
}
inline void remember_sticker(const emote& one) {
  constexpr std::size_t kKept = 20;
  auto& all = recent_stickers();
  std::erase_if(all, [&](const emote& each) { return each.url == one.url; });
  all.insert(all.begin(), one);
  if (all.size() > kKept)
    all.resize(kKept);
  stickers_changed() = true;
}
[[nodiscard]] inline bool is_favourite(std::string_view url) {
  return std::ranges::contains(favourite_stickers(), url, &emote::url);
}
// Made a favourite, or no longer one.
inline void flip_favourite(const emote& one) {
  auto& all = favourite_stickers();
  if (is_favourite(one.url))
    std::erase_if(all, [&](const emote& each) { return each.url == one.url; });
  else
    all.insert(all.begin(), one);
  stickers_changed() = true;
}

// The chat's stickers, as tdesktop's tab: a search at its top; the packs one
// under another in one list that scrolls -- Recent first -- each its name
// over a grid of its stickers; and a footer of the packs' pictures that
// brings each into view, lit for the one in view. A press sends one.
template <class Actions>
struct sticker_grid : nodes::Stack {
  static constexpr float kCell = 78.0f;
  struct cell : nodes::Stack {
    Actions* actions;
    emote sticker;
    struct parts_t {
      nodes::Image<from_avatars> picture;
    } parts;
    cell(Actions* a, emote one)
        : actions(a), sticker(one), parts{.picture = nodes::Image<from_avatars>({one.url})} {
      fState.apply({.width = kCell, .height = kCell, .margin = {2.0f, 2.0f, 2.0f, 2.0f}, .padding = {4.0f, 4.0f, 4.0f, 4.0f},
                    .cornerRadius = 6.0f, .hoverBackground = chosen_colour});
      parts.picture.apply({.fill = true});
      parts.picture.keepBox();  // the cell's size, whatever the sticker
    }
    dwell resting;
    [[nodiscard]] bool settling() const { return resting.counting(this->hovered()); }
    void update(double now) { resting.step(this->hovered(), now, {sticker.url, ":" + sticker.shortcode + ":", true}); }
    [[nodiscard]] bool acceptsInput() const { return true; }
    [[nodiscard]] bool hoverChangesAppearance() const { return true; }
    [[nodiscard]] bool onClick(float, float) {
      remember_sticker(sticker);
      actions->send_sticker(sticker);
      return true;
    }
  };
  // A pack: its name over its stickers.
  struct section : cell_section<cell> {
    section(Actions* a, std::string name, const std::vector<emote>& stickers)
        : cell_section<cell>(std::move(name)) {
      auto& cells = this->each();
      cells.reserve(stickers.size());
      for (const emote& one : stickers)
        cells.emplace_back(a, one);
    }
  };
  // A pack's tab in the footer: its picture -- the pack's own, else its
  // first sticker's -- or, for Recent, a clock.
  struct tab : nodes::Stack {
    sticker_grid* grid;
    std::size_t at;
    struct parts_t {
      std::optional<nodes::Image<from_avatars>> picture;
      std::optional<nodes::Text> mark;
    } parts;
    tab(sticker_grid* g, std::size_t place, std::optional<std::string> picture, std::string mark = "\u23F2")
        : grid(g), at(place) {
      this->setHorizontal();
      fStack.justify = nodes::justify::middle{};
      fState.apply({.width = 30.0f, .height = 30.0f, .shrink = scene::axes::kX, .minWidth = 16.0f, .alignSelf = scene::align::kMiddle, .cornerRadius = 6.0f,
                    .hoverBackground = chosen_colour, .selectedBackground = tile_colour});
      if (picture) {
        parts.picture.emplace(from_avatars{*picture});
        parts.picture->apply({.width = 24.0f, .height = 24.0f, .alignSelf = scene::align::kMiddle});
        parts.picture->keepBox();
      } else {
        parts.mark.emplace(std::move(mark), 16.0f, text_colour);
        parts.mark->apply({.alignSelf = scene::align::kMiddle});
      }
    }
    [[nodiscard]] bool acceptsInput() const { return true; }
    [[nodiscard]] bool onClick(float, float) {
      grid->bring(at);
      return true;
    }
  };
  struct searched {
    sticker_grid* grid;
    void operator()(std::string_view text) const { grid->search(text); }
  };
  using footer_row = tab_strip<tab>;
  using field_t = widgets::TextBox<searched>;
  using list_t = nodes::ScrollContainer<nodes::Flow<std::vector<section>>>;
  struct parts_t {
    field_t field;
    nodes::Text empty;
    list_t list{nodes::Flow<std::vector<section>>({.spacingY = 0.0f, .wrap = false}, {})};
    footer_row footer;
    // Over the rest: the sticker the mouse rests on, large.
    std::optional<emote_preview> preview;
  } parts;
  Actions* actions = nullptr;
  std::optional<previewed> preview_of;
  bool searching = false;
  // The pictures of the packs' tabs, as show_all made them.
  std::vector<std::string> tab_pictures;
  [[nodiscard]] bool settling() const { return previewed_emote() != preview_of; }

  explicit sticker_grid(Actions* a)
      : parts{.field = field_t("Search stickers", {this}),
              .empty = nodes::Text("No stickers here. A room's sticker packs, and yours, show here.", 13.0f, dim_colour)},
        actions(a) {
    auto& [field, empty, list, footer, preview] = parts;
    this->setGap(4.0f);
    fState.apply({.padding = {7.0f, 0.0f, 4.0f, 7.0f}});
    field.setSearchIcon(true);
    field.apply({.fillX = true, .height = 32.0f, .margin = {0.0f, 7.0f, 0.0f, 0.0f}});
    // Wrapped at the panel's width, not one line running past its edges.
    empty.setWrapped(true);
    empty.apply({.fillX = true, .margin = {12.0f, 12.0f, 0.0f, 12.0f}});
    list.apply({.fillX = true, .grow = scene::axes::kY});
    std::get<0>(list.fChildren).apply({.fillX = true, .autoSize = scene::axes::kY});
    footer.setHorizontal();
    footer.setGap(4.0f);
    footer.apply({.fillX = true, .height = 36.0f});
    this->show_all();
  }
  [[nodiscard]] std::vector<section>& sections() { return std::get<0>(std::get<0>(parts.list.fChildren).fChildren); }
  // The packs as the chat has them, in the order they first come; the
  // unnamed together, as "Stickers".
  [[nodiscard]] static std::string pack_of(const emote& one) { return one.pack.empty() ? std::string("Stickers") : one.pack; }
  [[nodiscard]] static std::vector<std::pair<std::string, std::vector<emote>>> packs() {
    const std::vector<std::string> names = chat_stickers() | std::views::transform(pack_of) | std::ranges::to<std::vector>();
    return std::views::iota(std::size_t{0}, names.size()) |
           std::views::filter([&](std::size_t i) { return std::ranges::find(names, names[i]) == names.begin() + static_cast<std::ptrdiff_t>(i); }) |
           std::views::transform([&](std::size_t i) {
             return std::pair{names[i], chat_stickers() | std::views::filter([&](const emote& one) { return pack_of(one) == names[i]; }) |
                                            std::ranges::to<std::vector>()};
           }) |
           std::ranges::to<std::vector>();
  }
  // Recent, then every pack, one under another; a tab for each.
  void show_all() {
    auto& all = this->sections();
    all.clear();
    auto& tabs = parts.footer.parts.each;
    tabs.clear();
    tab_pictures.clear();
    // Recent: those sent lately that the chat still has.
    std::vector<emote> recent = recent_stickers() | std::views::filter([](const emote& one) {
                                  return std::ranges::contains(chat_stickers(), one.url, &emote::url);
                                }) |
                                std::ranges::to<std::vector>();
    if (!recent.empty()) {
      all.emplace_back(actions, "Recently used", recent);
      tabs.emplace_back(this, all.size() - 1, std::nullopt, "\u23F2");
    }
    // The favourites: whichever chat they came from -- a sticker is its
    // picture's URL, sent anywhere.
    if (!favourite_stickers().empty()) {
      all.emplace_back(actions, "Favourites", favourite_stickers());
      tabs.emplace_back(this, all.size() - 1, std::nullopt, "\u2605");
    }
    for (auto& [name, stickers] : packs()) {
      const std::optional<std::string> picture = stickers.front().pack_avatar ? stickers.front().pack_avatar
                                                                              : std::optional<std::string>(stickers.front().url);
      all.emplace_back(actions, name, stickers);
      tabs.emplace_back(this, all.size() - 1, picture);
      if (picture)
        tab_pictures.push_back(*picture);
    }
    searching = false;
    parts.empty.setVisible(chat_stickers().empty() && favourite_stickers().empty());
    parts.footer.setVisible(tabs.size() > 1);
    parts.list.invalidateLayout();
    parts.footer.invalidateLayout();
    parts.list.scrollTo(0.0f);
  }
  // Those whose shortcode, words or pack have what is typed.
  void search(std::string_view query) {
    if (query.empty()) {
      this->show_all();
      return;
    }
    const auto lower = [](std::string_view text) {
      return text | std::views::transform([](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }) |
             std::ranges::to<std::string>();
    };
    const std::string wanted = lower(query);
    const std::vector<emote> found = chat_stickers() | std::views::filter([&](const emote& one) {
                                       return lower(one.shortcode).contains(wanted) || lower(one.body).contains(wanted) ||
                                              lower(one.pack).contains(wanted);
                                     }) |
                                     std::ranges::to<std::vector>();
    auto& all = this->sections();
    all.clear();
    all.emplace_back(actions, found.empty() ? std::string("Nothing found") : std::string("Search results"), found);
    searching = true;
    parts.list.invalidateLayout();
    parts.list.scrollTo(0.0f);
  }
  // The pictures it shows -- the cells in view and a row either side, and
  // the packs' tabs -- for the program to ask for. Pictures were asked for
  // only as the window refreshed, which neither opening the panel nor
  // scrolling it does: a sticker not loaded already stayed empty.
  [[nodiscard]] std::vector<std::string> pictures_shown() {
    if (!fState.visible())
      return {};
    const skia::SkRect view = parts.list.bounds().makeOutset(0.0f, kCell);
    std::vector<std::string> out =
        this->sections() | std::views::transform([](section& one) -> std::vector<cell>& { return one.each(); }) | std::views::join |
        std::views::filter([&](cell& one) { return !one.bounds().isEmpty() && parts.list.toView(one.bounds()).intersects(view); }) |
        std::views::transform([](cell& one) { return one.sticker.url; }) | std::ranges::to<std::vector>();
    out.append_range(tab_pictures);
    return out;
  }
  // A pack brought to the top of the list, as its tab does.
  void bring(std::size_t at) {
    if (searching)
      parts.field.setText({});
    auto& all = this->sections();
    if (at >= all.size() || all[at].bounds().isEmpty())
      return;
    auto& list = parts.list;
    list.scrollTo(std::max(0.0f, list.current() + (list.toView(all[at].bounds()).fTop - list.bounds().fTop)));
  }
  // The tab of the pack at the top of the list lit; the preview kept to what
  // the cells say.
  void update(double) {
    if (follow_preview(parts.preview, preview_of))
      this->invalidateLayout();
    auto& all = this->sections();
    std::size_t lit = 0;
    const float top = parts.list.bounds().fTop + 1.0f;
    for (std::size_t s = 0; s < all.size(); ++s)
      if (!all[s].bounds().isEmpty() && parts.list.toView(all[s].bounds()).fTop <= top)
        lit = s;
    for (tab& each : parts.footer.parts.each)
      if (const bool on = !searching && each.at == lit; on != each.fState.selected())
        each.fState.apply({.selected = on});
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
  // A reaction that is text -- Matrix takes any -- as SchildiChat offers one:
  // what is searched, itself, at the top of the results, where the panel
  // reacts rather than writes.
  struct text_chip : nodes::Stack {
    emoji_panel* panel;
    std::string text;
    struct parts_t {
      nodes::Text label{"", 13.0f, text_colour};
    } parts;
    explicit text_chip(emoji_panel* p) : panel(p) {
      this->setHorizontal();
      fState.apply({.fillX = true, .height = 34.0f, .margin = {4.0f, 7.0f, 2.0f, 0.0f}, .padding = {0.0f, 12.0f, 0.0f, 12.0f},
                    .cornerRadius = 17.0f, .background = tile_colour, .hoverBackground = chosen_colour});
      parts.label.setElided(true);
      parts.label.apply({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
      this->setVisible(false);
    }
    // What is typed, its spaces at either end cut: offered where there is
    // any.
    void show(std::string_view typed) {
      const auto space = [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; };
      while (!typed.empty() && space(typed.front()))
        typed.remove_prefix(1);
      while (!typed.empty() && space(typed.back()))
        typed.remove_suffix(1);
      text = std::string(typed);
      parts.label.setText(std::format("React with \u201C{}\u201D", text));
      if (this->visible() != !text.empty())
        this->setVisible(!text.empty());
      this->invalidateLayout();
    }
    [[nodiscard]] bool acceptsInput() const { return true; }
    [[nodiscard]] bool hoverChangesAppearance() const { return true; }
    [[nodiscard]] bool onClick(float, float) {
      panel->tones_done = true;
      panel->pick(text, text);
      return true;
    }
  };
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
    dwell resting;
    [[nodiscard]] bool settling() const { return resting.counting(this->hovered()); }
    void update(double now) {
      resting.step(this->hovered(), now,
                   picture_url.empty() ? previewed{glyph, std::string(), false} : previewed{picture_url, glyph, true});
    }
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
      parts.picture->keepBox();  // fixed: its coming repaints, lays nothing out
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
  struct section : cell_section<cell> {
    section(emoji_panel* p, std::string_view name, const std::vector<const alef::emoji*>& all)
        : cell_section<cell>(std::string(name)) {
      auto& cells = this->each();
      cells.reserve(all.size());
      for (const alef::emoji* one : all)
        cells.emplace_back(p, logic::emoji_text(*one), one);
    }
    // The chat's custom emoji, as pictures.
    section(emoji_panel* p, std::string_view name, const std::vector<emote>& custom)
        : cell_section<cell>(std::string(name)) {
      auto& cells = this->each();
      cells.reserve(custom.size());
      for (const emote& one : custom)
        cells.emplace_back(p, one);
    }
    // The recently used: emoji as they were picked, text already.
    section(emoji_panel* p, std::string_view name, const std::vector<std::string>& glyphs)
        : cell_section<cell>(std::string(name)) {
      auto& cells = this->each();
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
          parts{.face = nodes::Text(logic::emoji_text(logic::emoji_group_face(g)), 16.0f, text_colour)} {
      this->setHorizontal();
      fStack.justify = nodes::justify::middle{};
      fState.apply({.width = 28.0f, .height = 28.0f, .shrink = scene::axes::kX, .minWidth = 16.0f, .alignSelf = scene::align::kMiddle, .cornerRadius = 6.0f,
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
  using footer_row = tab_strip<tab>;
  using field_t = widgets::TextBox<searched>;
  using list_t = nodes::ScrollContainer<nodes::Flow<std::vector<section>>>;
  struct parts_t {
    field_t field;
    text_chip text_option;
    list_t list{nodes::Flow<std::vector<section>>({.spacingY = 0.0f, .wrap = false}, {})};
    footer_row footer;
    // Over the rest: an emoji's tones, while they are asked for.
    std::optional<tone_strip> tones;
    // And the emoji the mouse rests on, large.
    std::optional<emote_preview> preview;
  } parts;
  std::optional<previewed> preview_of;
  // A pick made: the tones, if open, closed at the next frame -- not now,
  // from inside one of their own cells.
  bool tones_done = false;
  // Whether a search is shown, not the groups: no tab is lit then.
  bool searching = false;
  // Where the groups begin in the list: after the recently used, if any.
  std::size_t first_group = 0;

  // Sized by where it is shown.
  explicit emoji_panel(Pick what)
      : pick(std::move(what)), parts{.field = field_t("Search emoji", {this}), .text_option = text_chip(this)} {
    auto& [field, text_option, list, footer, tones, preview] = parts;
    this->setGap(4.0f);
    fState.apply({.padding = {7.0f, 0.0f, 4.0f, 7.0f}});
    field.setSearchIcon(true);
    field.apply({.fillX = true, .height = 32.0f, .margin = {0.0f, 7.0f, 0.0f, 0.0f}});
    list.apply({.fillX = true, .grow = scene::axes::kY});
    std::get<0>(list.fChildren).apply({.fillX = true, .autoSize = scene::axes::kY});
    footer.setHorizontal();
    footer.setGap(4.0f);
    footer.apply({.fillX = true, .height = 36.0f});
    for (std::size_t g = 0; g < logic::emoji_group_count(); ++g)
      footer.parts.each.emplace_back(this, g);
    this->show_all();
  }
  [[nodiscard]] std::vector<section>& sections() { return std::get<0>(std::get<0>(parts.list.fChildren).fChildren); }
  // Every group, one under another.
  void show_all() {
    auto& all = this->sections();
    all.clear();
    all.reserve(logic::emoji_group_count() + 1);
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
    for (std::size_t g = 0; g < logic::emoji_group_count(); ++g)
      all.emplace_back(this, logic::emoji_group_name(g), logic::emoji_of_group(g));
    searching = false;
    parts.text_option.show({});
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
    // Where it reacts: what is typed, as a reaction of text.
    if (Pick::takes_text())
      parts.text_option.show(query);
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
    list.scrollTo(std::max(0.0f, list.current() + (list.toView(all[at].bounds()).fTop - list.bounds().fTop)));
  }
  // An emoji's tones over its cell, kept inside the panel; nothing for one
  // that takes none.
  bool show_tones(const alef::emoji& base, const skia::SkRect& cell) {
    // The cell as the list shows it: its rows are laid out as if unscrolled.
    const skia::SkRect at = parts.list.toView(cell);
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
  [[nodiscard]] bool settling() const { return previewed_emote() != preview_of; }
  // The tab of the group at the top of the list lit; the preview kept to
  // what the cells say.
  void update(double) {
    if (follow_preview(parts.preview, preview_of))
      this->invalidateLayout();
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
      if (!all[s].bounds().isEmpty() && parts.list.toView(all[s].bounds()).fTop <= top)
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
  // What is typed in its search, a reaction too: Matrix takes any text.
  [[nodiscard]] static constexpr bool takes_text() { return true; }
  void operator()(const std::string&, const std::string& key) const { actions->menu_react(key); }
};
// What the input's emoji do: go into what is written.
template <class Actions>
struct insert_emoji_into {
  Actions* actions = nullptr;
  [[nodiscard]] static constexpr bool takes_text() { return false; }
  // A glyph as itself; a custom emoji (its key its picture's, not its
  // text) as its picture.
  void operator()(const std::string& text, const std::string& key) const {
    actions->insert_emoji(text, key == text ? std::string() : key);
  }
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
    empty.setWrapped(true);
    empty.apply({.fillX = true, .margin = {12.0f, 12.0f, 0.0f, 12.0f}});
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
    // Docked as Telegram's apps have it, on a phone: across all of the window
    // over the field, square, its tabs a row along its bottom -- or a card
    // by its button, as tdesktop's.
    bool docked = false;
    static constexpr float kTabsHigh = 40.0f;
    void set_docked(bool on) {
      if (on == docked)
        return;
      docked = on;
      fState.apply({.padding = {0.0f, 0.0f, on ? kTabsHigh : 0.0f, 0.0f}, .cornerRadius = on ? 0.0f : 8.0f});
      parts.tabs.fState.setOutOfFlow(on);
      // In the room kept for them under the panel's own (its padding): a
      // node placed by its anchor goes by the box its parent lays out in,
      // which the padding has already taken off -- at its bottom left alone,
      // the tabs stood over the emoji groups' row.
      if (on)
        parts.tabs.apply({.place = scene::anchor::kBottomLeft, .y = kTabsHigh, .depth = 1.0f});
      parts.tabs.fStack.justify = on ? nodes::Justify{nodes::justify::middle{}} : nodes::Justify{nodes::justify::start{}};
      this->invalidateLayout();
      this->markDamaged();
    }
    // The tab shown, and a swipe across the card to the next or the one
    // before, as Telegram's apps do: where it began.
    int page_at = 0;
    std::optional<skia::SkPoint> swipe_from;
    using Node::onPointer;
    void onPointer(scene::phase::capture, const scene::pointer::down& press, scene::PointerReply&) {
      swipe_from.reset();
      if (docked && press.button <= 1)
        swipe_from = skia::SkPoint{press.x, press.y};
    }
    void onPointer(scene::phase::capture, const scene::pointer::up& lift, scene::PointerReply& reply) {
      const std::optional<skia::SkPoint> from = std::exchange(swipe_from, std::nullopt);
      if (!from)
        return;
      const float dx = lift.x - from->fX;
      const float dy = lift.y - from->fY;
      if (std::abs(dx) < 90.0f || std::abs(dy) > std::abs(dx) * 0.5f)
        return;
      static const std::array<popup_page_t, 3> kPages{popup_page::emoji{}, popup_page::stickers{}, popup_page::gifs{}};
      const int to = std::clamp(page_at + (dx < 0.0f ? 1 : -1), 0, 2);
      if (to != page_at) {
        this->show(kPages[static_cast<std::size_t>(to)]);
        reply.handle();
      }
    }
    // One tab's page shown, the others hidden; the GIFs asked of the program
    // as their tab opens, for what was saved since.
    void show(const popup_page_t& page) {
      const auto [emoji, stickers, gifs] =
          splice::visit(splice::overloaded{[](popup_page::emoji) { return std::array{true, false, false}; },
                                [](popup_page::stickers) { return std::array{false, true, false}; },
                                [](popup_page::gifs) { return std::array{false, false, true}; }},
                     page);
      parts.panel.setVisible(emoji);
      parts.stickers.setVisible(stickers);
      parts.gifs.setVisible(gifs);
      page_at = emoji ? 0 : stickers ? 1 : 2;
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
  float placed_w = 0.0f;

  emoji_popup(Actions* a, float at_right, float at_bottom)
      : parts{.card = card_t(a)}, actions(a), right(at_right), bottom(at_bottom) {
    fState.apply({.fill = true});
  }
  void layoutChildren() {
    const skia::SkRect box = fState.contentBox();
    constexpr float kMostWidth = 345.0f, kEdge = 10.0f;
    // As wide as tdesktop's panel, or as the window leaves -- a phone's.
    const float kWidth = std::min(kMostWidth, std::max(160.0f, box.width() - 2.0f * kEdge));
    const float room = std::max(0.0f, bottom - box.fTop - kEdge);
    const float h = std::min(std::clamp(box.height() * 0.6f, 278.0f, 640.0f), room);
    const float x = std::clamp(right - kWidth, kEdge, std::max(kEdge, box.width() - kWidth - kEdge));
    float y = std::max(box.fTop + kEdge, bottom - h) - box.fTop;
    // A phone's window: docked across it over the field, as Telegram's apps.
    const bool phone = box.width() < 600.0f && box.height() > box.width();
    parts.card.set_docked(phone);
    float x_at = x, w_at = kWidth, h_at = h;
    if (phone) {
      // Where the keyboard would be, as Telegram's: along the window's
      // bottom, the chat's field standing over it.
      x_at = 0.0f;
      w_at = box.width();
      h_at = std::round(box.height() * 0.4f);
      y = box.height() - h_at;
    }
    set_docked_panel_height(phone ? h_at : 0.0f);
    if (x_at != placed_x || y != placed_y || h_at != placed_h || w_at != placed_w) {
      placed_x = x_at;
      placed_y = y;
      placed_h = h_at;
      placed_w = w_at;
      parts.card.apply({.x = x_at, .y = y, .width = w_at, .height = h_at});
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
// Who has seen a message, as tdesktop's menu shows it (chat_helpers.style's
// defaultWhoRead, who_reacted_context_action.cpp): the read ticks 15 in;
// "N Seen", the one reader's name, or "Nobody Viewed", 44 in; up to three
// userpics of 22 at the right, 17 in, each 8 over the next, in a ring of the
// menu's colour. Hovered, every reader in a submenu beside the menu: a
// userpic of 30, 13 in, the name 57 in, and under it when they read.
template <class Actions>
struct seen_row : nodes::Stack {
  static constexpr float kHeight = 33.0f, kFace = 22.0f, kOverlap = 8.0f, kRight = 17.0f;
  static constexpr std::size_t kMostFaces = 3;
  // How far the readers list lies over the menu it opens from.
  static constexpr float kOverlapMenu = 6.0f;
  // A reader, as a line of the submenu: pressed, their card, as a name
  // pressed anywhere opens it.
  struct reader_row : nodes::Stack {
    Actions* actions = nullptr;
    std::string id;
    struct lines_t : nodes::Stack {
      struct parts_t {
        nodes::Text name;
        nodes::Text when;
      } parts;
      explicit lines_t(const seen_reader& one)
          : parts{.name = nodes::Text(one.name, 13.0f, text_colour),
                  .when = nodes::Text(one.at ? clock_of(*one.at) : std::string("seen"), 12.0f, dim_colour)} {
        fState.apply({.autoSize = scene::axes::kY, .grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
        parts.name.setElided(true);
        parts.when.setElided(true);
      }
    };
    struct parts_t {
      avatar_mark face;
      lines_t lines;
    } parts;
    reader_row(Actions* a, const seen_reader& one)
        : actions(a), id(one.id), parts{.face = avatar_mark(one.id, one.name, 30.0f), .lines = lines_t(one)} {
      this->setHorizontal();
      this->setGap(14.0f);  // the name at 13 + 30 + 14 = 57
      // 6 over and under: the name at 13 and the time at 12 are 31.25 high,
      // and 7 left them 30 -- 1.25 out of the row.
      fState.apply({.fillX = true, .height = 44.0f, .padding = {6.0f, 17.0f, 6.0f, 13.0f}, .hoverBackground = chosen_colour});
    }
    [[nodiscard]] bool acceptsInput() const { return true; }
    [[nodiscard]] bool hoverChangesAppearance() const { return true; }
    [[nodiscard]] bool onClick(float, float) {
      actions->close_menu();
      actions->open_member_info(id);
      return true;
    }
  };
  // The readers, scrolling where there are more than fit: at most about
  // seven rows tall.
  struct submenu_t : nodes::Stack {
    static constexpr float kRow = 44.0f, kMostHeight = 320.0f, kWidth = 240.0f;
    [[nodiscard]] static float height_for(std::size_t readers) {
      return std::min(kMostHeight, kRow * static_cast<float>(readers) + 10.0f);
    }
    using rows_t = nodes::Flow<std::vector<reader_row>>;
    struct parts_t {
      nodes::ScrollContainer<rows_t> list{rows_t({.spacingY = 0.0f, .wrap = false}, {})};
    } parts;
    submenu_t(Actions* a, const std::vector<seen_reader>& readers) {
      auto& rows = std::get<0>(std::get<0>(parts.list.fChildren).fChildren);
      rows.reserve(readers.size());
      for (const seen_reader& one : readers)
        rows.emplace_back(a, one);
      std::get<0>(parts.list.fChildren).apply({.fillX = true, .autoSize = scene::axes::kY});
      parts.list.apply({.fill = true});
      const float tall = height_for(readers.size());
      fState.apply({.width = kWidth, .height = tall, .padding = {6.0f, 0.0f, 4.0f, 0.0f},
                    .cornerRadius = 10.0f, .background = popup_colour(), .border = scene::Border{band_colour, 1.0f},
                    .masking = true});
    }
  };
  Actions* actions = nullptr;
  std::vector<seen_reader> readers;
  // The window, as the menu fills it: the submenu kept inside it.
  const skia::SkRect* window = nullptr;
  // The submenu, held by the menu's layer over the whole window -- not by
  // this row, outside whose box and its card's it lies: what is repainted
  // or pointed at there went by the card's box, and missed it. And that
  // layer, laid out again as it opens or goes.
  std::optional<submenu_t>* submenu = nullptr;
  scene::Node* layer = nullptr;
  struct parts_t {
    icon_mark mark;
    nodes::Text label;
    std::vector<avatar_mark> faces;
  } parts;
  seen_row(Actions* a, std::vector<seen_reader> who)
      : actions(a), readers(std::move(who)),
        parts{.mark = icon_mark(icon::check{}),
              .label = nodes::Text(readers.empty()       ? std::string("Nobody Viewed")
                                   : readers.size() == 1 ? readers.front().name
                                                         : std::to_string(readers.size()) + " Seen",
                                   13.0f, text_colour)} {
    auto& [mark, label, faces] = parts;
    this->setHorizontal();
    const std::size_t shown = std::min(kMostFaces, readers.size());
    const float faces_width = shown ? kFace + static_cast<float>(shown - 1) * (kFace - kOverlap) : 0.0f;
    // The text 44 in, 9 over and 7 under it; the faces' room kept at the right.
    fState.apply({.fillX = true,
                  .height = kHeight,
                  .padding = {9.0f, kRight + (shown ? faces_width + 8.0f : 0.0f), 7.0f, 44.0f},
                  .hoverBackground = chosen_colour});
    // The ticks 15 in, in the middle of the row's height: out of the flow,
    // back over the padding.
    mark.apply({.place = scene::anchor::kCentreLeft, .x = 15.0f - 44.0f});
    label.setElided(true);
    label.apply({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
    // The faces out of the flow, in the room at the right: the first the
    // rightmost, 17 in from the edge, each next 14 to its left.
    for (std::size_t i = 0; i < shown; ++i) {
      faces.emplace_back(readers[i].id, readers[i].name, kFace);
      faces.back().apply({.place = scene::anchor::kCentreRight,
                          .x = faces_width + 8.0f - static_cast<float>(i) * (kFace - kOverlap),
                          .border = scene::Border{sidebar_colour, 2.0f}});
    }
  }
  // The submenu while the row or the submenu is hovered, as tdesktop's opens
  // under the pointer: moving over to it no longer closes it. It overlaps the
  // menu's edge: a gap between them, where neither is hovered, closed it on
  // the way over.
  void update(double) {
    if (submenu == nullptr || layer == nullptr)
      return;
    const bool open = (fState.hovered() || (*submenu && (*submenu)->fState.hovered())) && !readers.empty();
    if (open == submenu->has_value())
      return;
    if (open) {
      submenu->emplace(actions, readers);
      // Beside the menu, over its edge -- left of it where the window has
      // no room on the right. Its top at the row's, or higher where it would
      // pass the window's bottom -- as Telegram's, kept on the screen -- but
      // never above its top. In the layer's box, the window's.
      const skia::SkRect row = fState.fBounds;
      const skia::SkRect box = window != nullptr && !window->isEmpty() ? *window : row;
      const float tall = submenu_t::height_for(readers.size());
      const float top = std::max(box.fTop + 8.0f, std::min(row.fTop, box.fBottom - 8.0f - tall));
      float left = row.fRight - kOverlapMenu;
      if (left + submenu_t::kWidth > box.fRight - 8.0f)
        left = std::max(box.fLeft + 8.0f, row.fLeft + kOverlapMenu - submenu_t::kWidth);
      (*submenu)->apply({.place = scene::anchor::kTopLeft, .x = left - box.fLeft, .y = top - box.fTop});
    } else {
      submenu->reset();
    }
    layer->invalidateLayout();
    layer->markDamaged();
  }
};

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
    using thread_row = row_item<ask<Actions, &Actions::menu_thread>>;
    using quote_reply_row = row_item<ask<Actions, &Actions::menu_quote_reply>>;
    using edit_row = row_item<ask<Actions, &Actions::menu_edit>>;
    using later_row = row_item<not_yet<Actions>>;
    using copy_row = row_item<ask<Actions, &Actions::menu_copy>>;
    using link_row = row_item<ask<Actions, &Actions::menu_copy_link>>;
    using url_row = row_item<ask<Actions, &Actions::menu_copy_url>>;
    using fave_row = row_item<ask<Actions, &Actions::menu_fave_sticker>>;
    using copy_image_row = row_item<ask<Actions, &Actions::menu_copy_image>>;
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
      // Reply in thread, as Element's menu has it: a Matrix room's.
      thread_row thread_reply;
      // What is selected of another's message, quoted in an answer.
      quote_reply_row quote_reply;
      edit_row edit;
      pin_row pin;
      copy_row copy;
      link_row copy_link;
      // The link pressed on, in the text or its preview.
      url_row copy_url;
      // A sticker: made a favourite, or no longer one.
      fave_row fave;
      copy_image_row copy_image;
      save_row save;
      gif_row save_gif;
      reactions_row reactions;
      forward_row forward;
      source_row source;
      delete_row remove;
      nodes::Box<> seen_band{band_colour};
      seen_row<Actions> seen;
      // Every emoji, once asked for: over the items, out of their flow, and
      // after them, so drawn on top of them and pressed first.
      std::optional<emoji_panel<react_with<Actions>>> emoji;
    } parts;
    void expand() {
      auto& [quick, quick_band, reply, thread_reply, quote_reply, edit, pin, copy, copy_link, copy_url, fave, copy_image, save, save_gif, reactions, forward, source,
             remove, seen_band, seen, emoji] = parts;
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
      auto& [quick, quick_band, reply, thread_reply, quote_reply, edit, pin, copy, copy_link, copy_url, fave, copy_image, save, save_gif, reactions, forward, source, remove,
             seen_band, seen, emoji] = parts;
      for (scene::Node* item : std::initializer_list<scene::Node*>{&reply, &thread_reply, &quote_reply, &edit, &pin, &copy, &copy_link, &copy_url, &fave, &copy_image, &save,
                                                                   &save_gif, &reactions, &forward, &source,
                                                                   &remove, &seen_band, &seen})
        item->setVisible(false);
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
                .thread_reply = thread_row("Reply in thread", {a}, icon::threads{}),
                .quote_reply = quote_reply_row("Quote & Reply", {a}, icon::back{}),
                .edit = edit_row("Edit", {a}, icon::sliders{}),
                .pin = pin_row(facts.pinned ? "Unpin" : "Pin", {a}, icon::check{}),
                .copy = copy_row(facts.selection ? "Copy Selected Text" : "Copy Text", {a}, icon::clip{}),
                .copy_link = link_row("Copy Message Link", {a}, icon::info{}),
                .copy_url = url_row("Copy Link", {a}, icon::clip{}),
                .fave = fave_row(facts.sticker && is_favourite(facts.sticker->url) ? "Remove from Favourites" : "Add to Favourites",
                                 {a}, icon::check{}),
                .copy_image = copy_image_row("Copy Image", {a}, icon::clip{}),
                .save = save_row("Save As…", {a}, icon::send{}),
                .save_gif = gif_row("Save GIF", {a}, icon::check{}),
                .reactions = reactions_row(facts.reaction_count == 1 ? std::string("1 reaction")
                                                                     : std::format("{} reactions", facts.reaction_count),
                                           {a}, icon::people{}),
                .forward = forward_row("Forward", {a}, icon::send{}),
                .source = source_row("View Source", {a}, icon::info{}),
                .remove = delete_row("Delete", {a}, icon::close{}),
                .seen = seen_row<Actions>(a, facts.seen)} {
      fState.setFloats(true);  // over the chat: frosted live, where asked
      auto& [quick, quick_band, reply, thread_reply, quote_reply, edit, pin, copy, copy_link, copy_url, fave, copy_image, save, save_gif, reactions, forward, source,
             remove, seen_band, seen, emoji] = parts;
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
      compact(quote_reply);
      compact(edit);
      compact(pin);
      compact(thread_reply);
      compact(copy);
      compact(copy_link);
      compact(copy_url);
      compact(fave);
      compact(copy_image);
      compact(save);
      compact(save_gif);
      compact(reactions);
      compact(forward);
      compact(source);
      compact(remove);
      // One's own text, or one's own picture's caption, as Element edits it.
      // A reaction is neither edited nor reacted to: Matrix changes none.
      edit.setVisible(facts.editable && !facts.reaction && ((!facts.text.empty() && !facts.media) || facts.captioned));
      // Reactions where the account sends them.
      quick.setVisible(!facts.reaction && facts.can.react);
      quick_band.setVisible(!facts.reaction && facts.can.react);
      forward.setVisible(facts.can.forward);
      quote_reply.setVisible(facts.selection && !facts.own && !facts.copied.empty());
      copy.setVisible(!facts.copied.empty());
      copy_link.setVisible(!facts.link.empty());
      copy_url.setVisible(!facts.pressed_link.empty());
      fave.setVisible(facts.sticker.has_value());
      copy_image.setVisible(facts.picture.has_value());
      save.setVisible(facts.media.has_value());
      save_gif.setVisible(facts.media.has_value() && facts.moving);
      remove.setVisible(facts.deletable);
      pin.setVisible(facts.pinnable);
      // A message the server named, where the account has threads, and its
      // source: what was offered by being pinnable, a Matrix room's.
      thread_reply.setVisible(facts.can.threads && !facts.link.empty());
      source.setVisible(facts.can.view_source && !facts.link.empty());
      // Who reacted, as Telegram's menu lists them: wherever there are any.
      reactions.setVisible(facts.reaction_count > 0);
      seen_band.apply({.fillX = true, .height = 1.0f, .margin = {4.0f, 0.0f, 4.0f, 0.0f}});
      // As wide as its widest -- the quick reactions -- and no narrower than a
      // menu reads well at; the items fill that width.
      fState.apply({.autoSize = scene::axes::kBoth, .minWidth = 220.0f, .padding = {6.0f, 0.0f, 6.0f, 0.0f}, .cornerRadius = 10.0f, .background = popup_colour(), .border = scene::Border{band_colour, 1.0f},
                    .shadow = scene::Shadow{skia::colorSetARGB(70, 0, 0, 0), 3.0f}});
    }
  };
  struct parts_t {
    card menu;
    // Who has seen it, listed beside the menu: over the whole window, as the
    // menu is.
    std::optional<typename seen_row<Actions>::submenu_t> seen_list;
  } parts;
  Actions* actions = nullptr;

  // Where it was asked for: the pointer.
  float at_x = 0.0f, at_y = 0.0f;
  explicit context_menu(Actions* a, const menu_facts& facts)
      : parts{.menu = card(a, facts)}, actions(a), at_x(facts.x), at_y(facts.y) {
    fState.apply({.fill = true});
    parts.menu.parts.seen.window = &fState.fBounds;
    parts.menu.parts.seen.submenu = &parts.seen_list;
    parts.menu.parts.seen.layer = this;
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
