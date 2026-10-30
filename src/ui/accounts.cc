// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:accounts -- The accounts.
module;
#include "../inline_in_release.h"
#include "skiff/named.h"
export module mux.ui:accounts;

import std;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes.flow;
import skiff.nodes.scroll;
import skiff.nodes.text;
import skiff.widgets.button;
import skiff.widgets.sliderbar;
import skiff.widgets.textbox;
import skiff.model;
import skiff.compose;
import mux.core;
import mux.config;
import mux.ui.base;
import :account_forms;
import mux.plugins.ui.top;
import mux.composition;
import mux.levels;
import mux.ui.proto;
import :add_account;
import :info;

export namespace mux::ui {

// ---- what the panel shows, as the program says ----------------------------------------

// One saved account in the list: its address, its protocol and how it is --
// failed, said in the error's colour -- and whether it is the one chosen.
struct account_row {
  std::string address;
  std::string line;
  bool failed = false;
  bool chosen = false;
  friend bool operator==(const account_row&, const account_row&) = default;
};
// The saved accounts as the list shows them, the one chosen marked.
template <std::ranges::input_range Saved>
[[nodiscard]] std::vector<account_row> account_rows(const Saved& saved, const model& now, const std::optional<std::string>& chosen) {
  return saved | std::views::transform([&](const config::account_t& one) {
           const auto [how, failed] = connection_state_of(one, now);
           const std::string& address = config::address_of(one);
           return account_row{address, std::format("{} · {}", config::protocol_name(one), how), failed, chosen == address};
         }) | std::ranges::to<std::vector>();
}
// What the accounts panel shows beside its list: nothing chosen, an
// account's page, or an account being added (its pane reads
// add_account_shown).
namespace panel_detail {
struct none {
  friend bool operator==(none, none) = default;
};
struct account {
  std::string address;
  account_page_t page = account_page::connection{};
};
struct adding {
  friend bool operator==(adding, adding) = default;
};
}  // namespace panel_detail
using panel_detail_t = spl::variant<panel_detail::none, panel_detail::account, panel_detail::adding>;
// The accounts panel open, as the program says: what is beside its list;
// the accounts; a word said at its top, where there is one; and, narrow --
// a phone's -- whether the page is up in place of the list.
struct panel_facts {
  panel_detail_t detail = panel_detail::none{};
  std::optional<std::string> note;
  std::vector<account_row> rows;
  bool narrow = false;
  bool detail_up = false;
  // The theme its colours are shown in.
  config::theme_t theme;
  // Which way the last move went: into an account (from the right, 1) or
  // back (from the left, -1) -- what comes in comes from that side.
  float toward = 1.0f;
  // Changed whole, as one part.
  using ModelOpaque = void;
};
using panel_read = std::optional<panel_facts>;
using page_read = std::optional<account_page_facts>;
// The account chosen, where one is.
[[nodiscard]] inline std::optional<std::string> chosen_of(const panel_read& now) {
  if (!now)
    return std::nullopt;
  return spl::visit(spl::overloaded{[](const panel_detail::account& one) { return std::optional(one.address); },
                                    [](const auto&) { return std::optional<std::string>(); }},
                    now->detail);
}
[[nodiscard]] inline bool adding_now(const panel_read& now) {
  return now && spl::visit(spl::overloaded{[](panel_detail::adding) { return true; }, [](const auto&) { return false; }}, now->detail);
}

// Which page is beside the list, as a number: another where the page is
// made anew -- for what binds the pages to the settings to bind them again.
[[nodiscard]] inline std::uint64_t detail_identity(const panel_read& now) {
  if (!now)
    return 0;
  return spl::visit(spl::overloaded{[](const panel_detail::account& one) {
                                      return std::hash<std::string>{}(one.address) ^ (static_cast<std::uint64_t>(one.page.index()) << 48);
                                    },
                                    [&](const auto&) { return static_cast<std::uint64_t>(now->detail.index()) << 56; }},
                    now->detail);
}

// ---- the list down the side ---------------------------------------------------------

// One account in the list: its address over its protocol and state, on a
// plate lit while it is the one chosen; pressed, its pages.
MUX_INLINE_IN_RELEASE auto account_row_view(const palette& colours, const account_row& one) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(account_row_view, c::said(scene::SemanticRole{scene::semantic_role::list_item{}}, one.chosen,
      c::onPress(sends<request::select_account>{{one.address}}, c::column(
          c::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {7.0f, 16.0f, 7.0f, 16.0f},
                         .background = colours.sidebar, .hoverBackground = colours.chosen, .selectedBackground = colours.chosen,
                         .focusBackground = colours.chosen, .selected = one.chosen}),
          c::styled({.fillX = true}, elided(nodes::Text(one.address, 15.0f, colours.text, true))),
          c::styled({.fillX = true}, elided(nodes::Text(one.line, 13.0f, one.failed ? colours.error : colours.dim)))),
          one.address)));
}
MUX_INLINE_IN_RELEASE auto account_list(const palette& colours, const std::vector<account_row>& rows) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(account_list, c::many(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                 rows | std::views::transform([&](const account_row& one) { return account_row_view(colours, one); }) | std::ranges::to<std::vector>()));
}
using account_list_t = decltype(account_list(std::declval<const palette&>(), {}));
// The side with no account chosen: Add account, lit while one is being
// added, and the accounts under it.
MUX_INLINE_IN_RELEASE auto accounts_side(const palette& colours) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(accounts_side, c::column(c::vbox(0.0f, {.fill = true}),
      c::spec_for<panel_read>([](const panel_read& now) { return scene::Spec{.selected = adding_now(now)}; },
          c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY, .selectedBackground = colours.chosen}),
                    row_item(colours, "Add account", sends<request::open_new_account>{}, icon::plus{}))),
      c::styled({.fillX = true, .grow = scene::axes::kY}, skiff::compose::scrolled(c::column(
          c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
          c::projected<panel_read>([](const panel_read& now) { return now ? std::optional(now->rows) : std::nullopt; },
              c::mount<account_list_t, std::vector<account_row>>(
                  [at = &colours](const std::vector<account_row>& rows) { return std::tuple{account_list(*at, rows)}; },
                  {.fillX = true, .autoSize = scene::axes::kY})))))));
}
using accounts_side_t = decltype(accounts_side(std::declval<const palette&>()));

