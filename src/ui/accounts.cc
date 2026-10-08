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
import :base;
import :icons;
import :avatars;
import :controls;
import :themes;
import :forms;
import :add_account;
import :info;

export namespace mux::ui {

// ---- the accounts -------------------------------------------------------------------

// What the model says of a saved account, in a few words, and whether that
// is a failure.
[[nodiscard]] inline std::pair<std::string, bool> state_of(const config::account_t& one, const model& now) {
  const std::string& address = config::address_of(one);
  if (!config::enabled_of(one))
    return {"off", false};
  const account* found = now.accounts().find(account_id{protocol_of(address), address});
  if (found == nullptr)
    return {"offline", false};
  bool failed = false;
  std::string said = spl::visit(spl::overloaded{[](const connection::offline&) { return std::string("offline"); },
                                           [](const connection::connecting&) { return std::string("connecting…"); },
                                           [](const connection::online&) { return std::string("online"); },
                                           [&failed](const connection::failed& why) {
                                             failed = true;
                                             return "failed: " + why.error;
                                           }},
                                found->state);
  return {std::move(said), failed};
}

// One account in the list: its address, protocol and state. A click shows
// its settings beside the list.
template <class Actions>
struct account_entry : skiff::compose::Stacked {
  // What its handlers ask for, returned.
  using Answer = ::mux::ui::request::select_account;
  std::string address;
  bool selected = false;
  struct parts_t {
    nodes::Text name;
    nodes::Text state;
  } parts;

  // Declared: its address over its protocol and state, on a plate lit
  // while it is the one chosen.
  account_entry(const ui_needs<Actions>& n, const config::account_t& saved, const model& now, bool is_selected)
      : Stacked(skiff::compose::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {7.0f, 16.0f, 7.0f, 16.0f},
                                            .background = n.colours->sidebar, .selectedBackground = n.colours->chosen, .selected = is_selected})),
        address(config::address_of(saved)),
        selected(is_selected),
        parts{.name = skiff::compose::styled({.fillX = true}, elided(nodes::Text(address, 15.0f, n.colours->text, true))),
              .state = skiff::compose::styled({.fillX = true}, elided(state_line(*n.colours, saved, now)))} {}
  // Its protocol and how it is: in the error colour where it failed.
  [[nodiscard]] static nodes::Text state_line(const palette& colours, const config::account_t& saved, const model& now) {
    const auto [how, failed] = state_of(saved, now);
    return nodes::Text(std::format("{} · {}", config::protocol_name(saved), how), 13.0f, failed ? colours.error : colours.dim);
  }

  [[nodiscard]] bool acceptsInput() const { return true; }
  std::optional<Answer> onClick(float, float) {
    return ::mux::ui::request::select_account{address};
  }
  [[nodiscard]] scene::Semantics semantics() const {
    scene::Semantics out;
    out.fRole = scene::semantic_role::list_item{};
    out.fLabel = address;
    out.fSelected = selected;
    out.fActions = {scene::semantic_action::focus{}, scene::semantic_action::activate{}};
    return out;
  }
};

// The chosen account: on or off, removed, and its own protocol's form.
template <class Actions>
struct account_editor : skiff::compose::Stacked {
  // Its address, then on or off and Remove, in a line.
  struct head_row : skiff::compose::Stacked {
    struct parts_t {
      nodes::Text heading;
      nodes::Text enabled_label;
      widgets::Toggle<flip_account<Actions>> enabled;
      widgets::Button<remove_account<Actions>> remove;
    } parts;
    head_row(const palette& colours, const config::account_t& saved)
        : Stacked(skiff::compose::hbox(10.0f, {.fillX = true, .autoSize = scene::axes::kY})),
          parts{.heading = skiff::compose::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle},
                                                  elided(nodes::Text(config::address_of(saved), 20.0f, colours.text, true))),
                .enabled_label = skiff::compose::styled({.alignSelf = scene::align::kMiddle}, nodes::Text("On", 13.0f, colours.dim)),
                .enabled = skiff::compose::styled({.alignSelf = scene::align::kMiddle},
                                                  on_now(widgets::Toggle<flip_account<Actions>>(colours.widgets, flip_account<Actions>{config::address_of(saved)}),
                                                         config::enabled_of(saved))),
                .remove = skiff::compose::styled({.width = 100.0f, .height = 32.0f},
                                                 widgets::Button<remove_account<Actions>>(colours.widgets, "Remove",
                                                                                          remove_account<Actions>{config::address_of(saved)}))} {}
  };
  // The colours its state is said in as it changes.
  const palette* colours_ = nullptr;
  struct parts_t {
    head_row head;
    nodes::Text state;
    account_form<Actions> form;
  } parts;

  account_editor(const ui_needs<Actions>& n, const config::account_t& saved)
      : Stacked(skiff::compose::vbox(6.0f, {.fill = true})),
        colours_(n.colours),
        parts{.head = head_row(*n.colours, saved),
              .state = skiff::compose::styled({.fillX = true, .margin = {0.0f, 0.0f, 14.0f, 0.0f}}, elided(nodes::Text("", 13.0f, n.colours->dim))),
              .form = form_of<Actions>(*n.colours, saved)} {}

  // What the model says of it now, kept current without touching the form.
  void show(const config::account_t& saved, const model& now) {
    const auto [how, failed] = state_of(saved, now);
    parts.state.setText(std::format("{} · {}", config::protocol_name(saved), how));
    parts.state.setColour(failed ? colours_->error : colours_->dim);
    parts.head.parts.enabled.setOn(config::enabled_of(saved));
  }

  void say(std::string text, bool error) {
    spl::visit([&](auto& one) { one.say(std::move(text), error); }, parts.form);
  }
};

