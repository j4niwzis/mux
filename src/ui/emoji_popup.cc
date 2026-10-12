// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:emoji_popup -- The emoji popup: its pages -- its emoji, and each a
// plugin gives it (contract::media_pages) -- and what a pick does.
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
import mux.plugins.types;
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

// The input's emoji, as tdesktop's panel: a card over the chat, 345 wide
// (emojiPanWidth), 278 to 640 high, rounded 8, its bottom right at the top
// right of the button that opened it; a press off it closes it. It stays
// open while emoji are picked, and the input keeps the keys.
// The input's emoji panel open: where it is put, its right edge and its
// bottom, in the window.
struct emoji_facts {
  float right = 0.0f;
  float bottom = 0.0f;
  popup_page_t page = popup_page::emoji{};
  bool stickers = false;  // the account sends stickers
  bool files = false;     // and files
  // What is shown on each plugin's page, as the program has it.
  pages_shown_t pages;
};
// Whether a page is offered: a plugin's says where (shown_where).
inline bool page_available(const emoji_facts& facts, const popup_page_t& page) {
  return spl::visit(spl::overloaded{
      [](popup_page::emoji) { return true; },
      [&](const auto& theirs) { return shown_where(theirs, facts); }}, page);
}
// The pages in their order: what a swipe goes through.
template <class... Features>
std::array<popup_page_t, 1 + sizeof...(Features)> all_pages(plugin_list<Features...>) {
  return {popup_page::emoji{}, typename decltype(page_type(contract::media_pages{}, type_tag<Features>{}))::type{}...};
}
// The emote pictures a page shows, for the program to fetch: where it says.
template <class Page>
  requires requires(Page& page) { page.emote_pictures_shown(); }
std::vector<emote_picture> pictures_of(std::optional<Page>& page) {
  return page ? page->emote_pictures_shown() : std::vector<emote_picture>{};
}
template <class Page>
std::vector<emote_picture> pictures_of(std::optional<Page>&) {
  return {};
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

  // The plugins' pages: each its tab, and its node from its extend(), made
  // where the window is instantiated, which imports them (mux.plugins.ui) --
  // each in a slot of its own, built there (composition::put): a page may
  // point at its own parts.
  template <class List>
  struct plugin_pages;
  template <class... Features>
  struct plugin_pages<plugin_list<Features...>> {
    template <class Feature>
    using page_t = typename decltype(page_type(contract::media_pages{}, type_tag<Feature>{}))::type;
    template <class Feature>
    using shown_t = typename decltype(shown_type(contract::media_pages{}, type_tag<Feature>{}))::type;
    template <class Feature>
    static auto made(const ui_needs<Actions>& needs, const emoji_facts& facts) {
      static_assert(requires { extend(contract::media_pages{}, type_tag<Feature>{}, needs, std::get<shown_t<Feature>>(facts.pages)); },
                    composition::broken<contract::media_pages, Feature>());
      return extend(contract::media_pages{}, type_tag<Feature>{}, needs, std::get<shown_t<Feature>>(facts.pages));
    }
    using nodes_t = std::tuple<std::optional<composition::node_t<decltype(made<Features>(
        std::declval<const ui_needs<Actions>&>(), std::declval<const emoji_facts&>()))>>...>;
    // Each built in its slot, shown where it is the page chosen.
    template <class Feature, class Slot>
    static void place(Slot& slot, const ui_needs<Actions>& needs, const emoji_facts& facts) {
      composition::put(slot, made<Feature>(needs, facts))
          .apply({.fillX = true, .grow = scene::axes::kY, .masking = true,
                  .visible = facts.page == popup_page_t(page_t<Feature>{}) && page_available(facts, page_t<Feature>{})});
    }
    static void build(nodes_t& slots, const ui_needs<Actions>& needs, const emoji_facts& facts) {
      [&]<std::size_t... At>(std::index_sequence<At...>) {
        (place<Features>(std::get<At>(slots), needs, facts), ...);
      }(std::index_sequence_for<Features...>{});
    }
    template <class Tab>
    static auto tabs(const palette& colours, const emoji_facts& facts, Tab emoji) {
      return skiff::compose::row(skiff::compose::hbox(4.0f, {.fillX = true, .height = 36.0f, .depth = 1.0f,
                                                              .padding = {4.0f, 8.0f, 4.0f, 8.0f}}),
          std::move(emoji),
          popup_tab(colours, page_t<Features>{}, std::string(tab_name(page_t<Features>{})),
                    facts.page == popup_page_t(page_t<Features>{}), page_available(facts, page_t<Features>{}))...);
    }
  };
  using pages_t = plugin_pages<media_page_plugins>;

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
    using tabs_t = decltype(pages_t::tabs(std::declval<const palette&>(), std::declval<const emoji_facts&>(),
                                          std::declval<tab_t>()));
    // The colours it is made in: its tabs read them from it.
    const palette* colours_ = nullptr;
    emoji_facts allowed_;
    struct parts_t {
      tabs_t tabs;
      panel_t panel;
      typename pages_t::nodes_t pages;
    } parts;
    card_t(const ui_needs<Actions>& needs, const emoji_facts& facts)
        : card_t(needs, *needs.colours, *needs.emoji, facts) {}
    card_t(const ui_needs<Actions>& needs, const palette &colours, emoji_kept &kept, const emoji_facts& facts)
        : Stacked(skiff::compose::vbox(
              0.0f, {.width = 345.0f,
                     .height = 360.0f,
                     .cornerRadius = 8.0f,
                     .background = colours.sidebar,
                     .border = scene::Border{colours.band, 1.0f},
                     .shadow = scene::Shadow{skia::colorSetARGB(70, 0, 0, 0),
                                             3.0f}})),
          colours_(&colours), allowed_(facts),
          parts{.tabs = pages_t::tabs(colours, facts,
                    popup_tab(colours, popup_page::emoji{}, "Emoji", facts.page == popup_page_t(popup_page::emoji{}), true)),
                .panel = panel_t(colours, kept, insert_emoji_into<Actions>{},
                    {.fillX = true, .grow = scene::axes::kY, .masking = true,
                     .visible = facts.page == popup_page_t(popup_page::emoji{})})} {
      pages_t::build(parts.pages, needs, facts);

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
      static const auto kPages = all_pages(media_page_plugins{});
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
      : Specced({.fill = true}), parts{.card = card_t(n, facts)},
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