// ---- an account's pages, down the side --------------------------------------------

struct account_page_link {
  std::string title;
  account_page_t page;
  icon_t icon;
};
MUX_INLINE_IN_RELEASE auto account_page_row(const palette& colours, account_page_link link, const account_page_t& selected) {
  namespace c = skiff::compose;
  const bool lit = link.page.index() == selected.index();
  return SKIFF_NAMED(account_page_row, c::onClick(request::account_page{std::move(link.page)}, c::row(
      c::hbox(17.0f, {.fillX = true, .height = 46.0f, .padding = {0.0f, 20.0f, 0.0f, 20.0f},
          .hoverBackground = colours.chosen, .selectedBackground = colours.chosen,
          .focusBackground = colours.chosen, .selected = lit}),
      c::styled({.alignSelf = scene::align::kMiddle}, icon_mark(colours, link.icon, 24.0f, 24.0f)),
      c::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle},
          elided(nodes::Text(std::move(link.title), 15.0f, colours.text))))));
}
// The client's pages, then its protocol's own, then Proxy: the one up lit.
// Protocol UI overloads arrive after mux.ui is imported; resolved where the
// program instantiates it.
// Whether a plugin's account page is shown for an account of this
// protocol: as it says (page_shown(page, state), found by ADL), else for
// every account.
namespace page_defaults {
constexpr bool page_shown(const auto&, const auto&) { return true; }
}  // namespace page_defaults
template <class Page, class State>
[[nodiscard]] constexpr bool page_shown_for(const State& now) {
  using page_defaults::page_shown;
  return page_shown(Page{}, now);
}
// The plugins' pages for a protocol's accounts (contract::account_pages):
// those whose page says it is for them (page_shown(page, its state)) --
// the list made while compiling, of that protocol's alone.
template <class Protocol, class Page>
inline constexpr bool page_for = page_shown_for<Page>(state_of<Protocol>{});
template <class Protocol, class... Pages>
auto plugin_pages_for(type_list<Pages...>) -> typename joined<type_list<>, std::conditional_t<page_for<Protocol, Pages>, type_list<Pages>, type_list<>>...>::type;
template <class Protocol>
using plugin_pages_of = decltype(plugin_pages_for<Protocol>(plugin_account_pages_t{}));
template <class... Pages>
std::vector<account_page_link> links_of(type_list<Pages...>) {
  return {{std::string(page_title(Pages{})), account_page_t{Pages{}}, page_icon(Pages{})}...};
}
template <class... Pages>
std::vector<account_page_link> links_of(proto::account_page_list<Pages...>) {
  return {{std::string(page_title(Pages{})), account_page_t{Pages{}}, page_icon(Pages{})}...};
}
// A protocol's account's pages: the client's, its protocol's own, then the
// plugins' for it.
template <class Protocol>
auto account_pages_view(const palette& colours, const account_page_t& selected) {
  namespace c = skiff::compose;
  std::vector<account_page_link> links{
      {"Connection", account_page::connection{}, icon::sliders{}},
      {"Privacy", account_page::privacy{}, icon::eye{}},
      {"Notifications", account_page::notifications{}, icon::bell{}},
      {"Appearance", account_page::appearance{}, icon::gear{}},
      {"Chats", account_page::chats{}, icon::people{}}};
  auto groups = std::array{std::move(links), links_of(proto::account_pages_of(state_of<Protocol>{})), links_of(plugin_pages_of<Protocol>{})};
  auto rows = groups | std::views::join | std::views::transform([&](auto link) {
    return account_page_row(colours, std::move(link), selected);
  }) | std::ranges::to<std::vector>();
  return SKIFF_NAMED(account_pages_view, c::many(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {6.0f, 0.0f, 0.0f, 0.0f}}), std::move(rows)));
}
// One protocol's list or another's, as the account chosen is.
template <class>
struct account_pages_of_list;
template <class... Protocols>
struct account_pages_of_list<protocol_list<Protocols...>> {
  using type = spl::variant<decltype(account_pages_view<Protocols>(std::declval<const palette&>(), std::declval<const account_page_t&>()))...>;
};
using account_pages_variant = typename account_pages_of_list<protocols>::type;
// In a column, as the side's own variant is: what is mounted stays one type.
MUX_INLINE_IN_RELEASE auto account_pages_in(account_pages_variant pages) {
  return skiff::compose::column(skiff::compose::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), std::move(pages));
}
using account_pages = decltype(account_pages_in(std::declval<account_pages_variant>()));
// Which page is up, of which protocol: the links made again where either
// moves.
struct pages_key {
  protocol_t speaks;
  account_page_t page;
  friend bool operator==(const pages_key& a, const pages_key& b) {
    return a.speaks.index() == b.speaks.index() && a.page.index() == b.page.index();
  }
};
MUX_INLINE_IN_RELEASE auto pages_side(const palette& colours) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(pages_side, c::projected<page_read>([](const page_read& now) { return now ? std::optional(pages_key{now->by.speaks, now->page}) : std::nullopt; },
      c::mount<account_pages, pages_key>([at = &colours](const pages_key& now) {
        return std::tuple{account_pages_in(
            spl::visit([&]<class Protocol>(Protocol) { return account_pages_variant(account_pages_view<Protocol>(*at, now.page)); }, now.speaks))};
      }, {.fillX = true, .autoSize = scene::axes::kY})));
}
using pages_side_t = decltype(pages_side(std::declval<const palette&>()));

