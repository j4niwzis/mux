// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:accounts -- The accounts.
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
inline auto account_row_view(const palette& colours, const account_row& one) {
  namespace c = skiff::compose;
  return c::said(scene::SemanticRole{scene::semantic_role::list_item{}}, one.chosen,
      c::onPress(sends<request::select_account>{{one.address}}, c::column(
          c::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {7.0f, 16.0f, 7.0f, 16.0f},
                         .background = colours.sidebar, .hoverBackground = colours.chosen, .selectedBackground = colours.chosen,
                         .focusBackground = colours.chosen, .selected = one.chosen}),
          c::styled({.fillX = true}, elided(nodes::Text(one.address, 15.0f, colours.text, true))),
          c::styled({.fillX = true}, elided(nodes::Text(one.line, 13.0f, one.failed ? colours.error : colours.dim)))),
          one.address));
}
inline auto account_list(const palette& colours, const std::vector<account_row>& rows) {
  namespace c = skiff::compose;
  return c::many(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                 rows | std::views::transform([&](const account_row& one) { return account_row_view(colours, one); }) | std::ranges::to<std::vector>());
}
using account_list_t = decltype(account_list(std::declval<const palette&>(), {}));
// The side with no account chosen: Add account, lit while one is being
// added, and the accounts under it.
inline auto accounts_side(const palette& colours) {
  namespace c = skiff::compose;
  return c::column(c::vbox(0.0f, {.fill = true}),
      c::spec_for<panel_read>([](const panel_read& now) { return scene::Spec{.selected = adding_now(now)}; },
          c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY, .selectedBackground = colours.chosen}),
                    row_item(colours, "Add account", sends<request::open_new_account>{}, icon::plus{}))),
      c::styled({.fillX = true, .grow = scene::axes::kY}, nodes::ScrollContainer(c::column(
          c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
          c::projected<panel_read>([](const panel_read& now) { return now ? std::optional(now->rows) : std::nullopt; },
              c::mount<account_list_t, std::vector<account_row>>(
                  [at = &colours](const std::vector<account_row>& rows) { return std::tuple{account_list(*at, rows)}; },
                  {.fillX = true, .autoSize = scene::axes::kY}))))));
}
using accounts_side_t = decltype(accounts_side(std::declval<const palette&>()));

// ---- an account's pages, down the side --------------------------------------------

struct account_page_link {
  std::string title;
  account_page_t page;
  icon_t icon;
};
inline auto account_page_row(const palette& colours, account_page_link link, const account_page_t& selected) {
  namespace c = skiff::compose;
  const bool lit = link.page.index() == selected.index();
  return c::onClick(request::account_page{std::move(link.page)}, c::row(
      c::hbox(17.0f, {.fillX = true, .height = 46.0f, .padding = {0.0f, 20.0f, 0.0f, 20.0f},
          .hoverBackground = colours.chosen, .selectedBackground = colours.chosen,
          .focusBackground = colours.chosen, .selected = lit}),
      c::styled({.alignSelf = scene::align::kMiddle}, icon_mark(colours, link.icon, 24.0f, 24.0f)),
      c::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle},
          elided(nodes::Text(std::move(link.title), 15.0f, colours.text)))));
}
// The client's pages, then its protocol's own, then Proxy: the one up lit.
// Protocol UI overloads arrive after mux.ui is imported; resolved where the
// program instantiates it.
template <class State>
inline auto account_pages_view(const palette& colours, const State& state, const account_page_t& selected) {
  namespace c = skiff::compose;
  std::vector<account_page_link> links{
      {"Connection", account_page::connection{}, icon::sliders{}},
      {"Privacy", account_page::privacy{}, icon::eye{}},
      {"Notifications", account_page::notifications{}, icon::bell{}},
      {"Appearance", account_page::appearance{}, icon::gear{}},
      {"Chats", account_page::chats{}, icon::people{}}};
  auto own = spl::visit([](const auto& now) {
    return []<class... Pages>(proto::account_page_list<Pages...>) {
      return std::vector<account_page_link>{{std::string(page_title(Pages{})), account_page_t{Pages{}}, page_icon(Pages{})}...};
    }(proto::account_pages_of(now));
  }, state);
  auto groups = std::array{std::move(links), std::move(own),
      std::vector<account_page_link>{{"Proxy", account_page::proxy{}, icon::gear{}}}};
  auto rows = groups | std::views::join | std::views::transform([&](auto link) {
    return account_page_row(colours, std::move(link), selected);
  }) | std::ranges::to<std::vector>();
  return c::many(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {6.0f, 0.0f, 0.0f, 0.0f}}), std::move(rows));
}
using account_pages = decltype(account_pages_view(std::declval<const palette&>(), std::declval<const protocol_state_t&>(),
    std::declval<const account_page_t&>()));
