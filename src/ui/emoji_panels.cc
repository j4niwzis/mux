// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:emoji_panels -- The sticker grid and the emoji panel.
export module mux.ui:emoji_panels;

import std;
import mux.logic.text;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.compose;
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
import :emoji_kept;

export namespace mux::ui {
// The chat's stickers, as tdesktop's tab: a search at its top; the packs one
// under another in one list that scrolls -- Recent first -- each its name
// over a grid of its stickers; and a footer of the packs' pictures that
// brings each into view, lit for the one in view. A press sends one.
// A picker's layout, as tdesktop's emoji and sticker panels share it: the
// search field at the top, the list scrolling under it, the tabs' footer at
// the bottom.
[[nodiscard]] inline skiff::compose::Look picker_look() {
  return skiff::compose::vbox(4.0f, {.padding = {7.0f, 0.0f, 4.0f, 7.0f}});
}
[[nodiscard]] inline skiff::compose::Look picker_footer() {
  return skiff::compose::hbox(4.0f, {.fillX = true, .height = 36.0f});
}
template <class Field> [[nodiscard]] Field picker_field(Field field) {
  field.setSearchIcon(true);
  return skiff::compose::styled(
      {.fillX = true, .height = 32.0f, .margin = {0.0f, 7.0f, 0.0f, 0.0f}},
      std::move(field));
}
template <class Rows> [[nodiscard]] auto picker_list(Rows rows) {
  return skiff::compose::styled(
      {.fillX = true, .grow = scene::axes::kY},
      nodes::ScrollContainer<Rows>(skiff::compose::styled(
          {.fillX = true, .autoSize = scene::axes::kY}, std::move(rows))));
}

// Group by source and state key, not the displayed title. Old cached and
// protocol-neutral packs without a source retain their named grouping.
inline auto emote_pack_identity(const emote& one) {
  return std::tuple{one.pack_room, one.pack_key, one.pack_room || !one.pack_key.empty() ? std::string() : one.pack};
}
inline auto grouped_emotes(const std::vector<emote>& all, std::string fallback) {
  return std::ranges::to<std::vector>(std::views::transform(
      std::views::filter(std::views::iota(std::size_t{0}, all.size()), [&](std::size_t i) {
        return std::ranges::find(all, emote_pack_identity(all[i]), emote_pack_identity) == all.begin() + static_cast<std::ptrdiff_t>(i);
      }), [&](std::size_t i) {
        return std::pair{all[i].pack.empty() ? fallback : all[i].pack,
            std::ranges::to<std::vector>(std::views::filter(all, [&](const auto& one) { return emote_pack_identity(one) == emote_pack_identity(all[i]); }))};
      }));
}
inline std::optional<emote_pack> source_pack(const std::vector<emote>& images, std::string name) {
  if (images.empty() || !images.front().pack_room || !std::ranges::all_of(images, [&](const auto& one) {
        return emote_pack_identity(one) == emote_pack_identity(images.front());
      }))
    return std::nullopt;
  return emote_pack{.chat = images.front().pack_room, .key = images.front().pack_key, .name = std::move(name)};
}
template <class Panel>
struct picker_jump {
  Panel* panel;
  std::size_t at;
  scene::Taken operator()() const { panel->bring(at); return {}; }
};
template <class Panel>
auto picker_tab(const palette& colours, Panel* panel, std::size_t at, std::optional<std::string> picture, std::string mark = "⏲", emote_kind kind = emote_kind::emoji) {
  namespace c = skiff::compose;
  std::optional<nodes::Image<from_emotes>> image;
  if (picture) {
    image.emplace(from_emotes{*picture, kind});
    image->apply({.width = 24.0f, .height = 24.0f, .alignSelf = scene::align::kMiddle});
    image->keepBox();
  }
  return c::onPress(picker_jump<Panel>{panel, at},
      c::row(c::justified(c::hbox(0.0f, {.width = 30.0f, .height = 30.0f, .shrink = scene::axes::kX,
          .minWidth = 24.0f, .alignSelf = scene::align::kMiddle, .cornerRadius = 6.0f,
          .hoverBackground = colours.chosen, .selectedBackground = colours.tile}), nodes::justify::middle{}),
          std::move(image), c::visible(!picture, c::styled({.alignSelf = scene::align::kMiddle},
              nodes::Text(std::move(mark), 16.0f, colours.text)))), "Show emoji group");
}

template <class Actions> struct sticker_grid : skiff::compose::Stacked {
  // Child references and handlers require a fixed address.
  sticker_grid(const sticker_grid&) = delete;
  sticker_grid& operator=(const sticker_grid&) = delete;
  sticker_grid(sticker_grid&&) = delete;
  sticker_grid& operator=(sticker_grid&&) = delete;

  static constexpr float kCell = 78.0f;
  struct cell : skiff::compose::Stacked {
    // What its handlers ask for, returned.
    using Answer = ::mux::ui::request::send_sticker;
    emoji_kept* kept_ = nullptr;
    emote sticker;
    struct parts_t {
      nodes::Image<from_emotes> picture;
    } parts;
    cell(const palette &colours, emoji_kept &kept, emote one)
        : Stacked(
              skiff::compose::vbox(0.0f, {.width = kCell,
                                          .height = kCell,
                                          .margin = {2.0f, 2.0f, 2.0f, 2.0f},
                                          .padding = {4.0f, 4.0f, 4.0f, 4.0f},
                                          .cornerRadius = 6.0f,
                                          .hoverBackground = colours.chosen})),
          kept_(&kept), sticker(one),
          parts{.picture = skiff::compose::styled(
                    {.fill = true}, nodes::Image<from_emotes>({one.url, emote_kind::sticker}))} {

      parts.picture.keepBox();  // the cell's size, whatever the sticker
    }
    dwell resting;
    [[nodiscard]] bool settling() const { return resting.counting(this->hovered()); }
    void update(double now) { resting.step(this->hovered(), now, {sticker.url, ":" + sticker.shortcode + ":", true, emote_kind::sticker}, *kept_); }
    [[nodiscard]] bool acceptsInput() const { return true; }
    [[nodiscard]] bool hoverChangesAppearance() const { return true; }
    std::optional<Answer> onClick(float, float) {
      return ::mux::ui::request::send_sticker{sticker};
    }
  };
  // A pack: its name over its stickers.
  using section = cell_section_t<cell>;
  using tab = decltype(picker_tab(std::declval<const palette&>(), std::declval<sticker_grid*>(), std::size_t{}, std::optional<std::string>{}));
  static section make_section(const palette& colours, emoji_kept& kept, std::string name, const std::vector<emote>& stickers) {
    return cell_section(colours, name,
        std::ranges::to<std::vector>(std::views::transform(stickers, [&](const emote& one) { return cell(colours, kept, one); })),
        source_pack(stickers, name), kept.pack_account);
  }
  struct searched {
    sticker_grid* grid;
    void operator()(std::string_view text) const { grid->search(text); }
  };
  using footer_row = tab_strip_t<tab>;
  using field_t = widgets::TextBox<searched>;
  using list_t = nodes::ScrollContainer<nodes::Flow<std::vector<section>>>;
  // The colours it is made in, for what it makes later.
  const palette* colours_ = nullptr;
  // The stickers kept: the program's.
  emoji_kept* kept_ = nullptr;
  struct parts_t {
    field_t field;
    nodes::Text empty;
    list_t list{picker_list(nodes::Flow<std::vector<section>>(
        {.spacingY = 0.0f, .wrap = false}, {}))};
    footer_row footer{tab_strip<tab>(picker_footer())};
    // Over the rest: the sticker the mouse rests on, large.
    std::optional<emote_preview_t> preview;
  } parts;
  std::optional<previewed> preview_of;
  bool searching = false;
  // The pictures of the packs' tabs, as show_all made them.
  std::vector<std::string> tab_pictures;
  [[nodiscard]] bool settling() const { return kept_->previewed_now != preview_of; }

  sticker_grid(const palette &colours, emoji_kept &kept, scene::Spec spec = {})
      : Stacked(picker_look(), spec), colours_(&colours), kept_(&kept),
        parts{.field = picker_field(
                  field_t(colours.widgets, "Search stickers", {this})),
              .empty = skiff::compose::styled(
                  {.fillX = true, .margin = {12.0f, 12.0f, 0.0f, 12.0f}},
                  wrapped(nodes::Text("No stickers here. A room's sticker "
                                      "packs, and yours, show here.",
                                      13.0f, colours.dim)))} {
    auto& [field, empty, list, footer, preview] = parts;
    // Wrapped at the panel's width, not one line running past its edges.

    this->show_all();
  }
  [[nodiscard]] std::vector<section>& sections() { return std::get<0>(std::get<0>(parts.list.fChildren).fChildren); }
  // The packs as the chat has them, in the order they first come; the
  // unnamed together, as "Stickers".
  [[nodiscard]] auto packs() const { return grouped_emotes(kept_->chat_stickers, "Stickers"); }
  // Recent, then every pack, one under another; a tab for each.
  void show_all() {
    auto& all = this->sections();
    all.clear();
    auto& tabs = parts.footer.fParts;
    tabs.clear();
    tab_pictures.clear();
    // Recent: those sent lately that the chat still has.
    std::vector<emote> recent = std::ranges::to<std::vector>(std::views::filter(kept_->recent_stickers, [this](const emote& one) {
                                  return std::ranges::contains(kept_->chat_stickers, one.url, &emote::url);
                                }));
    if (!recent.empty()) {
      all.push_back(make_section(*colours_, *kept_, "Recently used", recent));
      tabs.push_back(picker_tab(*colours_, this, all.size() - 1, std::nullopt, "\u23F2"));
    }
    // The favourites: whichever chat they came from -- a sticker is its
    // picture's URL, sent anywhere.
    if (!kept_->favourite_stickers.empty()) {
      all.push_back(make_section(*colours_, *kept_, "Favourites", kept_->favourite_stickers));
      tabs.push_back(picker_tab(*colours_, this, all.size() - 1, std::nullopt, "\u2605"));
    }
    for (auto& [name, stickers] : packs()) {
      const std::optional<std::string> picture = stickers.front().pack_avatar ? stickers.front().pack_avatar
                                                                              : std::optional<std::string>(stickers.front().url);
      all.push_back(make_section(*colours_, *kept_, name, stickers));
      tabs.push_back(picker_tab(*colours_, this, all.size() - 1, picture, "⏲", emote_kind::sticker));
      if (picture)
        tab_pictures.push_back(*picture);
    }
    searching = false;
    parts.empty.setVisible(kept_->chat_stickers.empty() && kept_->favourite_stickers.empty());
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
    constexpr auto lower = mux::logic::folded;
    const std::string wanted = lower(query);
    const std::vector<emote> found = std::ranges::to<std::vector>(std::views::filter(kept_->chat_stickers, [&](const emote& one) {
                                       return lower(one.shortcode).contains(wanted) || lower(one.body).contains(wanted) ||
                                              lower(one.pack).contains(wanted);
                                     }));
    auto& all = this->sections();
    all.clear();
    std::ranges::for_each(grouped_emotes(found, "Search results"), [&](const auto& pack) {
      all.push_back(make_section(*colours_, *kept_, pack.first, pack.second));
    });
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
        std::ranges::to<std::vector>(std::views::transform(std::views::filter(std::views::join(std::views::transform(this->sections(), [](section& one) -> std::vector<cell>& { return cell_section_cells(one); })), [&](cell& one) { return !one.bounds().isEmpty() && parts.list.toView(one.bounds()).intersects(view); }), [](cell& one) { return one.sticker.url; }));
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
    if (follow_preview(parts.preview, preview_of, *colours_, *kept_))
      this->invalidateLayout();
    auto& all = this->sections();
    std::size_t lit = 0;
    const float top = parts.list.bounds().fTop + 1.0f;
    for (std::size_t s = 0; s < all.size(); ++s)
      if (!all[s].bounds().isEmpty() && parts.list.toView(all[s].bounds()).fTop <= top)
        lit = s;
    for (auto&& [index, each] : std::views::enumerate(parts.footer.fParts))
      if (const bool on = !searching && static_cast<std::size_t>(index) == lit; on != each.fState.selected())
        each.fState.apply({.selected = on});
  }
};

// Every emoji, as tdesktop's panel lists them (chat_helpers.style): a search
// at its top; the groups one under another in one list that scrolls, each a
// semibold header over its emoji, 37 across (desiredSize); and a footer of
// the groups' tabs, 36 high, that brings each into view and is lit for the
// one in view. A press on an emoji gives it to Pick: a reaction, from a
// message's menu; text in the input, from the input's own button.
template <class Pick> struct emoji_panel : skiff::compose::Stacked {
  // Child references and handlers require a fixed address.
  emoji_panel(const emoji_panel&) = delete;
  emoji_panel& operator=(const emoji_panel&) = delete;
  emoji_panel(emoji_panel&&) = delete;
  emoji_panel& operator=(emoji_panel&&) = delete;

  Pick pick;
  static constexpr float kCell = 37.0f;
  // A reaction that is text -- Matrix takes any -- as SchildiChat offers one:
  // what is searched, itself, at the top of the results, where the panel
  // reacts rather than writes.
  struct text_chip : skiff::compose::Stacked {
    using Answer = typename Pick::Answer;
    emoji_panel* panel;
    std::string text;
    struct parts_t {
      nodes::Text label;
    } parts;
    explicit text_chip(emoji_panel *p)
        : Stacked(skiff::compose::hbox(
              0.0f, {.fillX = true,
                     .height = 34.0f,
                     .margin = {4.0f, 7.0f, 2.0f, 0.0f},
                     .padding = {0.0f, 12.0f, 0.0f, 12.0f},
                     .cornerRadius = 17.0f,
                     .background = p->colours_->tile,
                     .hoverBackground = p->colours_->chosen})),
          panel(p),
          parts{
              .label = skiff::compose::styled(
                  {.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle},
                  elided(nodes::Text("", 13.0f, p->colours_->text)))} {

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
    auto onClick(float, float) -> std::optional<typename Pick::Answer> {
      panel->tones_done = true;
      return panel->pick(text, text);
    }
  };
  // One emoji of the list.
  struct cell : skiff::compose::Stacked {
    using Answer = typename Pick::Answer;
    emoji_panel* panel;
    std::string glyph;
    // The emoji it shows, where it is one of the table's: its tones are
    // found through it.
    const alef::emoji* source = nullptr;
    // A custom emoji's picture on the server: what a reaction with it is,
    // while what goes into the text is its :shortcode:.
    std::string picture_url;
    struct parts_t {
      std::optional<nodes::Image<from_emotes>> picture;
      nodes::Text face;
    } parts;
    dwell resting;
    [[nodiscard]] bool settling() const { return resting.counting(this->hovered()); }
    void update(double now) {
      resting.step(this->hovered(), now,
                   picture_url.empty() ? previewed{glyph, std::string(), false} : previewed{picture_url, glyph, true},
                   *panel->kept_);
    }
    cell(emoji_panel *p, std::string g, const alef::emoji *from = nullptr)
        : Stacked(skiff::compose::justified(
              skiff::compose::hbox(0.0f,
                                   {.width = kCell,
                                    .height = kCell,
                                    .cornerRadius = 6.0f,
                                    .hoverBackground = p->colours_->chosen}),
              nodes::justify::middle{})),
          panel(p), glyph(g), source(from),
          parts{.face = skiff::compose::styled(
                    {.alignSelf = scene::align::kMiddle},
                    nodes::Text(std::move(g), 22.0f, p->colours_->text))} {}
    [[nodiscard]] bool acceptsInput() const { return true; }
    [[nodiscard]] bool hoverChangesAppearance() const { return true; }
    // A custom emoji: its picture in place of a glyph.
    cell(emoji_panel* p, const emote& custom) : cell(p, ":" + custom.shortcode + ":") {
      picture_url = custom.url;
      parts.face.setVisible(false);
      parts.picture.emplace(from_emotes{custom.url});
      parts.picture->apply({.width = 26.0f, .height = 26.0f, .alignSelf = scene::align::kMiddle});
      parts.picture->keepBox();  // fixed: its coming repaints, lays nothing out
    }
    auto onClick(float, float) -> std::optional<typename Pick::Answer> {
      const std::string chosen = glyph;
      const std::string key = picture_url.empty() ? glyph : picture_url;
      if (picture_url.empty())
        panel->kept_->remember_emoji(chosen);
      panel->tones_done = true;
      return panel->pick(chosen, key);
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
  using section = cell_section_t<cell>;
  using tab = decltype(picker_tab(std::declval<const palette&>(), std::declval<emoji_panel*>(), std::size_t{}, std::optional<std::string>{}));
  static cell make_cell(emoji_panel* p, const alef::emoji* one) { return cell(p, logic::emoji_text(*one), one); }
  static cell make_cell(emoji_panel* p, const emote& one) { return cell(p, one); }
  static cell make_cell(emoji_panel* p, const std::string& one) { return cell(p, one); }
  static section make_section(emoji_panel* p, std::string_view name, const auto& all) {
    return cell_section(*p->colours_, std::string(name),
        std::ranges::to<std::vector>(std::views::transform(all, [p](const auto& one) { return make_cell(p, one); })));
  }
  static section make_section(emoji_panel* p, std::string_view name, const std::vector<emote>& all) {
    return cell_section(*p->colours_, std::string(name),
        std::ranges::to<std::vector>(std::views::transform(all, [p](const auto& one) { return make_cell(p, one); })),
        source_pack(all, std::string(name)), p->kept_->pack_account);
  }
  // An emoji and its five tones in a row over it, as tdesktop's; a pick
  // closes it.
  struct tone_strip : skiff::compose::Stacked {
    struct parts_t {
      std::vector<cell> each;
    } parts;
    tone_strip(emoji_panel *p, const alef::emoji &base,
               const std::vector<const alef::emoji *> &tones, float x, float y)
        : Stacked(skiff::compose::hbox(0.0f, {})) {
      const float wide = kCell * static_cast<float>(tones.size() + 1) + 8.0f;
      fState.apply({.place = scene::anchor::kTopLeft, .x = x, .y = y, .width = wide, .height = kCell + 8.0f,
                    .padding = {4.0f, 4.0f, 4.0f, 4.0f}, .cornerRadius = 8.0f, .background = p->colours_->sidebar,
                    .border = scene::Border{p->colours_->band, 1.0f},
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
  using footer_row = tab_strip_t<tab>;
  using field_t = widgets::TextBox<searched>;
  using list_t = nodes::ScrollContainer<nodes::Flow<std::vector<section>>>;
  // The colours it is made in, for what it makes later: its cells, its
  // sections, its tones.
  const palette* colours_ = nullptr;
  // The emoji kept: the program's.
  emoji_kept* kept_ = nullptr;
  struct parts_t {
    field_t field;
    text_chip text_option;
    list_t list{picker_list(nodes::Flow<std::vector<section>>(
        {.spacingY = 0.0f, .wrap = false}, {}))};
    footer_row footer{tab_strip<tab>(picker_footer())};
    // Over the rest: an emoji's tones, while they are asked for.
    std::optional<tone_strip> tones;
    // And the emoji the mouse rests on, large.
    std::optional<emote_preview_t> preview;
  } parts;
  std::optional<previewed> preview_of;
  // A pick made: the tones, if open, closed at the next frame -- not now,
  // from inside one of their own cells.
  bool tones_done = false;
  // Whether a search is shown, not the groups: no tab is lit then.
  bool searching = false;

  // Sized by where it is shown.
  emoji_panel(const palette &colours, emoji_kept &kept, Pick what,
              scene::Spec spec = {})
      : Stacked(picker_look(), spec), pick(std::move(what)), colours_(&colours),
        kept_(&kept), parts{.field = picker_field(field_t(
                                colours.widgets, "Search emoji", {this})),
                            .text_option = text_chip(this)} {
    auto& [field, text_option, list, footer, tones, preview] = parts;
    this->show_all();
  }
  [[nodiscard]] std::vector<section>& sections() { return std::get<0>(std::get<0>(parts.list.fChildren).fChildren); }
  // Every group, one under another.
  void show_all() {
    auto& all = this->sections();
    all.clear();
    all.reserve(logic::emoji_group_count() + 1);
    auto& tabs = parts.footer.fParts;
    tabs.clear();
    {
      // tdesktop's: what was picked lately first, then its default list
      // (lib_ui's GetDefaultRecent), up to the section's number -- so the
      // section is full from the first run, as Telegram's.
      std::vector<std::string> shown = kept_->recent_emoji;
      for (const char* one : {"😂", "😘", "❤️", "😍", "😊", "😁", "👍", "☺️", "😔", "😄", "😭", "💋",
                              "😒", "😳", "😜", "🙈", "😉", "😃", "😢", "😝", "😱", "😡", "😏", "😞",
                              "😅", "😚", "🙊", "😌", "😀", "😋", "😆", "👌", "😐", "😕"})
        if (shown.size() < 42 && std::ranges::find(shown, std::string(one)) == shown.end())
          shown.emplace_back(one);
      all.push_back(make_section(this, "Recently used", shown));
      tabs.push_back(picker_tab(*colours_, this, all.size() - 1, std::nullopt));
    }
    std::ranges::for_each(grouped_emotes(kept_->chat_emotes, "Custom emoji"), [&](const auto& pack) {
      const auto& [name, images] = pack;
      all.push_back(make_section(this, name, images));
      tabs.push_back(picker_tab(*colours_, this, all.size() - 1, images.front().pack_avatar.value_or(images.front().url)));
    });
    for (std::size_t g = 0; g < logic::emoji_group_count(); ++g) {
      all.push_back(make_section(this, logic::emoji_group_name(g), logic::emoji_of_group(g)));
      tabs.push_back(picker_tab(*colours_, this, all.size() - 1, std::nullopt, logic::emoji_text(logic::emoji_group_face(g))));
    }
    parts.footer.invalidateLayout();
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
    all.push_back(make_section(this, "Search results", logic::emoji_found(query)));
    const std::string wanted = logic::folded(query);
    const auto custom = std::ranges::to<std::vector>(std::views::filter(kept_->chat_emotes, [&](const emote& one) {
      return logic::folded(one.shortcode).contains(wanted) || logic::folded(one.body).contains(wanted) || logic::folded(one.pack).contains(wanted);
    }));
    std::ranges::for_each(grouped_emotes(custom, "Custom emoji"), [&](const auto& pack) {
      all.push_back(make_section(this, pack.first, pack.second));
    });
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
    const std::size_t at = group;
    if (at >= all.size() || all[at].bounds().isEmpty())
      return;
    auto& list = parts.list;
    list.scrollTo(std::max(0.0f, list.current() + (list.toView(all[at].bounds()).fTop - list.bounds().fTop)));
  }
  [[nodiscard]] std::vector<std::string> pictures_shown() {
    if (!this->visible())
      return {};
    const auto view = parts.list.bounds().makeOutset(0.0f, kCell);
    auto cells = std::views::join(std::views::transform(this->sections(), [](section& one) -> std::vector<cell>& { return cell_section_cells(one); }));
    auto shown = std::ranges::to<std::vector>(std::views::transform(std::views::filter(cells, [&](const cell& one) {
      return !one.picture_url.empty() && !one.bounds().isEmpty() && parts.list.toView(one.bounds()).intersects(view);
    }), [](const cell& one) { return one.picture_url; }));
    // Pack tabs stay visible even when their section is outside the view.
    std::ranges::for_each(grouped_emotes(kept_->chat_emotes, ""), [&](const auto& pack) {
      shown.push_back(pack.second.front().pack_avatar.value_or(pack.second.front().url));
    });
    return shown;
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
  [[nodiscard]] bool settling() const { return kept_->previewed_now != preview_of; }
  // The tab of the group at the top of the list lit; the preview kept to
  // what the cells say.
  void update(double) {
    if (follow_preview(parts.preview, preview_of, *colours_, *kept_))
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
    for (std::size_t s = 0; s < all.size(); ++s)
      if (!all[s].bounds().isEmpty() && parts.list.toView(all[s].bounds()).fTop <= top)
        lit = s;
    for (auto&& [index, each] : std::views::enumerate(parts.footer.fParts))
      if (const bool on = !searching && static_cast<std::size_t>(index) == lit; on != each.fState.selected())
        each.fState.apply({.selected = on});
  }
};

}  // namespace mux::ui
