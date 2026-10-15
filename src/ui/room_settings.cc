// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:room_settings -- Room settings with protocol-owned pages and
// the client's preferences. Each protocol chooses its initial page and
// page names; permission checks also guard the requests behind controls.
export module mux.ui:room_settings;

import std;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes.box;
import skiff.nodes.flow;
import skiff.nodes.icon;
import skiff.nodes.image;
import skiff.nodes.scroll;
import skiff.nodes.text;
import skiff.widgets.button;
import skiff.widgets.model;
import skiff.widgets.sliderbar;
import skiff.widgets.textarea;
import skiff.model;
import skiff.compose;
import mux.core;
import mux.config;
import mux.protocols;
import mux.ui.base;
import mux.plugins.ui.top;
import mux.ui.proto;
import :info;

export namespace mux::ui {

// What the dialog shows of a room, as it was when it opened.
struct room_settings;

// Page factories need a complete type while the dialog's child types are
// deduced. Keep their data and refresh entry point outside the dialog.
struct room_settings_context : room_settings_shown {
  room_settings* owner;
  room_manage_style presentation;
  template <class Tag>
  using tabs_of_t = decltype(manage_tabs_of(state_of<Tag>{}));
  template <class>
  struct protocol_lists;
  template <class... Tags>
  struct protocol_lists<protocol_list<Tags...>> {
    using tabs = typename joined<type_list<>, typename tab_types<tabs_of_t<Tags>>::type...>::type;
    using parts = std::tuple<typename decltype(manage_part_of(state_of<Tags>{}))::type...>;
  };
  using protocol_tabs = typename protocol_lists<protocols>::tabs;
  typename protocol_lists<protocols>::parts protocol_parts;
  template <class Tag, class... Tags>
  static constexpr std::size_t index_in(protocol_list<Tags...>) {
    constexpr std::array<bool, sizeof...(Tags)> is{std::derived_from<Tag, Tags>...};
    return static_cast<std::size_t>(std::ranges::find(is, true) - is.begin());
  }
  template <class State> [[nodiscard]] auto& part() {
    return std::get<index_in<proto::id<State>>(protocols{})>(protocol_parts);
  }
  room_settings_context(room_settings* owner, const palette* colours, const looks_shown* looks,
                        const room_settings_facts& shown);
};

// The dialog's parts, made before it: their types known as it is defined.
template <class Pick>
auto manage_tab_row(const palette& colours, std::string text, icon_t icon, Pick what) {
  namespace c = skiff::compose;
  return c::onPress(std::move(what), c::row(c::hbox(8.0f, {.fillX = true, .height = 40.0f,
      .padding = {0.0f, 12.0f, 0.0f, 12.0f}, .cornerRadius = colours.widgets.fCorner,
      .hoverBackground = colours.background, .selectedBackground = colours.tile}),
      c::styled({.width = 16.0f, .height = 16.0f, .alignSelf = scene::align::kMiddle}, icon_mark(colours, icon)),
      c::styled({.shrink = scene::axes::kX, .alignSelf = scene::align::kMiddle}, elided(nodes::Text(std::move(text), 14.0f, colours.text)))));
}
template <class Pick, class Rows, class Context>
auto manage_tab_list_of(Context* box, Rows protocol) {
  namespace c = skiff::compose;
  return c::column(c::vbox(0.0f, {.fillY = true, .width = box->presentation.navigation_width, .padding = {0.0f, 8.0f, 32.0f, 8.0f}, .background = box->colours_->sidebar}),
      c::row(c::hbox(8.0f, {.fillX = true, .height = 54.0f}), avatar_mark(avatar_of::chat{}, box->facts.id, box->facts.name, 24.0f),
          c::styled({.relativeSize = scene::axes::kNone, .grow = scene::axes::kX, .shrink = scene::axes::kX, .alignSelf = scene::align::kMiddle}, elided(nodes::Text(box->facts.name, 18.0f, box->colours_->text, true))),
          c::visible(false, icon_button<sends<::mux::ui::request::close_manage>>(*box->colours_, icon::close{}, {}))),
      c::many(c::vbox(2.0f, {.fillX = true, .autoSize = scene::axes::kY}), std::move(protocol)),
      c::visible(true, manage_tab_row(*box->colours_, spl::visit([](auto tag) { return std::string(preferences_title(state_of<decltype(tag)>{})); }, box->facts.speaks), icon::gear{}, Pick{box->owner, settings_tab::general{}})),
      c::visible(true, manage_tab_row(*box->colours_, "Notifications", icon::bell{}, Pick{box->owner, settings_tab::notifications{}})),
      c::visible(true, manage_tab_row(*box->colours_, "Appearance", icon::eye{}, Pick{box->owner, settings_tab::looks{}})));
}
template <class Back, class Close>
auto manage_room_header(const palette& colours, std::string name, Back back, Close close, bool has_back) {
  namespace c = skiff::compose;
  auto header = page_header<Back, Close>(colours, std::move(name), back, close, has_back, true);
  return c::row(c::hbox(12.0f, {.fillX = true, .height = kPageHeaderHeight, .padding = {0.0f, 10.0f, 0.0f, 10.0f}}),
      std::move(std::get<0>(header.fParts)), c::onPress(back, std::move(std::get<1>(header.fParts))), std::move(std::get<2>(header.fParts)));
}
// The dialog's row: the tabs down its left, and the page beside them under
// its header -- each part made by the dialog, put together here.
template <class Context, class Tabs, class Header, class Holder>
auto manage_body_row(Context* box, Tabs tabs, Header header, Holder holder) {
  namespace c = skiff::compose;
  return c::row(c::hbox(0.0f, {.fillX = true, .grow = scene::axes::kY}), std::move(tabs),
      c::column(c::vbox(0.0f, {.fillY = true, .relativeSize = scene::axes::kY, .grow = scene::axes::kX,
                               .background = box->colours_->background}),
          c::visible(box->presentation.page_header, std::move(header)),
          c::styled({.fillX = true, .relativeSize = scene::axes::kX, .grow = scene::axes::kY},
              nodes::ScrollContainer<Holder>(std::move(holder)))));
}

struct room_settings : skiff::compose::Stacked {
  // Child references and handlers require a fixed address.
  room_settings(const room_settings&) = delete;
  room_settings& operator=(const room_settings&) = delete;
  room_settings(room_settings&&) = delete;
  room_settings& operator=(room_settings&&) = delete;