// Which page is up, of which protocol's state: the links made again where
// either moves.
struct pages_key {
  protocol_state_t state;
  account_page_t page;
  friend bool operator==(const pages_key& a, const pages_key& b) {
    return a.state.index() == b.state.index() && a.page.index() == b.page.index();
  }
};
inline auto pages_side(const palette& colours) {
  namespace c = skiff::compose;
  return c::projected<page_read>([](const page_read& now) { return now ? std::optional(pages_key{now->state, now->page}) : std::nullopt; },
      c::mount<account_pages, pages_key>([at = &colours](const pages_key& now) { return std::tuple{account_pages_view(*at, now.state, now.page)}; },
                                         {.fillX = true, .autoSize = scene::axes::kY}));
}
using pages_side_t = decltype(pages_side(std::declval<const palette&>()));

// ---- the client's pages of an account -------------------------------------------------

// A switch row of an account's: its words, on as the account kept says, a
// press asking `Flip`; dimmed and still where `allowed` says not.
template <class Flip, class On, class Allowed>
auto account_switch(const palette& colours, std::string text, On on, Allowed allowed) {
  namespace c = skiff::compose;
  using toggle_t = toggle_for<Flip>;
  return c::spec_for<page_read>([allowed](const page_read& now) {
    const bool may = now && allowed(now->account);
    return scene::Spec{.alpha = may ? 1.0f : 0.4f, .disabled = !may};
  }, c::row(c::hbox(16.0f, {.fillX = true, .height = kRowHeight, .padding = {0.0f, 20.0f, 0.0f, 20.0f}}),
            c::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle}, elided(nodes::Text(text, 15.0f, colours.text))),
            c::applied<page_read>([on](const page_read& now) { return now && on(now->account); },
                                  [](toggle_t& toggle, bool now) { toggle.setOn(now); },
                                  c::named(text, {}, c::styled({.alignSelf = scene::align::kMiddle}, toggle_t(colours.widgets, Flip{}))))));
}
inline constexpr auto always = [](const config::account_t&) { return true; };

// The connection: its address, on or off and Remove; how it is now; and
// its protocol's form.
inline auto account_editor_view(const ui_needs& n, const std::string& address) {
  namespace c = skiff::compose;
  const palette& colours = *n.colours;
  using toggle_t = widgets::Toggle<flip_account>;
  return c::column(c::vbox(6.0f, {.fillX = true, .autoSize = scene::axes::kY}),
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
      account_form_view(n));
}

// Its typing, as every account's until chosen here.
inline auto account_typing_view(const palette& colours, std::string address) {
  return skiff::compose::scoped<config::account_t>(
      skiff::compose::handlers(),
      skiff::compose::bound<skiff::model::Field<&config::account_shared::send_typing>>(
          show_hide_field<typing_setting, std::optional<bool>, skiff::model::Field<&config::account_shared::send_typing>>(colours, choice_level::account{})),
      std::move(address));
}
// Privacy: whether it sends read receipts, and tells others one is typing;
// and, where its protocol can (Matrix's), the mentions read shared with its
// other sessions -- sealed there, where they are shared.
inline auto account_privacy_view(const palette& colours, const std::string& address) {
  namespace c = skiff::compose;
  const auto mentions = [](const page_read& now) { return now && config::mentions_choice_of(now->account).has_value(); };
  return c::column(c::vbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      section_title(colours, "PRIVACY"),
      account_switch<sends<request::flip_account_receipts>>(colours, "Send read receipts",
          [](const config::account_t& one) { return config::read_receipts_of(one); }, always),
      account_typing_view(colours, address),
      c::styled({.fillX = true}, wrapped(note_text(colours, "Off, the people you talk to through this account are not told when you have "
                                                            "read their messages, or that you are typing. Theirs are still shown, and "
                                                            "receipts are still kept here."))),
      c::shown_for<page_read>(mentions, c::column(c::vbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}),
          account_switch<sends<request::flip_account_mentions_shared>>(colours, "Sync read mentions between sessions",
              [](const config::account_t& one) { return config::mentions_choice_of(one).value_or(config::mentions_choice{}).shared; }, always),
          account_switch<sends<request::flip_account_mentions_sealed>>(colours, "Encrypt them (with the recovery key)",
              [](const config::account_t& one) { return config::mentions_choice_of(one).value_or(config::mentions_choice{}).sealed; },
              [](const config::account_t& one) { return config::mentions_choice_of(one).value_or(config::mentions_choice{}).shared; }),
          c::styled({.fillX = true}, wrapped(note_text(colours, "Which mentions you have read, kept with your account on its server, so "
                                                                "your other sessions take them as read too. Encrypted, the server cannot "
                                                                "read them; a session gets the key when it is restored with the recovery key."))))));
}

