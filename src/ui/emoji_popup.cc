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
  Answer operator()(const std::string& text, const std::string& key) const {
    return ::mux::ui::request::menu_react{key, key == text ? std::nullopt : std::optional(text)};
  }
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
inline auto gif_grid(const palette& colours, const std::vector<std::string>& paths) {
  namespace c = skiff::compose;
  auto cells = std::ranges::to<std::vector>(std::views::transform(paths, [&](const auto& path) {
    return gif_cell(colours, path);
  }));
  return c::column(c::vbox(0.0f, {.padding = {4.0f, 4.0f, 4.0f, 4.0f}}),
      c::visible(paths.empty(), c::styled({.fillX = true, .margin = {12.0f, 12.0f, 0.0f, 12.0f}},
          wrapped(nodes::Text("No saved GIFs yet. Save one from a GIF's menu.", 13.0f, colours.dim)))),
      c::styled({.fillX = true, .grow = scene::axes::kY}, nodes::ScrollContainer(
          c::styled({.fillX = true, .autoSize = scene::axes::kY},
              nodes::Flow<std::vector<decltype(gif_cell(colours, std::string()))>>(
                  {.direction = nodes::direction::horizontal{}, .wrap = true}, std::move(cells))))));
}
using gif_grid_t = decltype(gif_grid(std::declval<const palette&>(), std::declval<const std::vector<std::string>&>()));

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
  popup_page_t page = popup_page::emoji{};
  bool stickers = false;
  bool files = false;
};
inline bool page_available(const emoji_facts& facts, const popup_page_t& page) {
  return spl::visit(spl::overloaded{
      [](popup_page::emoji) { return true; },
      [&](popup_page::stickers) { return facts.stickers; },
      [&](popup_page::gifs) { return facts.files; }}, page);
}
inline auto popup_tab(const palette& colours, popup_page_t page, std::string name, bool selected, bool available) {
  namespace c = skiff::compose;
  return c::visible(available, c::onClick(request::select_emoji_page{std::move(page)},
      c::row(c::justified(c::hbox(0.0f, {.width = 80.0f, .height = 28.0f, .cornerRadius = 6.0f,
          .hoverBackground = colours.chosen, .selectedBackground = colours.tile, .selected = selected}), nodes::justify::middle{}),
          c::styled({.alignSelf = scene::align::kMiddle}, nodes::Text(name, 13.0f, colours.text, true))), name));
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
    using Answer = ::mux::ui::request::select_emoji_page;
    using panel_t = emoji_panel<insert_emoji_into<Actions>>;
    using tab_t = decltype(popup_tab(std::declval<const palette&>(), popup_page_t{popup_page::emoji{}}, std::string{}, false, true));
    using tabs_t = decltype(skiff::compose::row(skiff::compose::hbox(),
        std::declval<tab_t>(), std::declval<tab_t>(), std::declval<tab_t>()));
    // The colours it is made in: its tabs read them from it.
    const palette* colours_ = nullptr;
    emoji_facts allowed_;
    struct parts_t {
      tabs_t tabs;
      panel_t panel;
      sticker_grid<Actions> stickers;
      gif_grid_t gifs;
    } parts;
    card_t(const palette &colours, emoji_kept &kept, const emoji_facts& facts)
        : Stacked(skiff::compose::vbox(
              0.0f, {.width = 345.0f,
                     .height = 360.0f,
                     .cornerRadius = 8.0f,
                     .background = colours.sidebar,
                     .border = scene::Border{colours.band, 1.0f},
                     .shadow = scene::Shadow{skia::colorSetARGB(70, 0, 0, 0),
                                             3.0f}})),
          colours_(&colours), allowed_(facts),
          parts{.tabs = skiff::compose::row(skiff::compose::hbox(4.0f, {.fillX = true, .height = 36.0f, .depth = 1.0f,
                    .padding = {4.0f, 8.0f, 4.0f, 8.0f}}),
                    popup_tab(colours, popup_page::emoji{}, "Emoji", facts.page == popup_page_t(popup_page::emoji{}), true),
                    popup_tab(colours, popup_page::stickers{}, "Stickers", facts.page == popup_page_t(popup_page::stickers{}), facts.stickers),
                    popup_tab(colours, popup_page::gifs{}, "GIFs", facts.page == popup_page_t(popup_page::gifs{}), facts.files)),
                .panel = panel_t(colours, kept, insert_emoji_into<Actions>{},
                    {.fillX = true, .grow = scene::axes::kY, .masking = true,
                     .visible = facts.page == popup_page_t(popup_page::emoji{})}),
                .stickers = sticker_grid<Actions>(colours, kept,
                    {.fillX = true, .grow = scene::axes::kY, .masking = true,
                     .visible = facts.stickers && facts.page == popup_page_t(popup_page::stickers{})}),
                .gifs = skiff::compose::visible(facts.files && facts.page == popup_page_t(popup_page::gifs{}),
                    skiff::compose::styled({.fillX = true, .grow = scene::axes::kY, .masking = true}, gif_grid(colours, facts.gifs)))} {

      // The card paints its own floating backdrop. Its full-window layer
      // has no fill; treating this as an inner panel drops the plate when
      // live blur is enabled.
      fState.setFloats(true);
      page_at = static_cast<int>(facts.page.index());
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
    std::optional<::mux::ui::request::select_emoji_page> onPointer(scene::phase::capture, const scene::pointer::up& lift, scene::PointerReply& reply) {
      const std::optional<skia::SkPoint> from = std::exchange(swipe_from, std::nullopt);
      if (!from)
        return std::nullopt;
      const float dx = lift.x - from->fX;
      const float dy = lift.y - from->fY;
      if (std::abs(dx) < 90.0f || std::abs(dy) > std::abs(dx) * 0.5f)
        return std::nullopt;
      static const std::array<popup_page_t, 3> kPages{popup_page::emoji{}, popup_page::stickers{}, popup_page::gifs{}};
      const auto pages = std::ranges::to<std::vector>(std::views::filter(kPages, [&](const auto& page) {
        return page_available(allowed_, page);
      }));
      const auto current = std::ranges::find(pages, allowed_.page);
      const int at = static_cast<int>(current - pages.begin());
      const int to = std::clamp(at + (dx < 0.0f ? 1 : -1), 0, static_cast<int>(pages.size()) - 1);
      if (to == at)
        return std::nullopt;
      reply.handle();
      return request::select_emoji_page{pages[static_cast<std::size_t>(to)]};
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

  emoji_popup(const ui_needs<Actions>& n, const emoji_facts& facts)
      : Specced({.fill = true}), parts{.card = card_t(*n.colours, *n.emoji, facts)},
        right(facts.right), bottom(facts.bottom), shared_(n.shared) {}
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