  // The dialog it is shown in.
  [[nodiscard]] static dialog_look look_of_dialog() { return {.size = dialog_size::fixed{802.0f, 684.0f}}; }
  using context_type = room_settings_context;
  context_type context;
  // The colours it and its pages are made in: what it was handed.
  const palette*& colours_;
  // The looks shown: the program's.
  const looks_shown*& looks_;
  room_settings_facts& facts;

  // ---- the tabs and pages: the client's, then each protocol's -----------------
  template <class Tag> using tabs_of_t = typename context_type::template tabs_of_t<Tag>;
  using protocol_tabs = typename context_type::protocol_tabs;
  template <class State> [[nodiscard]] auto& part() { return context.template part<State>(); }
  // A room avatar being sent: each protocol's part told.
  void avatar_sending() {
    std::apply([](auto&... parts) {
      using manage_defaults::avatar_sending;
      (avatar_sending(parts), ...);
    }, context.protocol_parts);
    show_again();
  }
  template <class Tab>
  using page_of_t = typename decltype(page_type(Tab{}, type_tag<context_type>{}))::type;
  template <class List>
  struct pages_of;
  template <class... Tabs>
  struct pages_of<type_list<Tabs...>> {
    // Debug Matrix tabs share an owning erased page; the variant needs one
    // alternative for that type, while the tab list keeps every tab.
    using distinct = skiff::compose::Union<skiff::model::Types<>, skiff::model::Types<page_of_t<Tabs>...>>;
    template <class... Pages> static type_list<Pages...> listed(skiff::model::Types<Pages...>);
    using type = decltype(listed(distinct{}));
  };
  using protocol_pages = typename pages_of<protocol_tabs>::type;
  using settings_tab_t = typename variant_of_types<typename joined<
      type_list<settings_tab::general, settings_tab::notifications, settings_tab::looks>, protocol_tabs>::type>::type;

  // ---- what is asked ---------------------------------------------------------
  struct pick_tab {
    using Answer = skiff::model::Nothing;
    room_settings* box;
    settings_tab_t tab;
    skiff::model::Nothing operator()() const {
      box->to_top = true;
      box->navigation = false; box->picked = true;
      box->show_tab(tab);
      return {};
    }
  };