// ---- the client's pages of an account -------------------------------------------------

// A switch row of an account's: its words, on as the account kept says, a
// press asking `Flip`; dimmed and still where `allowed` says not.
template <class Flip, class On, class Allowed>
auto account_switch(const palette& colours, std::string text, On on, Allowed allowed) {
  namespace c = skiff::compose;
  using toggle_t = toggle_for<Flip>;
  return SKIFF_NAMED(account_switch, c::spec_for<page_read>([allowed](const page_read& now) {
    const bool may = now && allowed(now->account);
    return scene::Spec{.alpha = may ? 1.0f : 0.4f, .disabled = !may};
  }, c::row(c::hbox(16.0f, {.fillX = true, .height = kRowHeight, .padding = {0.0f, 20.0f, 0.0f, 20.0f}}),
            c::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle}, elided(nodes::Text(text, 15.0f, colours.text))),
            c::named(text, {}, c::applied<page_read>([on](const page_read& now) { return now && on(now->account); },
                                  [](toggle_t& toggle, bool now) { toggle.setOn(now); },
                                  c::styled({.alignSelf = scene::align::kMiddle}, toggle_t(colours.widgets, Flip{})))))));
}
inline constexpr auto always = [](const config::account_t&) { return true; };

// The plugins' rows on a protocol's account's page (contract::account_rows<P>):
// those put on that protocol's.
template <class Protocol, class... Features>
auto account_rows_of(plugin_list<Features...>, const palette& colours, const std::string& address) {
  return skiff::compose::column(skiff::compose::vbox(6.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      extend(contract::account_rows<Protocol>{}, type_tag<Features>{}, colours, address)...);
}
// The connection: its address, on or off and Remove; how it is now; and
// its protocol's form.
template <class Protocol>
auto account_editor_view(const ui_needs& n, const std::string& address) {
  namespace c = skiff::compose;
  const palette& colours = *n.colours;
  using toggle_t = widgets::Toggle<flip_account>;
  return SKIFF_NAMED(account_editor_view, c::column(c::vbox(6.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      c::row(c::hbox(10.0f, {.fillX = true, .autoSize = scene::axes::kY}),
             c::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle}, elided(nodes::Text(address, 20.0f, colours.text, true))),
             c::styled({.alignSelf = scene::align::kMiddle}, nodes::Text("On", 13.0f, colours.dim)),
             c::applied<page_read>([](const page_read& now) { return now && config::enabled_of(now->account); },
                                   [](toggle_t& toggle, bool on) { toggle.setOn(on); },
                                   c::styled({.alignSelf = scene::align::kMiddle}, toggle_t(colours.widgets, flip_account{address}))),
             c::styled({.width = 100.0f, .height = 32.0f}, widgets::Button<remove_account>(colours.widgets, "Remove", remove_account{address}))),
      c::applied<page_read>([](const page_read& now) { return now ? form_said{now->how, now->failed} : form_said{}; },
                            [at = &colours](nodes::Text& line, const form_said& now) {
                              line.setText(now.text);
                              line.setColour(now.error ? at->error : at->dim);
                            },
                            c::styled({.fillX = true, .margin = {0.0f, 0.0f, 14.0f, 0.0f}}, elided(nodes::Text("", 13.0f, colours.dim)))),
      account_form_view(n),
      account_rows_of<Protocol>(composition::providers<contract::account_rows<Protocol>, enabled_plugins>{}, colours, address)));
}