// A line with a switch on its right: its text, and the switch.
template <class Act>
struct switch_row : skiff::compose::Stacked {
  struct parts_t {
    nodes::Text label;
    toggle_for<Act> toggle;
  } parts;

  // Declared: the text taking the room, the switch at the end.
  switch_row(const palette& colours, std::string text, Act what)
      : Stacked(skiff::compose::hbox(16.0f, {.fillX = true, .height = row_item<nothing>::kHeight, .padding = {0.0f, 20.0f, 0.0f, 20.0f}})),
        parts{.label = skiff::compose::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle}, elided(nodes::Text(std::move(text), 15.0f, colours.text))),
              .toggle = skiff::compose::styled({.alignSelf = scene::align::kMiddle}, toggle_for<Act>(colours.widgets, std::move(what)))} {}
};

// A page of an account's settings chosen from its list.
template <class Actions>
struct choose_account_page {
  using Answer = ::mux::ui::request::account_page;
  account_page_t page = account_page::connection{};
  ::mux::ui::request::account_page operator()() { return ::mux::ui::request::account_page{page}; }
};

// An account's pages, in place of the list of accounts once one is chosen: a
// line for each page of its settings, the one shown lit -- the client's, and
// after Chats its protocol's own.
template <class Actions> struct account_pages : skiff::compose::Stacked {
  using row = row_item<choose_account_page<Actions>>;
  // The colours its protocol's rows are made in, as they change.
  const palette* colours_ = nullptr;
  struct parts_t {
    row connection;
    row privacy;
    row notifications;
    row chats;
    std::vector<row> own;  // its protocol's, as it lists them
    row proxy;
  } parts;

  account_pages(const palette &colours)
      : Stacked(
            skiff::compose::vbox(0.0f, {.padding = {6.0f, 0.0f, 0.0f, 0.0f}})),
        colours_(&colours),
        parts{
            .connection = row(colours, "Connection",
                              {account_page::connection{}}, icon::sliders{}),
            .privacy =
                row(colours, "Privacy", {account_page::privacy{}}, icon::eye{}),
            .notifications = row(colours, "Notifications",
                                 {account_page::notifications{}}, icon::bell{}),
            .chats =
                row(colours, "Chats", {account_page::chats{}}, icon::people{}),
            .proxy =
                row(colours, "Proxy", {account_page::proxy{}}, icon::gear{})} {
    this->light(account_page::connection{});
  }
  // A protocol's pages: each its title and icon, by its own overloads.
  template <class... Pages>
  void add(proto::account_page_list<Pages...>) {
    (parts.own.emplace_back(*colours_, std::string(page_title(Pages{})), choose_account_page<Actions>{account_page_t{Pages{}}},
                            page_icon(Pages{})),
     ...);
  }
  // The rows of the chosen account's protocol.
  void show_for(const protocol_state_t& state) {
    parts.own.clear();
    spl::visit([&](const auto& now) { this->add(proto::account_pages_of(now)); }, state);
    this->invalidateLayout();
  }
  void light(const account_page_t& page) {
    const auto lit = [&](row& one) { one.set_lit(one.act.page.index() == page.index()); };
    std::ranges::for_each(std::array{&parts.connection, &parts.privacy, &parts.notifications, &parts.chats, &parts.proxy}, [&](row* one) { lit(*one); });
    std::ranges::for_each(parts.own, lit);
  }
};

// A section's title on a settings page, as Gajim sets them: small, bold, dim.
inline nodes::Text section_title(const palette& colours, std::string text) {
  return nodes::Text(std::move(text), 13.0f, colours.dim, true);
}
// A note under a section, as the settings' pages have them.
inline nodes::Text note_text(const palette& colours, std::string text) { return nodes::Text(std::move(text), 13.0f, colours.dim); }