// Notifications: whether what comes through it is told -- a message, an
// invite -- and with sound; each chat of it may choose again.
inline auto account_notify_view(const palette& colours, std::string address) {
  using shared = config::account_shared;
  using skiff::compose::bound;
  const choice_level_t level = choice_level::account{};
  return skiff::compose::scoped<config::account_t>(
      skiff::compose::handlers(),
      skiff::compose::column(
          skiff::compose::vbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}),
          bound<skiff::model::Field<&shared::notify>>(show_hide_field<notify_on_setting, std::optional<bool>, skiff::model::Field<&shared::notify>>(colours, level)),
          bound<skiff::model::Field<&shared::notify_mentions>>(show_hide_field<notify_mentions_setting, std::optional<bool>, skiff::model::Field<&shared::notify_mentions>>(colours, level)),
          bound<skiff::model::Field<&shared::notify_sound>>(show_hide_field<notify_sound_setting, std::optional<bool>, skiff::model::Field<&shared::notify_sound>>(colours, level))),
      std::move(address));
}
inline auto account_notifications_view(const palette& colours, const std::string& address) {
  namespace c = skiff::compose;
  return c::column(c::vbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      section_title(colours, "NOTIFICATIONS"),
      account_notify_view(colours, address),
      c::styled({.fillX = true, .margin = {10.0f, 0.0f, 0.0f, 0.0f}},
                wrapped(note_text(colours, "For messages and invites that come through this account; Default is as the Notifications settings "
                                           "say. A space, and a chat, can choose again in its own settings."))));
}

