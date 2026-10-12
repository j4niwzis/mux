// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.stickers.ui -- The stickers' page of the media panel, as
// tdesktop's: a search over the packs -- recent, favourites, then the chat's
// and one's own, each a tab -- and a press sends one. And a sticker made a
// favourite, or no longer one, from its menu.
export module mux.plugin.stickers.ui;

import std;
import skia;
import skiff.scene;
import skiff.compose;
import skiff.nodes.flow;
import skiff.nodes.image;
import skiff.nodes.scroll;
import skiff.nodes.text;
import skiff.widgets.textbox;
import mux.core;
import mux.logic.text;
import mux.ui;
import mux.plugin.stickers.types;

export namespace mux::plugins::stickers {
// What the media panel's pages are made of: the window's own.
using namespace ::mux::ui;

template <class Actions> struct grid : skiff::compose::Stacked {
  // Child references and handlers require a fixed address.
  grid(const grid&) = delete;
  grid& operator=(const grid&) = delete;
  grid(grid&&) = delete;
  grid& operator=(grid&&) = delete;

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
  using tab = decltype(picker_tab(std::declval<const palette&>(), std::declval<grid*>(), std::size_t{}, std::optional<std::string>{}));
  static section make_section(const palette& colours, emoji_kept& kept, std::string name, const std::vector<emote>& stickers) {
    return cell_section(colours, name,
        std::ranges::to<std::vector>(std::views::transform(stickers, [&](const emote& one) { return cell(colours, kept, one); })),
        source_pack(stickers, name), kept.pack_account);
  }
  struct searched {
    grid* list;
    void operator()(std::string_view text) const { list->search(text); }
  };
  using footer_row = decltype(picker_footer<tab>());
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
    footer_row footer{picker_footer<tab>()};
    // Over the rest: the sticker the mouse rests on, large.
    std::optional<emote_preview_t> preview;
  } parts;
  std::optional<previewed> preview_of;
  bool searching = false;
  // The pictures of the packs' tabs, as show_all made them.
  std::vector<std::string> tab_pictures;
  [[nodiscard]] bool settling() const { return kept_->previewed_now != preview_of; }

  grid(const palette &colours, emoji_kept &kept, scene::Spec spec = {})
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
    auto& tabs = picker_tabs(parts.footer);
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
    std::get<0>(parts.footer.fChildren).invalidateLayout();
    parts.footer.scrollToStart();
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
  // The same, as the window asks every page of the media panel.
  [[nodiscard]] std::vector<emote_picture> emote_pictures_shown() {
    return std::ranges::to<std::vector>(std::views::transform(pictures_shown(), [](std::string url) {
      return emote_picture{std::move(url), emote_kind::sticker};
    }));
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
    for (auto&& [index, each] : std::views::enumerate(picker_tabs(parts.footer)))
      light_picker_tab(parts.footer, each, !searching && static_cast<std::size_t>(index) == lit);
  }
};

// Its page, in the media panel (mux.ui:emoji_popup): built where it is kept,
// as it points at its own tabs.
template <class Actions>
auto extend(contract::media_pages, type_tag<feature>, const ui_needs<Actions>& needs, const shown&) {
  return composition::in_place<grid<Actions>, const palette&, emoji_kept&>{{*needs.colours, *needs.emoji}};
}

// A sticker's menu: made a favourite, or no longer one.
template <class Actions>
auto extend(contract::message_menu, type_tag<feature>, type_tag<Actions>, const palette& colours, const emoji_kept& kept,
            const menu_facts& facts) {
  using row = row_item<sends<favourite>>;
  return skiff::compose::visible(
      facts.sticker.has_value(),
      row(colours, facts.sticker && kept.is_favourite(facts.sticker->url) ? "Remove from Favourites" : "Add to Favourites",
          sends<favourite>{favourite{facts.sticker.value_or(emote{})}}, icon::check{}, std::nullopt, menu_row_look));
}

}  // namespace mux::plugins::stickers