  // ---- the tabs down the left ------------------------------------------------
  using tab_row_t = decltype(manage_tab_row(std::declval<const palette&>(), "", icon_t{}, std::declval<pick_tab>()));
  static tab_row_t tab_row(const palette& colours, std::string text, icon_t icon, pick_tab what) {
    return manage_tab_row(colours, std::move(text), icon, std::move(what));
  }
  template <class... Tabs> static std::vector<tab_row_t> protocol_rows(context_type* box, manage_tab_list<Tabs...>) {
    using manage_defaults::tab_visible;
    const auto listed = std::array<settings_tab_t, sizeof...(Tabs)>{settings_tab_t{Tabs{}}...};
    return std::ranges::to<std::vector<tab_row_t>>(std::views::transform(std::views::filter(listed, [&](const auto& tab) {
      return spl::visit([&](auto one) { return tab_visible(one, box->facts); }, tab);
    }), [&](const auto& tab) {
      return spl::visit([&](auto one) { return tab_row(*box->colours_, std::string(tab_title(one)), tab_icon(one), pick_tab{box->owner, tab}); }, tab);
    }));
  }
  using tab_list_t = decltype(manage_tab_list_of<pick_tab>(std::declval<context_type*>(), std::vector<tab_row_t>{}));
  static tab_list_t tab_list(context_type* box) {
    auto protocol = spl::visit([&](auto tag) { return protocol_rows(box, tabs_of_t<decltype(tag)>{}); }, box->facts.speaks);
    return manage_tab_list_of<pick_tab>(box, std::move(protocol));
  }
  static std::vector<tab_row_t*> tab_rows(tab_list_t& list) {
    auto& [heading, protocol, general, notifications, looks] = list.fParts;
    auto rows = std::ranges::to<std::vector<tab_row_t*>>(std::views::transform(protocol.fParts, [](auto& row) { return &row; }));
    rows.insert(rows.end(), {&general, &notifications, &looks});
    return rows;
  }
  // ---- Notifications ----------------------------------------------------------------
  struct notifications_page : skiff::compose::Stacked {
    struct parts_t {
      nodes::Text heading;
      decltype(chat_notify_view(std::declval<const palette&>(), std::declval<const conversation_id&>())) settings;
      nodes::Text note;
    } parts;
    notifications_page(room_settings *box, const room_settings_facts &facts)
        : Stacked(skiff::compose::vbox(
              10.0f, {.fillX = true,
                      .autoSize = scene::axes::kY,
                      .padding = {0.0f, 28.0f, 24.0f, 12.0f}})),
          parts{.heading = tab_heading(*box->colours_, "Notifications"),
                .settings = chat_notify_view(*box->colours_, facts.chat),
                .note = skiff::compose::styled(
                    {.fillX = true},
                    wrapped(nodes::Text(
                        facts.space
                            ? "For every chat in this space, unless the chat "
                              "chooses again; Default is "
                              "as the space above it, or the account, says."
                            : "Default is as the space it is in, or the "
                              "account, says.",
                        12.0f, box->colours_->dim)))} {}
  };

  // ---- Appearance: the room's background, bubbles and panels --------------------
  struct looks_page : skiff::compose::Stacked {
    struct parts_t {
      nodes::Text heading;
      decltype(skiff::compose::scoped<config::chat_choices>(skiff::compose::handlers(), style_controls(std::declval<const palette&>()), std::declval<conversation_id>())) fonts;
      look_choices choices;
    } parts;
    looks_page(room_settings *box, const room_settings_facts &facts)
        : Stacked(skiff::compose::vbox(
              6.0f, {.fillX = true,
                     .autoSize = scene::axes::kY,
                     .padding = {0.0f, 28.0f, 24.0f, 12.0f}})),
          parts{.heading = tab_heading(*box->colours_, "Appearance"),
                .fonts = skiff::compose::scoped<config::chat_choices>(skiff::compose::handlers(), style_controls(*box->colours_), facts.chat),
                .choices = look_choices(*box->colours_, *box->looks_,
                                                 choice_level::chat{})} {}
  };

  using page_t = typename variant_of_types<
      typename joined<type_list<room_general_page_t, notifications_page, looks_page>, protocol_pages>::type>::type;
  static skiff::compose::Box<page_t> page_holder(page_t first) {
    return skiff::compose::column(skiff::compose::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), std::move(first));
  }
  using page_holder_t = decltype(page_holder(std::declval<page_t>()));