// An account's Privacy page: whether it sends read receipts; whether it
// tells others one is typing, as every account's until chosen here, and a
// chat or space of it may choose again.
// An account's own choices in the model, bound under a scope at it: typing
// sent, on its Privacy page; receipts as faces, link previews and where
// they come from, on its Chats page.
inline auto account_typing_view(const palette& colours, std::string address) {
  return skiff::compose::scoped<config::account_t>(
      skiff::compose::handlers(),
      skiff::compose::bound<skiff::model::Field<&config::account_shared::send_typing>>(
          show_hide_field<typing_setting, std::optional<bool>>(colours, choice_level::account{})),
      std::move(address));
}
inline auto account_chats_view(const palette& colours, std::string address) {
  using shared = config::account_shared;
  using skiff::compose::bound;
  const choice_level_t level = choice_level::account{};
  return skiff::compose::scoped<config::account_t>(
      skiff::compose::handlers(),
      skiff::compose::column(
          skiff::compose::vbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}),
          bound<shared>(event_kinds_field<shared>(colours, level)),
          bound<skiff::model::Field<&shared::show_receipts>>(show_hide_field<receipts_setting, std::optional<bool>>(colours, level)),
          bound<skiff::model::Field<&shared::link_previews>>(show_hide_field<link_previews_setting, std::optional<bool>>(colours, level)),
          bound<skiff::model::Field<&shared::previews_direct>>(
              show_hide_field<previews_direct_setting, std::optional<bool>>(colours, level)),
          bound<skiff::model::Field<&shared::jump_search>>(jump_search_field<std::optional<std::int64_t>>(colours, level))),
      std::move(address));
}

template <class Actions> struct account_privacy : skiff::compose::Stacked {
  using receipts_row = switch_row<sends<::mux::ui::request::flip_account_receipts>>;
  using mentions_shared_row = switch_row<sends<::mux::ui::request::flip_account_mentions_shared>>;
  using mentions_sealed_row = switch_row<sends<::mux::ui::request::flip_account_mentions_sealed>>;
  struct parts_t {
    nodes::Text title;
    receipts_row receipts;
    decltype(account_typing_view(std::declval<const palette&>(), std::string())) settings;
    nodes::Text note;
    // The mentions read, shared with the account's other sessions; sealed
    // there -- where its protocol can (Matrix's).
    mentions_shared_row mentions_shared;
    mentions_sealed_row mentions_sealed;
    nodes::Text mentions_note;
  } parts;

  template <class... Rest>
  account_privacy(const ui_needs<Actions>& n, Rest&&... rest) : account_privacy(*n.colours, std::forward<Rest>(rest)...) {}
  account_privacy(
      const palette &colours, bool receipts_on, std::string address,
      std::optional<bool> events_all = std::nullopt,
      const std::optional<config::room_event_kinds> &kinds = std::nullopt,
      bool notify_on = true, bool notify_sound_on = true,
      std::optional<bool> faces_on = std::nullopt,
      std::optional<std::int64_t> jump_most = std::nullopt,
      std::optional<bool> previews_on = std::nullopt,
      std::optional<config::mentions_choice> mentions = std::nullopt)
      : Stacked(skiff::compose::vbox(8.0f, {.fill = true})),
        parts{.title = section_title(colours, "PRIVACY"),
              .receipts = receipts_row(colours, "Send read receipts", {}),
              .settings = account_typing_view(colours, std::move(address)),
              .note = wrapped(skiff::compose::styled(
                  {.fillX = true},
                  note_text(colours, "Off, the people you talk to through this "
                                     "account are not told when you have read "
                                     "their messages, or that you are typing. "
                                     "Theirs are still shown, and receipts are "
                                     "still kept here."))),
              .mentions_shared = mentions_shared_row(
                  colours, "Sync read mentions between sessions", {}),
              .mentions_sealed = mentions_sealed_row(
                  colours, "Encrypt them (with the recovery key)", {}),
              .mentions_note = wrapped(skiff::compose::styled(
                  {.fillX = true},
                  note_text(colours, "Which mentions you have read, kept with "
                                     "your account on its server, so "
                                     "your other sessions take them as read "
                                     "too. Encrypted, the server cannot "
                                     "read them; a session gets the key when "
                                     "it is restored with the "
                                     "recovery key.")))} {
    (void)events_all, (void)kinds, (void)faces_on, (void)jump_most, (void)previews_on, (void)notify_on, (void)notify_sound_on;

    parts.receipts.parts.toggle.setOnNow(receipts_on);

    for (scene::Node* one : std::initializer_list<scene::Node*>{&parts.mentions_shared, &parts.mentions_sealed, &parts.mentions_note})
      one->setVisible(mentions.has_value());
    if (mentions) {
      parts.mentions_shared.parts.toggle.setOnNow(mentions->shared);
      parts.mentions_sealed.parts.toggle.setOnNow(mentions->sealed);
      this->show_sealable(mentions->shared);
    }
  }
  void show(bool receipts_on) { parts.receipts.parts.toggle.setOn(receipts_on); }
  void show_mentions(config::mentions_choice now) {
    parts.mentions_shared.parts.toggle.setOn(now.shared);
    parts.mentions_sealed.parts.toggle.setOn(now.sealed);
    this->show_sealable(now.shared);
  }
  // Sealing says something only while they are shared.
  void show_sealable(bool shared) { parts.mentions_sealed.apply({.alpha = shared ? 1.0f : 0.4f, .disabled = !shared}); }
  void say(std::string, bool) {}
};