// Its chats' own choices, bound to the account's in the model.
inline auto account_chats_view(const palette& colours, std::string address) {
  using shared = config::account_shared;
  using skiff::compose::bound;
  const choice_level_t level = choice_level::account{};
  return skiff::compose::scoped<config::account_t>(
      skiff::compose::handlers(),
      skiff::compose::column(
          skiff::compose::vbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}),
          event_kinds_field<shared>(colours, level, proto::room_event_kinds_of(state_before(protocol_of(address)))),
          bound<skiff::model::Field<&shared::show_receipts>>(show_hide_field<receipts_setting, std::optional<bool>, skiff::model::Field<&shared::show_receipts>>(colours, level)),
          bound<skiff::model::Field<&shared::link_previews>>(show_hide_field<link_previews_setting, std::optional<bool>, skiff::model::Field<&shared::link_previews>>(colours, level)),
          skiff::compose::visible(proto::offers(state_before(protocol_of(address)), proto::feature::server_previews{}),
              bound<skiff::model::Field<&shared::previews_direct>>(
                  show_hide_field<previews_direct_setting, std::optional<bool>, skiff::model::Field<&shared::previews_direct>>(colours, level))),
          bound<skiff::model::Field<&shared::jump_search>>(jump_search_field<std::optional<std::int64_t>, skiff::model::Field<&shared::jump_search>>(colours, level))),
      std::move(address));
}
// Its colour chosen.
struct set_colour {
  using Answer = std::optional<request::set_account_colour>;
  std::optional<request::set_account_colour> operator()(const config::accent_t& one) { return request::set_account_colour{one}; }
};
// Home without what its spaces hold -- but direct messages -- or as every
// account's.
struct pick_home {
  using Answer = std::tuple<request::set_home_hides, request::set_home_direct>;
  Answer operator()(std::size_t index) const {
    const auto said = [&](bool on) { return index == 0 ? std::nullopt : std::optional<bool>(on); };
    return {request::set_home_hides{choice_level::account{}, said(index >= 2)}, request::set_home_direct{choice_level::account{}, said(index == 3)}};
  }
};
[[nodiscard]] inline std::size_t home_way(const config::account_t& one) {
  const auto hides = config::home_hides_of(one);
  return !hides ? 0 : !*hides ? 1 : config::home_direct_of(one).value_or(false) ? 3 : 2;
}
inline auto home_choice(const palette& colours, std::size_t way) {
  return make_choice_menu(colours, "Home", {"As above", "Every chat", "Without chats spaces hold", "Without those and direct messages"},
                          way, pick_home{});
}
using home_choice_t = decltype(home_choice(std::declval<const palette&>(), 0));
// Chats: its colour -- the strip on its chats in other lists, unless they
// chose another -- how its chats are, their looks, and its spaces in the
// bars. Each as every account's, until chosen here.
inline auto account_chats_page(const ui_needs& n, const std::string& address, const config::theme_t& theme) {
  namespace c = skiff::compose;
  const palette& colours = *n.colours;
  const auto spaced = [](nodes::Text title) { return c::styled({.margin = {10.0f, 0.0f, 0.0f, 0.0f}}, std::move(title)); };
  return c::column(c::vbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      section_title(colours, "COLOUR"),
      accent_circles_for<page_read>(set_colour{}, theme, false, [](const page_read& now) {
        return now ? config::colour_of(now->account) : config::accent_t{};
      }),
      account_switch<sends<request::flip_account_strip>>(colours, "A strip on its chats in other lists",
          [](const config::account_t& one) { return config::strip_of(one); }, always),
      spaced(section_title(colours, "CHATS")),
      account_chats_view(colours, address),
      spaced(section_title(colours, "LOOKS")),
      look_choices(colours, choice_level::account{}),
      spaced(section_title(colours, "SPACES")),
      c::projected<page_read>([](const page_read& now) { return now ? std::optional(home_way(now->account)) : std::nullopt; },
          c::mount<home_choice_t, std::size_t>([at = &colours](std::size_t way) { return std::tuple{home_choice(*at, way)}; },
                                               {.fillX = true, .autoSize = scene::axes::kY})),
      spaces_choices(colours, *n.shared));
}

inline auto account_appearance_view(const palette& colours, std::string address) {
  namespace c = skiff::compose;
  return c::scoped<config::account_t>(c::handlers(), c::column(
      c::vbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      section_title(colours, "APPEARANCE"), style_controls(colours)), std::move(address));
}

