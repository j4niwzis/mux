// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:emoji_popup -- The emoji popup: its pages -- emoji, stickers, GIFs -- and what a pick does.
export module mux.ui:emoji_popup;

import std;
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
import :emoji_panels;

export namespace mux::ui {
// What the menu's emoji do: react with it.
template <class Actions>
struct react_with {
  using Answer = ::mux::ui::request::menu_react;
  // What is typed in its search, a reaction too: Matrix takes any text.
  [[nodiscard]] static constexpr bool takes_text() { return true; }
  Answer operator()(const std::string&, const std::string& key) const { return ::mux::ui::request::menu_react{key}; }
};
// What the input's emoji do: go into what is written.
template <class Actions>
struct insert_emoji_into {
  using Answer = ::mux::ui::request::insert_emoji;
  [[nodiscard]] static constexpr bool takes_text() { return false; }
  // A glyph as itself; a custom emoji (its key its picture's, not its
  // text) as its picture.
  Answer operator()(const std::string& text, const std::string& key) const {
    return ::mux::ui::request::insert_emoji{text, key == text ? std::string() : key};
  }
};

// The GIFs saved, as tdesktop's GIF tab shows them: a grid of them playing,
// newest first; a press sends one into the chat.
inline auto gif_cell(const palette& colours, std::string path) {
  const std::string key = "gif:" + path;
  auto image = nodes::Image<from_moving_whole>({key});
  image.keepBox();
  return skiff::compose::onClick(
      ::mux::ui::request::send_gif{std::move(path)},
      skiff::compose::row(
          skiff::compose::vbox(0.0f, {.width = 104.0f,
                                     .height = 104.0f,
                                     .margin = {2.0f, 2.0f, 2.0f, 2.0f},
                                     .cornerRadius = 6.0f,
                                     .background = colours.tile,
                                     .masking = true}),
          skiff::compose::styled({.fill = true, .cornerRadius = 6.0f}, std::move(image))));
}
template <class Actions> struct gif_grid : skiff::compose::Stacked {
  using cell_t = decltype(gif_cell(std::declval<const palette&>(), std::string{}));
  using cells_t = nodes::Flow<std::vector<cell_t>>;
  struct parts_t {
    nodes::Text empty;
    nodes::ScrollContainer<cells_t> list{
        cells_t({.direction = nodes::direction::horizontal{}, .spacingX = 0.0f, .spacingY = 0.0f, .wrap = true}, {})};
  } parts;
  // The colours its cells are made in, as they change.
  const palette* colours_ = nullptr;

  gif_grid(const palette &colours)
      : Stacked(
            skiff::compose::vbox(0.0f, {.padding = {4.0f, 4.0f, 4.0f, 4.0f}})),
        parts{.empty = skiff::compose::styled(
                  {.fillX = true, .margin = {12.0f, 12.0f, 0.0f, 12.0f}},
                  wrapped(nodes::Text(
                      "No saved GIFs yet. Save one from a GIF's menu.", 13.0f,
                      colours.dim)))},
        colours_(&colours) {
    auto& [empty, list] = parts;

    list.apply({.fillX = true, .grow = scene::axes::kY});
    std::get<0>(list.fChildren).apply({.fillX = true, .autoSize = scene::axes::kY});
  }
  // The saved ones, as the program lists them: newest first.
  void show(const std::vector<std::string>& paths) {
    auto& cells = std::get<0>(std::get<0>(parts.list.fChildren).fChildren);
    cells.clear();
    cells.reserve(paths.size());
    for (const std::string& one : paths)
      cells.push_back(gif_cell(*colours_, one));
    parts.empty.setVisible(paths.empty());
    parts.list.invalidateLayout();
    parts.list.scrollTo(0.0f);
  }
};

// The input's emoji, as tdesktop's panel: a card over the chat, 345 wide
// (emojiPanWidth), 278 to 640 high, rounded 8, its bottom right at the top
// right of the button that opened it; a press off it closes it. It stays
// open while emoji are picked, and the input keeps the keys.
// The input's emoji panel open: where it is put, its right edge and its
// bottom, in the window.
struct emoji_facts {
  float right = 0.0f;
  float bottom = 0.0f;
  // The GIFs saved, for its GIF tab, as the program has them.
  std::vector<std::string> gifs;
};
template <class Card> struct popup_tab_action {
  Card* card;
  popup_page_t page;
  std::variant<scene::Taken, request::show_gifs> operator()() const {
    if (auto asked = card->show(page))
      return *asked;
    return scene::Taken{};
  }
};
template <class Card>
auto popup_tab(const palette& colours, Card* card, popup_page_t page, std::string name) {
  namespace c = skiff::compose;
  return c::onPress(popup_tab_action<Card>{card, std::move(page)},
      c::row(c::justified(c::hbox(0.0f, {.width = 80.0f, .height = 28.0f, .cornerRadius = 6.0f,
          .hoverBackground = colours.chosen, .selectedBackground = colours.tile}), nodes::justify::middle{}),
          c::styled({.alignSelf = scene::align::kMiddle}, nodes::Text(name, 13.0f, colours.text, true))), name);
}

template <class Actions> struct emoji_popup : skiff::compose::Specced {
  // Child references and handlers require a fixed address.
  emoji_popup(const emoji_popup&) = delete;
  emoji_popup& operator=(const emoji_popup&) = delete;
  emoji_popup(emoji_popup&&) = delete;
  emoji_popup& operator=(emoji_popup&&) = delete;

  struct card_t : skiff::compose::Stacked {
    // Child references and handlers require a fixed address.
    card_t(const card_t&) = delete;
    card_t& operator=(const card_t&) = delete;
    card_t(card_t&&) = delete;
    card_t& operator=(card_t&&) = delete;

    // A swipe to the GIFs: what has been saved since, asked for.
    using Answer = ::mux::ui::request::show_gifs;
    using panel_t = emoji_panel<insert_emoji_into<Actions>>;
    using tab_t = decltype(popup_tab(std::declval<const palette&>(), std::declval<card_t*>(),
        popup_page_t{popup_page::emoji{}}, std::string{}));
    using tabs_t = decltype(skiff::compose::row(skiff::compose::hbox(),
        std::declval<tab_t>(), std::declval<tab_t>(), std::declval<tab_t>()));
    // The colours it is made in: its tabs read them from it.
    const palette* colours_ = nullptr;
    struct parts_t {
      tabs_t tabs;
      panel_t panel;
      sticker_grid<Actions> stickers;
      gif_grid<Actions> gifs;
    } parts;
    card_t(const palette &colours, emoji_kept &kept)
        : Stacked(skiff::compose::vbox(
              0.0f, {.width = 345.0f,
                     .height = 360.0f,
                     .cornerRadius = 8.0f,
                     .background = colours.sidebar,
                     .border = scene::Border{colours.band, 1.0f},
                     .shadow = scene::Shadow{skia::colorSetARGB(70, 0, 0, 0),
                                             3.0f}})),
          colours_(&colours),
          parts{.tabs = skiff::compose::row(skiff::compose::hbox(4.0f, {.fillX = true, .height = 36.0f, .depth = 1.0f,
                    .padding = {4.0f, 8.0f, 4.0f, 8.0f}}),
                    popup_tab(colours, this, popup_page::emoji{}, "Emoji"),
                    popup_tab(colours, this, popup_page::stickers{}, "Stickers"),
                    popup_tab(colours, this, popup_page::gifs{}, "GIFs")),
                .panel = panel_t(colours, kept, insert_emoji_into<Actions>{},
                                 {.fillX = true, .grow = scene::axes::kY, .masking = true}),
                .stickers = sticker_grid<Actions>(
                    colours, kept, {.fillX = true, .grow = scene::axes::kY, .masking = true}),
                .gifs = skiff::compose::styled(
                    {.fillX = true, .grow = scene::axes::kY, .masking = true},
                    gif_grid<Actions>(colours))} {

      // The card paints its own floating backdrop. Its full-window layer
      // has no fill; treating this as an inner panel drops the plate when
      // live blur is enabled.
      fState.setFloats(true);
      (void)this->show(popup_page::emoji{});
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
    std::optional<::mux::ui::request::show_gifs> onPointer(scene::phase::capture, const scene::pointer::up& lift, scene::PointerReply& reply) {
      const std::optional<skia::SkPoint> from = std::exchange(swipe_from, std::nullopt);
      if (!from)
        return std::nullopt;
      const float dx = lift.x - from->fX;
      const float dy = lift.y - from->fY;
      if (std::abs(dx) < 90.0f || std::abs(dy) > std::abs(dx) * 0.5f)
        return std::nullopt;
      static const std::array<popup_page_t, 3> kPages{popup_page::emoji{}, popup_page::stickers{}, popup_page::gifs{}};
      const int to = std::clamp(page_at + (dx < 0.0f ? 1 : -1), 0, 2);
      if (to == page_at)
        return std::nullopt;
      reply.handle();
      return this->show(kPages[static_cast<std::size_t>(to)]);
    }
    // One tab's page shown, the others hidden; the GIFs asked of the program
    // as their tab opens, for what was saved since.
    std::optional<::mux::ui::request::show_gifs> show(const popup_page_t& page) {
      const auto [emoji, stickers, gifs] =
          spl::visit(spl::overloaded{[](popup_page::emoji) { return std::array{true, false, false}; },
                                [](popup_page::stickers) { return std::array{false, true, false}; },
                                [](popup_page::gifs) { return std::array{false, false, true}; }},
                     page);
      parts.panel.setVisible(emoji);
      parts.stickers.setVisible(stickers);
      parts.gifs.setVisible(gifs);
      page_at = emoji ? 0 : stickers ? 1 : 2;
      std::get<0>(parts.tabs.fParts).fState.apply({.selected = emoji});
      std::get<1>(parts.tabs.fParts).fState.apply({.selected = stickers});
      std::get<2>(parts.tabs.fParts).fState.apply({.selected = gifs});
      this->invalidateLayout();
      if (gifs)
        return ::mux::ui::request::show_gifs{};
      return std::nullopt;
    }
    [[nodiscard]] bool acceptsInput() const { return true; }
  };
  struct parts_t {
    card_t card;
  } parts;
  // Where the button that opened it is: its right, its top.
  float right = 0.0f, bottom = 0.0f;
  float placed_x = -1.0f, placed_y = -1.0f, placed_h = -1.0f;
  float placed_w = 0.0f;
  // What the window's parts tell one another: the docked panel's height.
  ui_shared* shared_ = nullptr;

  emoji_popup(const ui_needs<Actions>& n, const emoji_facts& facts) : emoji_popup(n, facts.right, facts.bottom) { this->show_page(facts); }
  // What it shows changed while it is up: the GIFs saved.
  void show_page(const emoji_facts& facts) { parts.card.parts.gifs.show(facts.gifs); }
  emoji_popup(const ui_needs<Actions> &n, float at_right, float at_bottom)
      : Specced({.fill = true}), parts{.card = card_t(*n.colours, *n.emoji)},
        right(at_right), bottom(at_bottom), shared_(n.shared) {}
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
    shared_->set_docked_panel_height(phone ? h_at : 0.0f);
    if (x_at != placed_x || y != placed_y || h_at != placed_h || w_at != placed_w) {
      placed_x = x_at;
      placed_y = y;
      placed_h = h_at;
      placed_w = w_at;
      parts.card.apply({.x = x_at, .y = y, .width = w_at, .height = h_at});
    }
    scene::layoutChildrenInContentBox(*this);
  }
  // A press off it goes through to what is under it -- the chat scrolled
  // with the popup open, as tdesktop's panel lets it be; a tap off it, the
  // window's, closes it (window.cc).
  [[nodiscard]] bool acceptsInput() const { return false; }
};

}  // namespace mux::ui