// An account's notification rows, bound to the account's own choices in the model, under a scope
// at the account.
inline auto account_notify_view(const palette& colours, std::string address) {
  using shared = config::account_shared;
  using skiff::compose::bound;
  const choice_level_t level = choice_level::account{};
  return skiff::compose::scoped<config::account_t>(
      skiff::compose::handlers(),
      skiff::compose::column(
          skiff::compose::vbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}),
          bound<skiff::model::Field<&shared::notify>>(show_hide_field<notify_on_setting, std::optional<bool>>(colours, level)),
          bound<skiff::model::Field<&shared::notify_mentions>>(show_hide_field<notify_mentions_setting, std::optional<bool>>(colours, level)),
          bound<skiff::model::Field<&shared::notify_name>>(show_hide_field<notify_name_setting, std::optional<bool>>(colours, level)),
          bound<skiff::model::Field<&shared::notify_text>>(show_hide_field<notify_text_setting, std::optional<bool>>(colours, level)),
          bound<skiff::model::Field<&shared::notify_sound>>(show_hide_field<notify_sound_setting, std::optional<bool>>(colours, level))),
      std::move(address));
}

// An account's Notifications page: whether what comes through it is told --
// a message, an invite -- and with sound; each chat of it may choose again
// in its own settings.
template <class Actions>
struct account_notifications : skiff::compose::Stacked {
  using settings_t = decltype(account_notify_view(std::declval<const palette&>(), std::string()));
  struct parts_t {
    nodes::Text title;
    settings_t settings;
    nodes::Text note;
  } parts;
  account_notifications(const ui_needs<Actions>& n, std::string address)
      : Stacked(skiff::compose::vbox(8.0f, {.fill = true})),
        parts{.title = section_title(*n.colours, "NOTIFICATIONS"),
              .settings = account_notify_view(*n.colours, std::move(address)),
              .note = skiff::compose::styled({.fillX = true, .margin = {10.0f, 0.0f, 0.0f, 0.0f}},
                                             wrapped(note_text(*n.colours, "For messages and invites that come through this account; Default is as the "
                                                                           "Notifications settings say. A space, and a chat, can choose again in its own "
                                                                           "settings.")))} {}
  void say(std::string, bool) {}
};

// An account's Chats page: how its chats are -- the room events they show,
// who has read up to where, link previews, how far a jump looks back, their
// looks, and its spaces in the bars. Each as every account's, until chosen
// here; a chat of it may choose again.
template <class Actions> struct account_chats : skiff::compose::Stacked {
  // Its colour chosen.
  struct set_colour {
    using Answer = std::optional<::mux::ui::request::set_account_colour>;
    std::optional<::mux::ui::request::set_account_colour> operator()(const config::accent_t& one) { return ::mux::ui::request::set_account_colour{one}; }
  };
  // Home without what its spaces hold -- but direct messages -- or as every
  // account's.
  struct pick_home {
    using Answer = std::tuple<::mux::ui::request::set_home_hides, ::mux::ui::request::set_home_direct>;
    Answer operator()(std::size_t index) const {
      const auto said = [&](bool on) { return index == 0 ? std::nullopt : std::optional<bool>(on); };
      return {::mux::ui::request::set_home_hides{choice_level::account{}, said(index >= 2)}, ::mux::ui::request::set_home_direct{choice_level::account{}, said(index == 3)}};
    }
  };
  struct parts_t {
    // Its colour, as only this page shows it: the strip of
    // its chats listed in other accounts' lists, unless they chose another.
    nodes::Text colour_title;
    accent_circles<set_colour> colours;
    switch_row<sends<::mux::ui::request::flip_account_strip>> strip;
    nodes::Text title;
    decltype(account_chats_view(std::declval<const palette&>(), std::string())) settings;
    nodes::Text looks_title;
    look_choices<Actions> looks;
    nodes::Text spaces_title;
    choice_menu<pick_home> home;
    spaces_choices<Actions> places;
  } parts;
  account_chats(const palette &colours, const looks_shown &looks,
                const ui_shared &shared, std::string address,
                const chat_choice_values &chats, std::optional<bool> home_hides,
                std::optional<bool> home_direct, const config::accent_t &colour,
                bool strip_on, const config::theme_t &theme)
      : Stacked(skiff::compose::vbox(
            8.0f, {.fillX = true, .autoSize = scene::axes::kY})),
        parts{
            .colour_title = section_title(colours, "COLOUR"),
            .colours = accent_circles<set_colour>({}, theme, false),
            .strip = switch_row<sends<::mux::ui::request::flip_account_strip>>(
                colours, "A strip on its chats in other lists", {}),
            .title = section_title(colours, "CHATS"),
            .settings = account_chats_view(colours, std::move(address)),
            .looks_title = section_title(colours, "LOOKS"),
            .looks =
                look_choices<Actions>(colours, looks, choice_level::account{}),
            .spaces_title = section_title(colours, "SPACES"),
            .home = choice_menu<pick_home>(
                colours, "Home",
                {"As above", "Every chat", "Without chats spaces hold",
                 "Without those and direct messages"},
                !home_hides                   ? 0
                : !*home_hides                ? 1
                : home_direct.value_or(false) ? 3
                                              : 2,
                pick_home{}),
            .places = spaces_choices<Actions>(colours, shared)} {
    for (nodes::Text* each : {&parts.title, &parts.looks_title, &parts.spaces_title})
      each->apply({.margin = {10.0f, 0.0f, 0.0f, 0.0f}});
    this->show_colour(colour, strip_on);
  }
  void show_colour(const config::accent_t& colour, bool strip_on) {
    parts.colours.show_chosen(colour);
    parts.strip.parts.toggle.setOn(strip_on);
  }
  void say(std::string, bool) {}
};