// A proxy profile chosen for the chosen account: none, or one by its place.
struct choose_account_proxy {
  using Answer = request::choose_account_proxy;
  int index = -1;
  request::choose_account_proxy operator()() const { return {index}; }
};
// Proxy, as Gajim's: which of the program's proxy profiles it connects
// through, or none; and the way to the profiles themselves.
struct proxy_rows_key {
  std::vector<config::proxy_settings> all;
  std::optional<std::string> current;
  friend bool operator==(const proxy_rows_key&, const proxy_rows_key&) = default;
};
inline auto proxy_rows(const palette& colours, const proxy_rows_key& key) {
  namespace c = skiff::compose;
  // No proxy first -- an empty place where the dots are, so the names line
  // up -- then each profile, by its place.
  const auto row = [&](int at) {
    if (at < 0)
      return row_item(colours, "No proxy", choose_account_proxy{-1}, icon::dot{skia::colorSetARGB(0, 0, 0, 0)}, !key.current);
    const config::proxy_settings& one = key.all[static_cast<std::size_t>(at)];
    return row_item(colours, std::format("{} ({} {}:{})", one.name, config::label_of(one.kind), one.host, one.port),
                    choose_account_proxy{at}, icon::dot{proxy_colour(one.name)}, key.current == one.name);
  };
  return c::many(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                 std::views::iota(-1, static_cast<int>(key.all.size())) | std::views::transform(row) | std::ranges::to<std::vector>());
}
using proxy_rows_t = decltype(proxy_rows(std::declval<const palette&>(), proxy_rows_key{}));
inline auto account_proxy_view(const palette& colours) {
  namespace c = skiff::compose;
  return c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      c::styled({.margin = {0.0f, 0.0f, 4.0f, 0.0f}}, section_title(colours, "PROXY")),
      c::projected<page_read>([](const page_read& now) {
        return now ? std::optional(proxy_rows_key{now->proxies, config::proxy_of(now->account)}) : std::nullopt;
      }, c::mount<proxy_rows_t, proxy_rows_key>([at = &colours](const proxy_rows_key& key) { return std::tuple{proxy_rows(*at, key)}; },
                                                {.fillX = true, .autoSize = scene::axes::kY})),
      c::styled({.margin = {8.0f, 0.0f, 0.0f, 0.0f}}, row_item(colours, "Manage proxies…", sends<request::manage_proxies>{}, icon::gear{})));
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
inline auto choose_one(const palette& colours) { return nodes::Text("Choose an account.", 15.0f, colours.dim); }
// The pages of the client's, and each protocol's own.
template <class List>
struct protocol_page_views;
template <class... Pages>
struct protocol_page_views<type_list<Pages...>> {
  using type = type_list<account_page_view_t<Pages>...>;
};
using editor_view_t = decltype(account_editor_view(std::declval<const ui_needs&>(), std::string()));
using privacy_view_t = decltype(account_privacy_view(std::declval<const palette&>(), std::string()));
using notifications_view_t = decltype(account_notifications_view(std::declval<const palette&>(), std::string()));
using chats_page_t = decltype(account_chats_page(std::declval<const ui_needs&>(), std::string(), config::theme_t{}));
using appearance_view_t = decltype(account_appearance_view(std::declval<const palette&>(), std::string()));
using proxy_view_t = decltype(account_proxy_view(std::declval<const palette&>()));
using choose_one_t = decltype(choose_one(std::declval<const palette&>()));
using detail_page_t = typename variant_of_types<typename joined<
    type_list<choose_one_t, editor_view_t, add_account_view_t, privacy_view_t, notifications_view_t, chats_page_t, appearance_view_t,
              proxy_view_t>,
    typename protocol_page_views<typename protocol_account_pages<protocols>::type>::type>::type>::type;