// Privacy: whether it sends read receipts, and tells others one is typing;
// and, where its protocol can (Matrix's), the mentions read shared with its
// other sessions -- sealed there, where they are shared.
template <class Protocol>
auto mentions_part(const palette&) {
  return nothing_shown();
}
template <class Protocol>
  requires(proto::offered_by<state_of<Protocol>, proto::feature::shared_mentions>())
auto mentions_part(const palette& colours) {
  namespace c = skiff::compose;
  return c::column(c::vbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}),
          account_switch<sends<request::flip_account_mentions_shared>>(colours, "Sync read mentions between sessions",
              [](const config::account_t& one) { return config::mentions_choice_of(one).value_or(config::mentions_choice{}).shared; }, always),
          account_switch<sends<request::flip_account_mentions_sealed>>(colours, "Encrypt them (with the recovery key)",
              [](const config::account_t& one) { return config::mentions_choice_of(one).value_or(config::mentions_choice{}).sealed; },
              [](const config::account_t& one) { return config::mentions_choice_of(one).value_or(config::mentions_choice{}).shared; }),
          c::styled({.fillX = true}, wrapped(note_text(colours, "Which mentions you have read, kept with your account on its server, so "
                                                                "your other sessions take them as read too. Encrypted, the server cannot "
                                                                "read them; a session gets the key when it is restored with the recovery key."))));
}
template <class Protocol>
auto account_privacy_view(const palette& colours, const std::string& address) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(account_privacy_view, c::column(c::vbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      section_title(colours, "PRIVACY"),
      account_switch<sends<request::flip_account_receipts>>(colours, "Send read receipts",
          [](const config::account_t& one) { return config::read_receipts_of(one); }, always),
      c::styled({.fillX = true}, wrapped(note_text(colours, "Off, the people you talk to through this account are not told when you have "
                                                            "read their messages. Theirs are still shown, and "
                                                            "receipts are still kept here."))),
      // The plugins' rows on it (contract::account_privacy_rows): typing sent.
      rows_of<contract::account_privacy_rows>::made(colours, level_place::account_in<Protocol>{{address}}),
      mentions_part<Protocol>(colours)));
}