  // ---- the dialog ---------------------------------------------------------------------
  struct back_it {
    room_settings* box;
    using Answer = skiff::model::Nothing;
    Answer operator()() const {
      using manage_defaults::manage_close_editor;
      const bool editor = spl::visit([&](auto tab) {
        return std::apply([&](auto&... part) { return (manage_close_editor(tab, part) || ...); }, box->context.protocol_parts);
      }, box->tab);
      if (editor) box->show_again(); else box->show_navigation();
      return {};
    }
  };
  struct close_it {
    room_settings* box;
    using Answer = std::optional<::mux::ui::request::close_manage>;
    Answer operator()() const {
      using manage_defaults::manage_close_editor;
      const bool editor = spl::visit([&](auto tab) {
        return std::apply([&](auto&... part) { return (manage_close_editor(tab, part) || ...); }, box->context.protocol_parts);
      }, box->tab);
      if (editor) { box->show_again(); return std::nullopt; }
      if (box->narrow && !box->navigation) { box->show_navigation(); return std::nullopt; }
      return ::mux::ui::request::close_manage{};
    }
  };
  using header_t = decltype(manage_room_header(std::declval<const palette&>(), "", std::declval<back_it>(), std::declval<close_it>(), false));
  static header_t room_header(const palette& colours, std::string name, back_it back, close_it close, bool has_back) {
    return manage_room_header(colours, std::move(name), back, close, has_back);
  }
  using body_row_t = decltype(manage_body_row(std::declval<context_type*>(), std::declval<tab_list_t>(), std::declval<header_t>(),
                                               std::declval<page_holder_t>()));
  static body_row_t body_row(context_type* box, page_t first) {
    return manage_body_row(box, tab_list(box), room_header(*box->colours_, "General", {box->owner}, {box->owner}, false),
                           page_holder(std::move(first)));
  }
  struct parts_t { header_t header; body_row_t body; } parts;
  bool narrow = false, navigation = false, picked = false;
  float measured_width = 0.0f;

  settings_tab_t tab = settings_tab::general{};
  // What each protocol keeps while its pages are made again -- Matrix's: an
  // encryption being confirmed, the level being picked -- in the list's order.
  // A tab to be made again, at the next frame: not from inside a press on
  // what it replaces.
  bool rebuild_due = false;
  bool to_top = false;  // another tab: shown from its top

  room_settings(const ui_needs& n, const room_settings_facts& shown) : room_settings(n.colours, n.looks, shown) {}
  room_settings(const palette *colours, const looks_shown *looks,
                const room_settings_facts &shown)
      : Stacked(skiff::compose::vbox(0.0f, {.fill = true})), context{this, colours, looks, shown}, colours_(context.colours_),
        looks_(context.looks_), facts(context.facts),
        parts{.header = room_header(*colours, "Room Settings - " + shown.name, {this}, {this}, false),
              .body =
                  body_row(&context, page_t(std::in_place_index<0>, room_general_page(*colours, shown)))} {
    parts.header.setVisible(!context.presentation.page_header);
    this->apply({.cornerRadius = std::max(10.0f, colours_->widgets.fCorner), .background = colours_->background, .masking = true});
    tab = spl::visit([](auto tag) -> settings_tab_t { return initial_manage_tab(state_of<decltype(tag)>{}); }, facts.speaks);
    this->rebuild();
  }