// A proxy profile chosen for the chosen account: -1 for none.
template <class Actions>
struct choose_account_proxy {
  using Answer = ::mux::ui::request::choose_account_proxy;
  int index = -1;
  ::mux::ui::request::choose_account_proxy operator()() { return ::mux::ui::request::choose_account_proxy{index}; }
};

// An account's Proxy page, as Gajim's: which of the program's proxy profiles
// it connects through, or none; and the way to the profiles themselves.
template <class Actions> struct account_proxy : skiff::compose::Stacked {
  using manage_row = row_item<sends<::mux::ui::request::manage_proxies>>;
  struct parts_t {
    nodes::Text title;
    std::vector<row_item<choose_account_proxy<Actions>>> choices;
    manage_row manage;
  } parts;

  account_proxy(const palette &colours,
                const std::vector<config::proxy_settings> &all,
                const std::optional<std::string> &current)
      : Stacked(skiff::compose::vbox(0.0f, {.fill = true})),
        parts{.title =
                  skiff::compose::styled({.margin = {0.0f, 0.0f, 4.0f, 0.0f}},
                                         section_title(colours, "PROXY")),
              .manage = skiff::compose::styled(
                  {.margin = {8.0f, 0.0f, 0.0f, 0.0f}},
                  manage_row(colours, "Manage proxies…", {}, icon::gear{}))} {
    auto& choices = parts.choices;

    // An empty place where the dots are, so the names line up.
    choices.emplace_back(colours, "No proxy", choose_account_proxy<Actions>{-1}, icon::dot{skia::colorSetARGB(0, 0, 0, 0)},
                         !current.has_value());
    for (std::size_t i = 0; i < all.size(); ++i)
      choices.emplace_back(colours, std::format("{} ({} {}:{})", all[i].name, config::label_of(all[i].kind),
                                       all[i].host, all[i].port),
                           choose_account_proxy<Actions>{static_cast<int>(i)}, icon::dot{proxy_colour(all[i].name)},
                           current && *current == all[i].name);
  }
  void show(bool) {}
  void say(std::string, bool) {}
};

// The saved accounts down the side, and the chosen one's settings beside
// them.
// What the accounts panel shows beside its list: nothing chosen, an
// account's pages, or an account being added.
namespace panel_detail {
struct none {};
struct account {
  std::string address;
  // What its form says as it is made again -- "Saved.", what went wrong.
  std::optional<std::string> said;
  bool error = false;
};
// Adding one: of the protocol chosen, its form, where one is.
struct adding {
  std::optional<protocol_t> speaks;
};
}  // namespace panel_detail
using panel_detail_t = spl::variant<panel_detail::none, panel_detail::account, panel_detail::adding>;
// The accounts panel open: what is beside its list, and a word said at its
// top as it opens, where there is one.
struct panel_facts {
  panel_detail_t detail = panel_detail::none{};
  std::optional<std::string> note;
};
template <class Actions>
struct accounts_panel : closes_on_escape<Actions, sends<::mux::ui::request::accounts_back>> {
  static constexpr int kTab = 2;
  static constexpr float kListWidth = 280.0f;

  std::optional<std::string> selected;
  // The proxy profiles, for adding an account through one.
  std::vector<config::proxy_settings> proxies;