// Notifications: whether what comes through it is told -- a message, an
// invite -- and with sound; each chat of it may choose again.
MUX_INLINE_IN_RELEASE auto account_notify_view(const palette& colours, std::string address) {
  using shared = config::account_shared;
  using skiff::compose::bound;
  const choice_level_t level = choice_level::account{};
  return SKIFF_NAMED(account_notify_view, skiff::compose::scoped<config::account_t>(
      skiff::compose::handlers(),
      skiff::compose::column(
          skiff::compose::vbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}),
          bound<skiff::model::Field<&shared::notify>>(show_hide_field<notify_on_setting, std::optional<bool>, skiff::model::Field<&shared::notify>>(colours, level)),
          bound<skiff::model::Field<&shared::notify_mentions>>(show_hide_field<notify_mentions_setting, std::optional<bool>, skiff::model::Field<&shared::notify_mentions>>(colours, level)),
          bound<skiff::model::Field<&shared::notify_sound>>(show_hide_field<notify_sound_setting, std::optional<bool>, skiff::model::Field<&shared::notify_sound>>(colours, level))),
      std::move(address)));
}
MUX_INLINE_IN_RELEASE auto account_notifications_view(const palette& colours, const std::string& address) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(account_notifications_view, c::column(c::vbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      section_title(colours, "NOTIFICATIONS"),
      account_notify_view(colours, address),
      c::styled({.fillX = true, .margin = {10.0f, 0.0f, 0.0f, 0.0f}},
                wrapped(note_text(colours, "For messages and invites that come through this account; Default is as the Notifications settings "
                                           "say. A space, and a chat, can choose again in its own settings.")))));
}