  [[nodiscard]] auto& tabs() { return std::get<0>(parts.body.fParts); }
  [[nodiscard]] auto& pane() { return std::get<1>(parts.body.fParts); }
  [[nodiscard]] auto& content() { return std::get<1>(pane().fParts); }
  [[nodiscard]] auto& pane_header() { return std::get<0>(pane().fParts); }
  [[nodiscard]] page_holder_t& holder() { return std::get<0>(content().fChildren); }
  [[nodiscard]] page_t& page() { return std::get<0>(holder().fParts); }
  void show_navigation() {
    navigation = true;
    this->arrange_navigation();
    this->markDamaged();
  }
  void arrange_navigation() {
    const bool menu = !narrow || navigation;
    tabs().setVisible(menu);
    tabs().apply({.width = narrow ? measured_width : context.presentation.navigation_width});
    std::get<2>(std::get<0>(tabs().fParts).fParts).setVisible(narrow && navigation);
    pane().setVisible(!narrow || !navigation);
    content().setVisible(!narrow || !navigation);
    std::get<0>(parts.header.fParts).setVisible(narrow && !navigation);
    using manage_defaults::manage_editor_header;
    const bool editor_header = spl::visit([&](auto tab) {
      return std::apply([&](const auto&... part) { return (manage_editor_header(tab, part) || ...); }, context.protocol_parts);
    }, tab);
    std::get<0>(pane_header().fParts).setVisible(editor_header || (narrow && !navigation));
    std::get<1>(pane_header().fParts).apply({.disabled = !editor_header});
    this->invalidateLayout();
  }
  // A tab shown from current facts at the next frame.
  void show_tab(const settings_tab_t& to) {
    spl::visit([](auto& page) { preserve_manage_input(page); }, page());
    tab = to;
    rebuild_due = true;
    this->markDamaged();
  }
  // The tab up, made again: what a page's act asks, once it changed the facts.
  void show_again() { this->show_tab(tab); }
  [[nodiscard]] bool settling() const { return rebuild_due; }
  void update(double) {
    const auto width = parts.body.fState.contentBox().width();
    if (width > 0.0f && width != measured_width) {
      measured_width = width;
      const bool phone = width < 560.0f;
      if (phone && !narrow && !picked) navigation = true;
      narrow = phone;
      this->arrange_navigation();
    }
    if (std::exchange(context.again, false)) rebuild_due = true;
    if (std::exchange(rebuild_due, false))
      this->rebuild();
  }
  void rebuild() {
    const settings_tab_t to = tab;
    auto& page = this->page();
    spl::visit(spl::overloaded{
                      [&](settings_tab::general) { page.template emplace<room_general_page_t>(room_general_page(*colours_, facts, context.presentation.page_header)); },
                      [&](settings_tab::notifications) { page.template emplace<notifications_page>(this, facts); },
                      [&](settings_tab::looks) { page.template emplace<looks_page>(this, facts); },
                      // Protocol pages may be made with combinators or an existing node.
                      [&](auto theirs) {
                        if constexpr (requires { make_manage_page(theirs, &context, facts); })
                          page.template emplace<page_of_t<decltype(theirs)>>(make_manage_page(theirs, &context, facts));
                        else page.template emplace<page_of_t<decltype(theirs)>>(*colours_, facts);
                      }},
                  to);
    std::get<1>(pane_header().fParts).setText(spl::visit(spl::overloaded{[&](settings_tab::general) { return spl::visit([](auto tag) { return std::string(preferences_title(state_of<decltype(tag)>{})); }, facts.speaks); }, [](auto one) { return std::string(tab_title(one)); }}, tab));
    std::get<1>(pane_header().fParts).setFontSize(20.0f);
    std::ranges::for_each(tab_rows(tabs()), [&](auto* row) {
      const bool selected = !navigation && row->fMake.tab.index() == tab.index();
      row->apply({.selected = selected});
      std::get<1>(row->fParts).setBold(selected);
    });
    this->arrange_navigation();
    if (std::exchange(to_top, false))
      content().scrollTo(0.0f);
    this->invalidateLayout();
  }
};

room_settings_context::room_settings_context(room_settings* dialog, const palette* colours,
                                               const looks_shown* looks, const room_settings_facts& shown)
    : room_settings_shown{.facts = shown, .looks_ = looks, .owner = dialog}, owner(dialog),
      presentation(spl::visit([&](auto tag) { return manage_style_of(state_of<decltype(tag)>{}, *colours); }, shown.speaks)) {
  colours_ = &presentation.colours;
  preserve_input = [](skiff::scene::Node* node) {
    auto& dialog = *static_cast<room_settings*>(node);
    spl::visit([](auto& page) { preserve_manage_input(page); }, dialog.page());
  };
  std::apply([&](auto&... parts) {
    using manage_defaults::manage_attach;
    (manage_attach(parts, *this), ...);
  }, protocol_parts);
}

}  // namespace mux::ui

// Outside a release build each child is walked through its table, and a
// stack lays itself out through its own (Stack::layoutChildren): so a unit
// that lays out or walks one of these would make its table, and the walks
// of everything under it, there -- the chats screen's took this unit past
// ten minutes. Their tables are made once, each in a walks unit of this
// module (src/ui/walks_*.cc); here, and wherever they are walked, a call.
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::room_settings> = true;  // walks_manage.cc
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::room_settings>() noexcept;