  using actions_type = Actions;
  // A protocol's account pages, as the nodes its page_type makes of them.
  template <class List>
  struct page_nodes;
  template <class... Pages>
  struct page_nodes<type_list<Pages...>> {
    using type = type_list<typename decltype(page_type(Pages{}, type_tag<Actions>{}))::type...>;
  };
  // Its ← goes back from an account's pages to the list, and from the list
  // to the chats.
  using header_t = page_header<sends<::mux::ui::request::accounts_back>, sends<::mux::ui::request::accounts_back>>;
  // Under the header: the list down the side, and beside it what is chosen.
  struct body_row : skiff::compose::Stacked {
    struct side_column : skiff::compose::Stacked {
      using add_row = row_item<sends<::mux::ui::request::open_new_account>>;
      struct parts_t {
        add_row add;
        account_pages<Actions> pages;
        nodes::Text message;
        nodes::ScrollContainer<nodes::Flow<std::vector<account_entry<Actions>>>> list{
            nodes::Flow<std::vector<account_entry<Actions>>>({.spacingY = 0.0f, .wrap = false}, {})};
      } parts;
      add_row& add = parts.add;
      account_pages<Actions>& pages = parts.pages;
      nodes::Text& message = parts.message;
      decltype(parts_t::list)& list = parts.list;
      side_column(const palette &colours)
          : Stacked(
                skiff::compose::vbox(0.0f, {.fillY = true,
                                            .width = kListWidth,
                                            .background = colours.sidebar})),
            parts{.add = add_row(colours, "Add account", {}, icon::plus{}),
                  .pages = account_pages<Actions>(colours),
                  .message = nodes::Text("", 13.0f, colours.error)} {
        pages.setVisible(false);
        pages.apply({.fillX = true, .autoSize = scene::axes::kY});
        message.setWrapped(true);
        message.apply({.fillX = true, .margin = scene::Margin::all(8.0f)});
        list.apply({.fillX = true, .grow = scene::axes::kY});
        std::get<0>(list.fChildren).apply({.fillX = true, .autoSize = scene::axes::kY});
      }
    };
    // The client's pages, then each protocol's own: made by its page_type.
    using detail_t = typename variant_of_types<typename joined<
        type_list<nodes::Text, account_editor<Actions>, add_account_pane<Actions>, account_privacy<Actions>, account_proxy<Actions>,
                  account_chats<Actions>, account_notifications<Actions>>,
        typename page_nodes<typename protocol_account_pages<protocols>::type>::type>::type>::type;
    struct detail_column : skiff::compose::Stacked {
      // No account chosen, or the chosen one, or adding one.
      struct parts_t {
        // In a scroll view: a page taller than the window scrolls.
        nodes::ScrollContainer<detail_t> scroll;
      } parts;
      explicit detail_column(const palette &colours)
          : Stacked(skiff::compose::vbox(
                0.0f, {.fillY = true,
                       .grow = scene::axes::kX,
                       .padding = {24.0f, 28.0f, 24.0f, 28.0f}})),
            parts{.scroll = skiff::compose::styled(
                      {.fill = true},
                      nodes::ScrollContainer<detail_t>(
                          detail_t{std::in_place_index<0>, "Choose an account.",
                                   15.0f, colours.dim}))} {}
    };
    struct parts_t {
      side_column side;
      detail_column main;
    } parts;
    body_row(const palette &colours)
        : Stacked(skiff::compose::hbox(
              0.0f, {.fillX = true, .grow = scene::axes::kY})),
          parts{.side = side_column(colours), .main = detail_column(colours)} {}
    // Narrow -- a phone's -- one column at a time, all of the width, side by
    // side as wide: the accounts (or a chosen one's pages, its settings), or
    // the page chosen from them; the header's arrow a step back. Nothing of
    // either column's sizing is changed -- the side as wide as the panel, the
    // page growing into what the side leaves: flipped from column to row and
    // back, the sizes clashed, the page had no width, and its buttons took no
    // press.
    bool narrow = false;
    bool detail_up = false;
    void show_columns() {
      parts.side.setVisible(!narrow || !detail_up);
      parts.main.setVisible(!narrow || detail_up);
      this->invalidateLayout();
    }
    void layoutChildren() {
      if (const bool now = fState.contentBox().width() < 600.0f; now != narrow) {
        narrow = now;
        this->show_columns();
      }
      parts.side.apply({.width = narrow ? fState.contentBox().width() : kListWidth});
      this->nodes::Stack::layoutChildren();
    }
  };
  struct parts_t {
    header_t header;
    body_row body;
  } parts;
  header_t& header = parts.header;
  typename body_row::side_column::add_row& add = parts.body.parts.side.add;
  account_pages<Actions>& pages = parts.body.parts.side.pages;
  nodes::Text& message = parts.body.parts.side.message;
  decltype(body_row::side_column::parts_t::list)& list = parts.body.parts.side.list;
  typename body_row::detail_t& detail = std::get<0>(parts.body.parts.main.parts.scroll.fChildren);
  // The page shown: across the pane, as tall as what it holds -- scrolled
  // from its top.
  void fit_detail() {
    spl::visit(
        [](auto& one) {
          one.fState.apply({.relativeSize = scene::axes::kX});
          one.fState.apply({.autoSize = scene::axes::kY});
        },
        detail);
    parts.body.parts.main.parts.scroll.scrollToStart();
  }

  // What is beside the list coming in when another is chosen, fading in.
  skiff::paint::Tween swap{1.0f, 200.0f, skiff::paint::movement::subtle{}};
  void begin_swap() {
    swap.jump(0.0f);
    swap.setTarget(1.0f);
    this->fade();
  }
  // The page coming in: faded in, and slid in from the side it comes from.
  float swap_side = 1.0f;
  void fade() {
    const float value = swap.value();
    const float shift = (1.0f - value) * 28.0f * swap_side;
    spl::visit(
        [&](auto& one) {
          one.fState.setAlpha(value);
          one.apply({.shiftX = shift});
        },
        detail);
  }
  // The side column's switch, the accounts' list for an account's pages and
  // back: the one coming in slid in and faded in, as the page beside it.
  skiff::paint::Tween side_swap{1.0f, 220.0f, skiff::paint::movement::subtle{}};
  float side_from = 1.0f;
  void slide_side() {
    const float value = side_swap.value();
    const float shift = (1.0f - value) * 36.0f * side_from;
    const std::array<scene::Node*, 2> shown = pages.visible() ? std::array<scene::Node*, 2>{&pages, nullptr}
                                                               : std::array<scene::Node*, 2>{&list, &add};
    for (scene::Node* each : std::views::filter(shown, [](scene::Node* one) { return one != nullptr; })) {
      each->fState.setAlpha(value);
      each->apply({.shiftX = shift});
    }
  }
  [[nodiscard]] bool settling() const { return swap.moving() || side_swap.moving(); }
  void update(double now_ms) {
    if (swap.step(now_ms))
      this->fade();
    if (side_swap.step(now_ms))
      this->slide_side();
  }