// Its chats' own choices, bound to the account's in the model.
template <class Protocol>
auto account_chats_view(const palette& colours, std::string address) {
  using shared = config::account_shared;
  using skiff::compose::bound;
  const choice_level_t level = choice_level::account{};
  auto rows = rows_of<contract::account_chat_rows>::made(colours, level_place::account_in<Protocol>{{address}});
  return SKIFF_NAMED(account_chats_view, skiff::compose::column(skiff::compose::vbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}),
    skiff::compose::scoped<config::account_t>(
      skiff::compose::handlers(),
      skiff::compose::column(
          skiff::compose::vbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}),
          room_events_part<Protocol, shared>(colours, level),
          bound<skiff::model::Field<&shared::jump_search>>(jump_search_field<std::optional<std::int64_t>, skiff::model::Field<&shared::jump_search>>(colours, level))),
      std::move(address)),
    std::move(rows)));
}
// Its colour chosen.
struct set_colour {
  using Answer = std::optional<request::set_account_colour>;
  std::optional<request::set_account_colour> operator()(const config::accent_t& one) { return request::set_account_colour{one}; }
};
// Chats: its colour -- the strip on its chats in other lists, unless they
// chose another -- how its chats are, their looks, and its spaces in the
// bars (the spaces plugin's section of its looks). Each as every
// account's, until chosen here.
template <class Protocol>
auto account_chats_page(const ui_needs& n, const std::string& address, const config::theme_t& theme) {
  namespace c = skiff::compose;
  const palette& colours = *n.colours;
  const auto spaced = [](nodes::Text title) { return c::styled({.margin = {10.0f, 0.0f, 0.0f, 0.0f}}, std::move(title)); };
  return SKIFF_NAMED(account_chats_page, c::column(c::vbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      section_title(colours, "COLOUR"),
      accent_circles_for<page_read>(set_colour{}, theme, false, [](const page_read& now) {
        return now ? config::colour_of(now->account) : config::accent_t{};
      }),
      account_switch<sends<request::flip_account_strip>>(colours, "A strip on its chats in other lists",
          [](const config::account_t& one) { return config::strip_of(one); }, always),
      spaced(section_title(colours, "CHATS")),
      account_chats_view<Protocol>(colours, address),
      spaced(section_title(colours, "LOOKS")),
      level_section_plugins::made(colours, choice_level::account{}, *n.shared)));
}

MUX_INLINE_IN_RELEASE auto account_appearance_view(const palette& colours, std::string address) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(account_appearance_view, c::scoped<config::account_t>(c::handlers(), c::column(
      c::vbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      section_title(colours, "APPEARANCE"), style_controls(colours)), std::move(address)));
}


// ---- what is beside the list ------------------------------------------------------------

// What the page beside the list is made from: the detail, and the theme
// its colours are in. Made again where the detail, the account or its page
// changes; each page reads its account's facts as they move.
struct detail_key {
  panel_detail_t detail;
  config::theme_t theme;
  float toward = 1.0f;
  friend bool operator==(const detail_key& a, const detail_key& b) {
    if (a.detail.index() != b.detail.index())
      return false;
    return spl::visit(spl::overloaded{[&](const panel_detail::account& one) {
                                        const auto& other = spl::get<panel_detail::account>(b.detail);
                                        return one.address == other.address && one.page.index() == other.page.index();
                                      },
                                      [](const auto&) { return true; }},
                      a.detail);
  }
};
MUX_INLINE_IN_RELEASE auto choose_one(const palette& colours) { return nodes::Text("Choose an account.", 15.0f, colours.dim); }
// The pages of the client's, and each protocol's own.
template <class List>
struct protocol_page_views;
template <class... Pages>
struct protocol_page_views<type_list<Pages...>> {
  using type = type_list<account_page_view_t<Pages>...>;
};
template <class Protocol>
using editor_view_t = decltype(account_editor_view<Protocol>(std::declval<const ui_needs&>(), std::string()));
template <class Protocol>
using privacy_view_t = decltype(account_privacy_view<Protocol>(std::declval<const palette&>(), std::string()));
using notifications_view_t = decltype(account_notifications_view(std::declval<const palette&>(), std::string()));
template <class Protocol>
using chats_page_t = decltype(account_chats_page<Protocol>(std::declval<const ui_needs&>(), std::string(), config::theme_t{}));
using appearance_view_t = decltype(account_appearance_view(std::declval<const palette&>(), std::string()));
using choose_one_t = decltype(choose_one(std::declval<const palette&>()));
// The client's pages that are a protocol's own type, for each protocol.
template <class>
struct client_page_views;
template <class... Protocols>
struct client_page_views<protocol_list<Protocols...>> {
  using type = type_list<editor_view_t<Protocols>..., privacy_view_t<Protocols>..., chats_page_t<Protocols>...>;
};
using detail_page_t = typename variant_of_types<typename joined<
    type_list<choose_one_t, add_account_view_t, notifications_view_t, appearance_view_t>,
    typename client_page_views<protocols>::type,
    typename protocol_page_views<typename protocol_account_pages<protocols>::type>::type,
    typename protocol_page_views<plugin_account_pages_t>::type>::type>::type;