// An account's page: a protocol's made over the facts it starts from --
// read again as they move.
[[nodiscard]] inline detail_page_t page_made(const ui_needs& n, const panel_detail::account& chosen, const config::theme_t& theme) {
  const palette& colours = *n.colours;
  const std::string& address = chosen.address;
  const account_page_facts facts{.by = account_id{protocol_of(address), address}, .page = chosen.page};
  return spl::visit(spl::overloaded{
                        [&](account_page::connection) { return detail_page_t(account_editor_view(n, address)); },
                        [&](account_page::privacy) { return detail_page_t(account_privacy_view(colours, address)); },
                        [&](account_page::notifications) { return detail_page_t(account_notifications_view(colours, address)); },
                        [&](account_page::chats) { return detail_page_t(account_chats_page(n, address, theme)); },
                        [&](account_page::appearance) { return detail_page_t(account_appearance_view(colours, address)); },
                        [&](account_page::proxy) { return detail_page_t(account_proxy_view(colours)); },
                        // A protocol's own: as its page_view makes it.
                        [&]<class Page>(Page page) { return detail_page_t(page_view(page, colours, facts)); }},
                    chosen.page);
}
[[nodiscard]] inline detail_page_t detail_made(const ui_needs& n, const detail_key& key) {
  return spl::visit(spl::overloaded{[&](panel_detail::none) { return detail_page_t(choose_one(*n.colours)); },
                                    [&](panel_detail::adding) { return detail_page_t(add_account_view(n)); },
                                    [&](const panel_detail::account& chosen) { return page_made(n, chosen, key.theme); }},
                    key.detail);
}
// It, coming in as it changes -- faded in, and slid in from the side the
// move came from: across the pane, as tall as what it holds.
inline auto detail_shown(const ui_needs& n, const detail_key& key) {
  namespace c = skiff::compose;
  return c::appearing_across(true, c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), detail_made(n, key)),
                             28.0f * key.toward, 200.0f);
}
using detail_shown_t = decltype(detail_shown(std::declval<const ui_needs&>(), detail_key{}));
// The page column: what the facts say is beside the list, in a scroll view
// -- a page taller than the window scrolls.
inline auto detail_column(const ui_needs& n) {
  namespace c = skiff::compose;
  return c::column(c::vbox(0.0f, {.fillY = true, .grow = scene::axes::kX, .padding = {24.0f, 28.0f, 24.0f, 28.0f}}),
      c::styled({.fill = true}, nodes::ScrollContainer(c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
          c::projected<panel_read>([](const panel_read& now) { return now ? std::optional(detail_key{now->detail, now->theme, now->toward}) : std::nullopt; },
              c::mount<detail_shown_t, detail_key>(
                  [at = &n](const detail_key& key) { return std::tuple{detail_shown(*at, key)}; },
                  {.fillX = true, .autoSize = scene::axes::kY}))))));
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
inline auto side_made(const palette& colours, const side_key& key) {
  namespace c = skiff::compose;
  using side_t = spl::variant<accounts_side_t, pages_side_t>;
  return c::appearing_across(true, c::column(c::vbox(0.0f, {.fill = true}),
                                             key.account ? side_t(pages_side(colours)) : side_t(accounts_side(colours))),
                             36.0f * key.toward, 220.0f);
}
using side_made_t = decltype(side_made(std::declval<const palette&>(), side_key{}));
inline auto side_column(const palette& colours) {
  namespace c = skiff::compose;
  return c::spec_for<panel_read>([](const panel_read& now) {
    const bool narrow = now && now->narrow;
    return scene::Spec{.fillX = narrow, .fillY = true, .width = narrow ? 0.0f : kListWidth};
  }, c::column(c::vbox(0.0f, {.fillY = true, .background = colours.sidebar}),
      c::projected<panel_read>([](const panel_read& now) { return side_key{chosen_of(now).has_value(), now ? now->toward : 1.0f}; },
          c::mount<side_made_t, side_key>([at = &colours](const side_key& key) { return std::tuple{side_made(*at, key)}; },
                                          {.fillX = true, .grow = scene::axes::kY})),
      c::shown_for<panel_read>([](const panel_read& now) { return now && now->note.has_value(); },
          c::text_for<panel_read>([](const panel_read& now) { return now ? now->note.value_or("") : std::string(); },
              c::styled({.fillX = true, .margin = scene::Margin::all(8.0f)}, wrapped(nodes::Text("", 13.0f, colours.error)))))));
}
// The header: ← back -- from a page to the list, narrow; from an account to
// the list; from the list to the chats -- and the account chosen's address,
// or Accounts.
inline auto panel_header(const palette& colours) {
  namespace c = skiff::compose;
  return c::row(c::hbox(12.0f, {.fillX = true, .height = kPageHeaderHeight, .padding = {0.0f, 10.0f, 0.0f, 10.0f}}),
      c::styled({.alignSelf = scene::align::kMiddle}, icon_button(colours, icon::back{}, sends<request::accounts_back>{})),
      c::text_for<panel_read>([](const panel_read& now) { return chosen_of(now).value_or("Accounts"); },
          c::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle}, elided(nodes::Text("", 17.0f, colours.text, true)))));
}
// The accounts: the header over the list down the side and, beside it, what
// is chosen. Narrow -- a phone's -- one column at a time, all of the width:
// the list (or a chosen one's pages), or the page chosen from them. Esc goes
// back as the arrow does.
inline auto accounts_view(const ui_needs& n) {
  namespace c = skiff::compose;
  const palette& colours = *n.colours;
  return closes_on_escape<sends<request::accounts_back>>(c::column(c::vbox(0.0f, {.fill = true}),
      panel_header(colours),
      c::row(c::hbox(0.0f, {.fillX = true, .grow = scene::axes::kY}),
             c::shown_for<panel_read>([](const panel_read& now) { return !now || !now->narrow || !now->detail_up; }, side_column(colours)),
             c::shown_for<panel_read>([](const panel_read& now) { return !now || !now->narrow || now->detail_up; },
                                      c::column(c::vbox(0.0f, {.fillY = true, .grow = scene::axes::kX}), detail_column(n))))));
}
using accounts_panel = decltype(accounts_view(std::declval<const ui_needs&>()));

}  // namespace mux::ui