  // What it was handed, for the panes it makes.
  ui_needs<Actions> needs_;
  accounts_panel(const ui_needs<Actions>& n)
      : closes_on_escape<Actions, sends<::mux::ui::request::accounts_back>>(),
        parts{.header = header_t(*n.colours, "Accounts", {}, {}, true, false), .body = body_row(*n.colours)},
        needs_(n) {
    this->fState.apply({.fill = true});
  }

  // The saved accounts, with what the model says of each; the chosen one's
  // form is kept as it is, typing and all.
  template <std::ranges::input_range Saved>
  void show(const Saved& saved, const model& now) {
    auto& entries = std::get<0>(std::get<0>(list.fChildren).fChildren);
    entries.clear();
    const config::account_t* chosen = nullptr;
    for (const config::account_t& one : saved) {
      const bool is_it = selected && config::address_of(one) == *selected;
      if (is_it)
        chosen = &one;
      entries.emplace_back(needs_, one, now, is_it);
    }
    if (chosen) {
      if (auto* up = this->editor())
        up->show(*chosen, now);
    } else if (!this->adding()) {
      // Nothing to show beside the list: with no account at all, adding one.
      selected.reset();
      if (std::ranges::empty(saved))
        this->show_adding();
      else
        detail.template emplace<0>("Choose an account.", 15.0f, needs_.colours->dim);
    }
    this->invalidateLayout();
  }

  // One of the chosen account's pages beside the list, brought up afresh.
  // A page that asks the program for something as it opens (its sessions,
  // the server's) says so: what it asks given to `asked`.
  template <class Page, class Asked>
    requires requires(Page& p) { p.asked_as_it_opens(); }
  static void opened(Page& page, Asked& asked) {
    asked(page.asked_as_it_opens());
  }
  template <class Page, class Asked>
  static void opened(Page&, Asked&) {}
  void show_page(const account_page_t& page, const config::account_t& one, const model& now,
                 const std::vector<config::proxy_settings>& proxies = {}, const config::theme_t& theme = config::theme_t{}) {
    const auto nothing = [](const auto&) {};
    this->show_page(page, one, now, proxies, theme, nothing);
  }
  template <class Asked>
  void show_page(const account_page_t& page, const config::account_t& one, const model& now,
                 const std::vector<config::proxy_settings>& proxies, const config::theme_t& theme, const Asked& asked) {
    pages.light(page);
    this->show_detail(true);
    spl::visit(
        spl::overloaded{
            [&](account_page::connection) {
              detail.template emplace<1>(needs_, one);
              spl::get<1>(detail).show(one, now);
            },
            [&](account_page::privacy) {
              detail.template emplace<3>(needs_, config::read_receipts_of(one), config::address_of(one),
                                           config::room_events_of(one), config::room_event_kinds_of(one),
                                           config::notify_of(one).value_or(true), config::notify_sound_of(one).value_or(true),
                                           config::show_receipts_of(one), config::jump_search_of(one),
                                           config::link_previews_of(one), config::mentions_choice_of(one));
            },
            [&](account_page::notifications) {
              detail.template emplace<6>(needs_, config::address_of(one));
            },
            [&](account_page::chats) {
              detail.template emplace<5>(*needs_.colours, *needs_.looks, *needs_.shared, config::address_of(one),
                                         chat_choice_values{.events_all = config::room_events_of(one),
                                                            .event_kinds = config::room_event_kinds_of(one),
                                                            .receipts = config::show_receipts_of(one),
                                                            .previews = config::link_previews_of(one),
                                                            .previews_direct = config::previews_direct_of(one),
                                                            .jump_search = config::jump_search_of(one)},
                                         config::home_hides_of(one), config::home_direct_of(one), config::colour_of(one),
                                         config::strip_of(one), theme);
            },
            [&](account_page::proxy) { detail.template emplace<4>(*needs_.colours, proxies, config::proxy_of(one)); },
            // A protocol's own: its node, made for the program's actions.
            [&]<class Page>(Page) {
              auto& made = detail.template emplace<typename decltype(page_type(Page{}, type_tag<Actions>{}))::type>(*needs_.colours,
                                                                                                                       *needs_.shared, one, now);
              opened(made, asked);
            }},
        page);
    this->fit_detail();
    this->begin_swap();
    this->invalidateLayout();
  }
  [[nodiscard]] account_privacy<Actions>* privacy() {
    return spl::visit(spl::overloaded{[](account_privacy<Actions>& one) { return &one; },
                                 [](auto&) -> account_privacy<Actions>* { return nullptr; }},
                      detail);
  }
  [[nodiscard]] account_chats<Actions>* chats_page() {
    return spl::visit(spl::overloaded{[](account_chats<Actions>& one) { return &one; },
                                            [](auto&) -> account_chats<Actions>* { return nullptr; }},
                         detail);
  }
  // The page shown, where it is one of this type: a protocol's own, as the
  // program tells it what its protocol's client says.
  // Something told to the page shown, where it takes it: f(page), where
  // that is a call.
  template <class F, class Page>
    requires std::invocable<F&, Page&>
  static void tell(F& f, Page& page) {
    f(page);
  }
  template <class F, class Page>
  static void tell(F&, Page&) {}
  template <class F>
  void tell_shown(F f) {
    spl::visit([&](auto& page) { tell(f, page); }, detail);
  }
  template <class Node>
  [[nodiscard]] Node* shown_page() {
    return spl::visit(spl::overloaded{[](Node& one) { return &one; }, [](auto&) -> Node* { return nullptr; }}, detail);
  }
  [[nodiscard]] account_proxy<Actions>* proxy() {
    return spl::visit(spl::overloaded{[](account_proxy<Actions>& one) { return &one; },
                                 [](auto&) -> account_proxy<Actions>* { return nullptr; }},
                      detail);
  }
  [[nodiscard]] bool pages_open() const { return pages.visible(); }