// An account's page: a protocol's made over the facts it starts from --
// read again as they move.
// Of its protocol's type: the account's protocol read once, here.
template <class Protocol>
[[nodiscard]] detail_page_t page_made_for(const ui_needs& n, const panel_detail::account& chosen, const config::theme_t& theme) {
  const palette& colours = *n.colours;
  const std::string& address = chosen.address;
  const account_page_facts facts{.by = account_id{Protocol{}, address}, .page = chosen.page};
  return spl::visit(spl::overloaded{
                        [&](account_page::connection) { return detail_page_t(account_editor_view<Protocol>(n, address)); },
                        [&](account_page::privacy) { return detail_page_t(account_privacy_view<Protocol>(colours, address)); },
                        [&](account_page::notifications) { return detail_page_t(account_notifications_view(colours, address)); },
                        [&](account_page::chats) { return detail_page_t(account_chats_page<Protocol>(n, address, theme)); },
                        [&](account_page::appearance) { return detail_page_t(account_appearance_view(colours, address)); },
                        // A protocol's own: as its page_view makes it.
                        [&]<class Page>(Page page) { return detail_page_t(page_view(page, colours, facts)); }},
                    chosen.page);
}
[[nodiscard]] inline detail_page_t page_made(const ui_needs& n, const panel_detail::account& chosen, const config::theme_t& theme) {
  return spl::visit([&]<class Protocol>(Protocol) { return page_made_for<Protocol>(n, chosen, theme); }, protocol_of(chosen.address));
}
[[nodiscard]] inline detail_page_t detail_made(const ui_needs& n, const detail_key& key) {
  return spl::visit(spl::overloaded{[&](panel_detail::none) { return detail_page_t(choose_one(*n.colours)); },
                                    [&](panel_detail::adding) { return detail_page_t(add_account_view(n)); },
                                    [&](const panel_detail::account& chosen) { return page_made(n, chosen, key.theme); }},
                    key.detail);
}
// It, coming in as it changes -- faded in, and slid in from the side the
// move came from: across the pane, as tall as what it holds.
MUX_INLINE_IN_RELEASE auto detail_shown(const ui_needs& n, const detail_key& key) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(detail_shown, c::appearing_across(true, c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), detail_made(n, key)),
                             28.0f * key.toward, 200.0f));
}
using detail_shown_t = decltype(detail_shown(std::declval<const ui_needs&>(), detail_key{}));
// The page column: what the facts say is beside the list, in a scroll view
// -- a page taller than the window scrolls.
MUX_INLINE_IN_RELEASE auto detail_column(const ui_needs& n) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(detail_column, c::column(c::vbox(0.0f, {.fill = true, .padding = {24.0f, 28.0f, 24.0f, 28.0f}}),
      c::styled({.fill = true}, skiff::compose::scrolled(c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
          c::projected<panel_read>([](const panel_read& now) { return now ? std::optional(detail_key{now->detail, now->theme, now->toward}) : std::nullopt; },
              c::mount<detail_shown_t, detail_key>(
                  [at = &n](const detail_key& key) { return std::tuple{detail_shown(*at, key)}; },
                  {.fillX = true, .autoSize = scene::axes::kY})))))));
}

// ---- the panel ------------------------------------------------------------------------