  void select(const config::account_t& one, const model& now) {
    add.set_lit(false);
    selected = config::address_of(one);
    const std::string& address = config::address_of(one);
    pages.show_for(protocol_state_of(*needs_.shared, account_id{protocol_of(address), address}));
    this->show_pages(true);
    this->show_page(account_page::connection{}, one, now);
    // Narrow: its settings -- its pages -- first, not the first of them.
    this->show_detail(false);
  }
  // Narrow, the page beside the list in its place, or the list back.
  void show_detail(bool up) {
    parts.body.detail_up = up;
    parts.body.show_columns();
    this->invalidateLayout();
  }
  // Narrow, a step back of its own before the list's: none -- the arrow
  // goes from an account's settings to the list, as it does wide.
  [[nodiscard]] bool step_back() {
    if (!parts.body.narrow || !parts.body.detail_up)
      return false;
    this->show_detail(false);  // from a page to the account's pages
    return true;
  }

  // Adding an account, beside the list.
  // The account list, or the chosen account's pages, down the side.
  void show_pages(bool shown) {
    // Into an account: its pages come in from the right, and the page beside
    // them with them; back: the list comes in from the left.
    if (shown != pages.visible()) {
      side_from = shown ? 1.0f : -1.0f;
      swap_side = side_from;
      side_swap.jump(0.0f);
      side_swap.setTarget(1.0f);
    }
    pages.setVisible(shown);
    add.setVisible(!shown);
    list.setVisible(!shown);
    this->slide_side();
    header.parts.title.setText(shown && selected ? *selected : std::string("Accounts"));
    this->invalidateLayout();
  }
  // Back to the list of accounts, nothing chosen.
  void close_pages() {
    selected.reset();
    this->show_detail(false);
    this->show_pages(false);
    detail.template emplace<0>("Choose an account.", 15.0f, needs_.colours->dim);
    this->fit_detail();
    this->begin_swap();
  }

  // What is to be beside the list, as what is shown said it last: done by
  // the program as it brings the panel up to date, with what it has of the
  // accounts.
  std::optional<panel_detail_t> detail_due;
  void show_adding() {
    this->show_pages(false);
    selected.reset();
    add.set_lit(true);
    this->show_detail(true);
    detail.template emplace<2>(needs_, proxies);
    this->fit_detail();
    this->begin_swap();
    this->invalidateLayout();
  }

  [[nodiscard]] account_editor<Actions>* editor() {
    return spl::visit(spl::overloaded{[](account_editor<Actions>& one) { return &one; },
                                 [](auto&) -> account_editor<Actions>* { return nullptr; }},
                      detail);
  }
  [[nodiscard]] add_account_pane<Actions>* adding() {
    return spl::visit(spl::overloaded{[](add_account_pane<Actions>& one) { return &one; },
                                 [](auto&) -> add_account_pane<Actions>* { return nullptr; }},
                      detail);
  }
  // The account form up -- an editor's, or the pane's that adds one.
  [[nodiscard]] account_form<Actions>* form() {
    return spl::visit(spl::overloaded{[](account_editor<Actions>& one) { return &one.parts.form; },
                                 [](add_account_pane<Actions>& one) { return &one.parts.form; },
                                 [](auto&) -> account_form<Actions>* { return nullptr; }},
                      detail);
  }

  void say(std::string text) {
    message.setText(std::move(text));
    this->invalidateLayout();
  }

};

}  // namespace mux::ui