inline constexpr float kListWidth = 280.0f;
// The side: the accounts, or -- an account chosen -- its pages; the one
// coming in as the other goes. Under it, what went wrong saving.
struct side_key {
  bool account = false;
  float toward = 1.0f;
  friend bool operator==(const side_key& a, const side_key& b) { return a.account == b.account; }
};
MUX_INLINE_IN_RELEASE auto side_made(const palette& colours, const side_key& key) {
  namespace c = skiff::compose;
  using side_t = spl::variant<accounts_side_t, pages_side_t>;
  return SKIFF_NAMED(side_made, c::appearing_across(true, c::column(c::vbox(0.0f, {.fill = true}),
                                             key.account ? side_t(pages_side(colours)) : side_t(accounts_side(colours))),
                             36.0f * key.toward, 220.0f));
}
using side_made_t = decltype(side_made(std::declval<const palette&>(), side_key{}));
MUX_INLINE_IN_RELEASE auto side_column(const palette& colours) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(side_column, c::spec_for<panel_read>([](const panel_read& now) {
    const bool narrow = now && now->narrow;
    // Which axes follow the parent, said both ways: a fill once asked for
    // is otherwise kept, and the list's width then taken as a fraction of
    // the window's (280 of them, as wide as a hundred thousand pixels).
    return scene::Spec{.fillX = narrow, .fillY = true, .width = narrow ? 1.0f : kListWidth,
                       .relativeSize = narrow ? scene::Axes(scene::axes::kBoth) : scene::Axes(scene::axes::kY)};
  }, c::column(c::vbox(0.0f, {.fillY = true, .background = colours.sidebar}),
      c::projected<panel_read>([](const panel_read& now) { return side_key{chosen_of(now).has_value(), now ? now->toward : 1.0f}; },
          c::mount<side_made_t, side_key>([at = &colours](const side_key& key) { return std::tuple{side_made(*at, key)}; },
                                          {.fillX = true, .grow = scene::axes::kY})),
      c::shown_for<panel_read>([](const panel_read& now) { return now && now->note.has_value(); },
          c::text_for<panel_read>([](const panel_read& now) { return now ? now->note.value_or("") : std::string(); },
              c::styled({.fillX = true, .margin = scene::Margin::all(8.0f)}, wrapped(nodes::Text("", 13.0f, colours.error))))))));
}
// The header: ← back -- from a page to the list, narrow; from an account to
// the list; from the list to the chats -- and the account chosen's address,
// or Accounts.
MUX_INLINE_IN_RELEASE auto panel_header(const palette& colours) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(panel_header, c::row(c::hbox(12.0f, {.fillX = true, .height = kPageHeaderHeight, .padding = {0.0f, 10.0f, 0.0f, 10.0f}}),
      c::styled({.alignSelf = scene::align::kMiddle}, icon_button(colours, icon::back{}, sends<request::accounts_back>{})),
      c::text_for<panel_read>([](const panel_read& now) { return chosen_of(now).value_or("Accounts"); },
          c::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle}, elided(nodes::Text("", 17.0f, colours.text, true))))));
}
// The accounts: the header over the list down the side and, beside it, what
// is chosen. Narrow -- a phone's -- one column at a time, all of the width:
// the list (or a chosen one's pages), or the page chosen from them. Esc goes
// back as the arrow does.
MUX_INLINE_IN_RELEASE auto accounts_view(const ui_needs& n) {
  namespace c = skiff::compose;
  const palette& colours = *n.colours;
  return SKIFF_NAMED(accounts_view, closes_on_escape<sends<request::accounts_back>>(c::column(c::vbox(0.0f, {.fill = true}),
      panel_header(colours),
      c::row(c::hbox(0.0f, {.fillX = true, .grow = scene::axes::kY}),
             c::shown_for<panel_read>([](const panel_read& now) { return !now || !now->narrow || !now->detail_up; }, side_column(colours)),
             c::shown_for<panel_read>([](const panel_read& now) { return !now || !now->narrow || now->detail_up; },
                                      c::column(c::vbox(0.0f, {.fillY = true, .grow = scene::axes::kX}), detail_column(n)))))));
}
using accounts_panel = decltype(accounts_view(std::declval<const ui_needs&>()));

}  // namespace mux::ui

