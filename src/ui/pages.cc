// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui.base:pages -- What a protocol builds its own pages of, under the
// window's settings and manage dialogs: a room's facts and tabs, their
// headings and editors, a switch's row, a profile's editor. Below the
// protocols' UI modules; room_settings and accounts, above them, hold the
// protocols' pages these build.
export module mux.ui.base:pages;

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
import skiff.widgets.pill;
import mux.core;
import mux.config;
import mux.protocols;
import skiff.widgets.textbox;
import :base;
import :icons;
import :avatars;
import :controls;
import :themes;
import :names;
import :forms;

export namespace mux::ui {

struct room_settings_facts {
  std::string id;  // the room's ID
  std::string name;
  std::string topic;
  std::optional<std::string> avatar;
  std::optional<std::string> alias;
  std::vector<std::string> other_aliases;
  bool encrypted = false;
  bool group = true;
  bool leavable = true;
  // Its protocol's part (Matrix: its rules and levels), for its pages.
  room_part_t theirs;
  // Its notifications, as chosen for it (muted: off).
  config::notify_choices notify;
  // Which of its room events it shows, as chosen for it: none chosen is as
  // its account's.
  std::optional<bool> events_all;
  // Whether it shows link previews, as chosen for it.
  std::optional<bool> typing;  // others told one is typing: its own choice
  std::optional<bool> previews;
  std::optional<bool> previews_direct;  // where its previews come from: its own choice
  // Whether it shows who has read up to where, as chosen for it.
  std::optional<bool> receipts;
  // How far a jump's search pages back in it, as chosen for it.
  std::optional<std::int64_t> jump_search;
  std::optional<config::room_event_kinds> event_kinds;
  // The user's own level, as the protocol fills it (manage_facts).
  std::int64_t mine = 0;
  // A room by its ID and name.
  struct named_room {
    std::string id;
    std::string name;
  };
  // The spaces it is in, as theirs list it: what a rule for their members
  // names.
  std::vector<named_room> parents;
  // A space's: the rooms and spaces it holds, and the account's others --
  // those that may be added to it.
  std::vector<named_room> children;
  std::vector<named_room> addable;
  std::vector<member> members;
  // A space: whether it holds spaces, and whether it is shown as a forum.
  bool space = false;
  bool holds_spaces = false;
  bool forum = false;
  bool hidden_from_home = false;  // a space whose rooms Home leaves out
  // The protocol the room is of: which tabs it has besides the client's own.
  protocol_t speaks;
  // Those whose level is not the default: Element's privileged users.
  struct person {
    std::string id;
    std::string name;
    std::int64_t level = 0;
  };
  std::vector<person> privileged;
  // The chat itself, its own choices kept by it in the model.
  conversation_id chat{};
};

// What a protocol's settings pages see of the dialog. A refresh is deferred
// until the dialog's next update, so a handler never replaces its own page.
struct room_settings_shown {
  room_settings_facts facts;
  const palette* colours_ = nullptr;
  const looks_shown* looks_ = nullptr;
  skiff::scene::Node* node = nullptr;
  bool again = false;
  void show_again() { again = true; if (node) node->markDamaged(); }
};

// Element's names for levels: 100 Admin, 50 Moderator, the default Default.
[[nodiscard]] inline std::string level_name(std::int64_t level, std::int64_t fallback) {
  if (level >= 100)
    return "Admin";
  if (level >= 50)
    return "Moderator";
  if (level == fallback)
    return "Default";
  return std::format("Custom level ({})", level);
}

// Which tabs a room's protocol has: a list of their types, its own -- the
// client's (General, Notifications, Appearance) every room has. A protocol's
// are what manage_tabs(state) lists, found by ADL in its folder (mux.ui.
// proto.<protocol>); each tab type has its title, its icon and its page by
// overloads of its own (tab_title, tab_icon, page_type). Asked where the
// dialog is made -- a template on Actions, made in the program, which
// imports every protocol's. A protocol with none: the default, no tabs.
template <class... Tabs>
struct manage_tab_list {};
namespace manage_defaults {
constexpr void manage_attach(auto&, room_settings_shown&) {}
constexpr void manage_page_placed(auto&, auto&) {}
constexpr void manage_preserve(auto&) {}
constexpr manage_tab_list<> manage_tabs(const auto&) { return {}; }
constexpr bool tab_visible(const auto&, const room_settings_facts&) { return true; }
// And what a protocol keeps while its pages are made again: none by default.
struct no_part {};
constexpr type_tag<no_part> manage_part_type(const auto&) { return {}; }
// A room avatar being sent: what a part shows of it, where it shows any.
constexpr void avatar_sending(auto&) {}
}  // namespace manage_defaults
// Asked of a state type the caller names: dependent on it, so found where
// the dialog is made, not here.
template <class State>
constexpr auto manage_tabs_of(const State& state) {
  using manage_defaults::manage_tabs;
  return manage_tabs(state);
}
template <class State>
constexpr auto manage_part_of(const State& state) {
  using manage_defaults::manage_part_type;
  return manage_part_type(state);
}
template <class... Tabs, class Tab>
[[nodiscard]] constexpr bool lists(manage_tab_list<Tabs...>, type_tag<Tab>) {
  struct all : type_tag<Tabs>... {};
  return std::derived_from<all, type_tag<Tab>>;
}

// The client's tabs, every room's.
namespace settings_tab {
struct general {};        // the room's events, receipts, previews, typing, Home, leaving
struct notifications {};
struct looks {};
constexpr std::string_view tab_title(general) { return "General"; }
constexpr std::string_view tab_title(notifications) { return "Notifications"; }
constexpr std::string_view tab_title(looks) { return "Appearance"; }
inline icon_t tab_icon(general) { return icon::gear{}; }
inline icon_t tab_icon(notifications) { return icon::bell{}; }
inline icon_t tab_icon(looks) { return icon::eye{}; }
}  // namespace settings_tab

template <class Tabs>
struct tab_types;
template <class... Tabs>
struct tab_types<manage_tab_list<Tabs...>> {
  using type = type_list<Tabs...>;
};
template <class Rule, class Variant>
[[nodiscard]] bool is_rule(const Variant& now) {
  return spl::visit(spl::overloaded{[](const Rule&) { return true; }, [](const auto&) { return false; }}, now);
}

namespace manage_defaults {
constexpr settings_tab::general manage_initial_tab(const auto&) { return {}; }
constexpr std::string_view manage_preferences_title(const auto&) { return "General"; }
}
template <class State> constexpr auto initial_manage_tab(const State& state) {
  using manage_defaults::manage_initial_tab;
  return manage_initial_tab(state);
}
template <class State> constexpr auto preferences_title(const State& state) {
  using manage_defaults::manage_preferences_title;
  return manage_preferences_title(state);
}

// A heading over a tab, and over a part of one, as Element's.
inline nodes::Text tab_heading(const palette& colours, std::string text) {
  nodes::Text out(std::move(text), 20.0f, colours.text, true);
  out.apply({.margin = {0.0f, 0.0f, 12.0f, 0.0f}});
  return out;
}
inline nodes::Text part_heading(const palette& colours, std::string text) {
  nodes::Text out(std::move(text), 15.0f, colours.text, true);
  out.apply({.margin = {18.0f, 0.0f, 4.0f, 0.0f}});
  return out;
}
inline nodes::Text explained(const palette& colours, std::string text) {
  nodes::Text out(std::move(text), 13.0f, colours.dim);
  out.setWrapped(true);
  out.apply({.fillX = true, .margin = {2.0f, 0.0f, 6.0f, 0.0f}});
  return out;
}

// Leaving a space, as Element's LeaveSpaceDialog: the rooms of it one is in
// left with it -- none of them, all, or those chosen, each by a switch.
struct leave_space_facts {
  conversation_id space;
  std::string name;
  std::vector<room_settings_facts::named_room> rooms;  // its rooms one is in
};
namespace leave_choice {
struct none { friend bool operator==(none, none) = default; };
struct all { friend bool operator==(all, all) = default; };
struct some { friend bool operator==(some, some) = default; };
}
using leave_choice_t = spl::variant<leave_choice::none, leave_choice::all, leave_choice::some>;
struct leave_draft {
  leave_choice_t choice = leave_choice::none{};
  std::set<std::string> chosen;
};
struct flip_leave_room { std::string room; };
struct submit_leave_space {};
struct leave_events {
  leave_space_facts facts;
  auto on(const flip_leave_room& event, const leave_draft& draft) const {
    auto chosen = draft.chosen;
    if (!chosen.erase(event.room))
      chosen.insert(event.room);
    return skiff::model::over<skiff::model::Field<&leave_draft::chosen>>(skiff::model::setTo(std::move(chosen)));
  }
  auto on(submit_leave_space, const leave_draft& draft) const {
    auto leaving = spl::visit(spl::overloaded{
        [](leave_choice::none) { return std::vector<std::string>{}; },
        [&](leave_choice::all) {
          return facts.rooms | std::views::transform([](const auto& room) { return room.id; }) | std::ranges::to<std::vector>();
        },
        [&](leave_choice::some) { return std::vector<std::string>(draft.chosen.begin(), draft.chosen.end()); }}, draft.choice);
    return skiff::model::Up{request::leave_space{facts.space, std::move(leaving)}};
  }
};
inline auto leave_room_row(const palette& colours, const room_settings_facts::named_room& room) {
  return skiff::compose::row(skiff::compose::hbox(10.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {6.0f, 0.0f, 6.0f, 0.0f}}),
      skiff::compose::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle}, wrapped(nodes::Text(room.name, 14.0f, colours.text))),
      skiff::compose::onClick(flip_leave_room{room.id}, skiff::compose::projected<skiff::model::Field<&leave_draft::chosen>>(
          [id = room.id](const auto& chosen) { return chosen.contains(id); },
          skiff::compose::styled({.alignSelf = scene::align::kMiddle}, widgets::ToggleField<bool>(colours.widgets))), room.name));
}
inline auto leave_space_box(const palette& colours, const leave_space_facts& facts) {
  using choice = skiff::model::Field<&leave_draft::choice>;
  return skiff::compose::local<leave_draft>(leave_events{facts}, skiff::compose::column(
      skiff::compose::vbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {20.0f, 22.0f, 20.0f, 22.0f}}),
      nodes::Text("Leave " + facts.name, 17.0f, colours.text, true),
      explained(colours, facts.rooms.empty() ? "You are in none of its rooms." : "Would you like to leave the rooms in this space too?"),
      skiff::compose::visible(!facts.rooms.empty(), skiff::compose::bound<choice>(
          widgets::ChoiceRowField<leave_choice_t>(colours.widgets, "Don't leave any rooms", leave_choice::none{}))),
      skiff::compose::visible(!facts.rooms.empty(), skiff::compose::bound<choice>(
          widgets::ChoiceRowField<leave_choice_t>(colours.widgets, "Leave all rooms", leave_choice::all{}))),
      skiff::compose::visible(!facts.rooms.empty(), skiff::compose::bound<choice>(
          widgets::ChoiceRowField<leave_choice_t>(colours.widgets, "Leave some rooms", leave_choice::some{}))),
      skiff::compose::shown_for<choice>([](const auto& picked) { return picked == leave_choice_t(leave_choice::some{}); },
          skiff::compose::many(skiff::compose::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
              facts.rooms | std::views::transform([&](const auto& room) { return leave_room_row(colours, room); }) | std::ranges::to<std::vector>())),
      skiff::compose::row(skiff::compose::justified(
          skiff::compose::hbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY, .margin = {8.0f, 0.0f, 0.0f, 0.0f}}), nodes::justify::end{}),
          skiff::compose::styled({.width = 130.0f, .height = 36.0f}, widgets::SendButton<request::close_leave_space>(colours.widgets, "Cancel", {})),
          skiff::compose::styled({.width = 130.0f, .height = 36.0f}, primary(widgets::SendButton<submit_leave_space>(colours.widgets, "Leave space", {}))))));
}
using leave_space_box_t = decltype(leave_space_box(std::declval<const palette&>(), std::declval<const leave_space_facts&>()));
inline dialog_look content_look(std::type_identity<leave_space_box_t>) { return {.size = dialog_size::fitting{440.0f}}; }
template <class Needs>
auto make_content(std::type_identity<leave_space_box_t>, const Needs& needs, const leave_space_facts& facts) {
  return leave_space_box(*needs.colours, facts);
}

// A chat's own choices, bound to them in the model: receipts, previews,
// where previews come from, typing sent -- Default as the level above.
inline auto chat_settings_view(const palette& colours, const conversation_id& chat) {
  using choices = config::chat_choices;
  const choice_level_t level = choice_level::chat{};
  return skiff::compose::scoped<choices>(
      skiff::compose::handlers(),
      skiff::compose::column(
          skiff::compose::vbox(6.0f, {.fillX = true, .autoSize = scene::axes::kY}),
          event_kinds_field<choices>(colours, level, proto::room_event_kinds_of(state_before(chat.account.speaks))),
          skiff::compose::bound<skiff::model::Field<&choices::receipts>>(show_hide_field<receipts_setting, std::optional<bool>, skiff::model::Field<&choices::receipts>>(colours, level)),
          skiff::compose::bound<skiff::model::Field<&choices::previews>>(
              show_hide_field<link_previews_setting, std::optional<bool>, skiff::model::Field<&choices::previews>>(colours, level)),
          skiff::compose::visible(proto::offers(state_before(chat.account.speaks), proto::feature::server_previews{}),
              skiff::compose::bound<skiff::model::Field<&choices::previews_direct>>(
                  show_hide_field<previews_direct_setting, std::optional<bool>, skiff::model::Field<&choices::previews_direct>>(colours, level))),
          skiff::compose::bound<skiff::model::Field<&choices::typing>>(show_hide_field<typing_setting, std::optional<bool>, skiff::model::Field<&choices::typing>>(colours, level)),
          skiff::compose::bound<skiff::model::Field<&choices::jump_search>>(jump_search_field<std::optional<std::int64_t>, skiff::model::Field<&choices::jump_search>>(colours, level))),
      chat);
}

// A chat's notifications on or off: off is muted -- the chat list's mute,
// the same -- and on, said apart from it; Default, neither. So it reads
// and sets the chat's choices whole.
struct chat_notify_change {
  std::optional<bool> on;
  config::chat_choices operator()(const config::chat_choices& now) const {
    auto next = now;
    next.muted = on == false;
    next.notify.on = on == true ? on : std::nullopt;
    return next;
  }
};
struct set_chat_notify {
  std::optional<bool> on;
  using Answer = skiff::bind::Own<chat_notify_change>;
  Answer operator()() const { return skiff::bind::own(chat_notify_change{on}); }
};
inline auto chat_on_field(const palette& colours) {
  namespace c = skiff::compose;
  using choices = config::chat_choices;
  const auto shown = [](const choices& now) { return now.muted ? std::optional<bool>(false) : now.notify.on; };
  return c::scoped<choices>(c::handlers(), c::row(setting_row(),
      setting_label(nodes::Text(std::string(notify_on_setting::label), 14.0f, colours.text)),
      setting_button<choices>(colours, "Default", 70.0f,
          [shown](const choices& now) { return !shown(now); }, set_chat_notify{std::nullopt}),
      setting_button<choices>(colours, std::string(notify_on_setting::yes), 70.0f,
          [shown](const choices& now) { return shown(now) == true; }, set_chat_notify{true}),
      setting_button<choices>(colours, std::string(notify_on_setting::no), 70.0f,
          [shown](const choices& now) { return shown(now) == false; }, set_chat_notify{false})));
}
// A chat's notification rows, bound to its own choices in the model.
inline auto chat_notify_view(const palette& colours, const conversation_id& chat) {
  using notify = config::notify_choices;
  using skiff::compose::bound;
  const choice_level_t level = choice_level::chat{};
  return skiff::compose::scoped<config::chat_choices>(
      skiff::compose::handlers(),
      skiff::compose::column(
          skiff::compose::vbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}),
          bound<config::chat_choices>(chat_on_field(colours)),
          bound<skiff::model::Field<&notify::mentions>>(show_hide_field<notify_mentions_setting, std::optional<bool>, skiff::model::Field<&notify::mentions>>(colours, level)),
          bound<skiff::model::Field<&notify::sound>>(show_hide_field<notify_sound_setting, std::optional<bool>, skiff::model::Field<&notify::sound>>(colours, level))),
      chat);
}

// Stable model-bound switches: keep the widget while its saved value changes.
template <auto Member>
inline auto space_choice_row(const palette& colours, std::string label, bool allowed = true) {
  namespace c = skiff::compose;
  return c::row(c::hbox(12.0f, {.fillX = true, .autoSize = scene::axes::kY,
                              .padding = {6.0f, 0.0f, 6.0f, 0.0f}, .disabled = !allowed}),
      c::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle},
                wrapped(nodes::Text(std::move(label), 14.0f, colours.text))),
      c::bound<skiff::model::Field<Member>>(c::styled({.alignSelf = scene::align::kMiddle},
                                                    widgets::ToggleField<bool>(colours.widgets))));
}
inline auto room_general_page(const palette& colours, const room_settings_facts& facts, bool has_page_header = false) {
  namespace c = skiff::compose;
  using choices = config::chat_choices;
  return c::column(c::vbox(6.0f, {.fillX = true, .autoSize = scene::axes::kY,
                                .padding = {0.0f, 28.0f, 24.0f, 12.0f}}),
      c::visible(!has_page_header, tab_heading(colours, spl::visit([](auto tag) { return std::string(preferences_title(state_of<decltype(tag)>{})); }, facts.speaks))),
      c::visible(!proto::room_event_kinds_of(state_before(facts.speaks)).empty(),
          explained(colours, "Room events shown in this room, for you: As above follows the inherited settings.")),
      chat_settings_view(colours, facts.chat),
      c::visible(facts.space, c::scoped<choices>(c::handlers(),
          c::column(c::vbox(6.0f, {.fillX = true, .autoSize = scene::axes::kY}),
              part_heading(colours, "Shown as"),
              space_choice_row<&choices::forum>(colours, "One chat, its rooms as topics", !facts.holds_spaces),
              explained(colours, facts.holds_spaces ? "A space that holds spaces is shown as a space."
                  : "On: in the chat list as one chat; its rooms open inside it, as Telegram's topics."),
              c::shown_for<skiff::model::Field<&choices::forum>>([](bool forum) { return !forum; },
                  space_choice_row<&choices::hidden_from_home>(colours, "Its rooms not in Home"))), facts.chat)),
      c::visible(facts.leavable, part_heading(colours, "Leave room")),
      c::styled({.width = 130.0f, .height = 32.0f},
          c::visible(facts.leavable, button_for<sends<::mux::ui::request::leave_chat>>(colours.widgets, "Leave room", {}))));
}
using room_general_page_t = decltype(room_general_page(std::declval<const palette&>(), std::declval<const room_settings_facts&>()));

inline void preserve_manage_input(auto&) {}
template <class Page> requires requires(Page& page) { preserve_manage_page(page); }
void preserve_manage_input(Page& page) { preserve_manage_page(page); }

struct room_manage_style {
  palette colours;
  bool page_header = false;
  float navigation_width = 220.0f;
};
namespace manage_defaults {
inline room_manage_style manage_style(const auto&, const palette& colours) { return {colours}; }
inline bool manage_close_editor(const auto&, auto&) { return false; }
inline bool manage_editor_header(const auto&, const auto&) { return false; }
inline icon_t manage_avatar_icon(const auto&, const room_settings_facts&) { return icon::none{}; }
}
template <class State> auto manage_style_of(const State& state, const palette& colours) {
  using manage_defaults::manage_style;
  return manage_style(state, colours);
}

template <class State> auto manage_avatar_icon_of(const State& state, const room_settings_facts& facts) {
  using manage_defaults::manage_avatar_icon;
  return manage_avatar_icon(state, facts);
}

// A setting's row with a switch at its end: the text taking the room; the
// switch flips what `act` answers.
template <class Act>
auto switch_row(const palette& colours, std::string text, Act act) {
  namespace c = skiff::compose;
  std::string label = text;
  return c::row(c::hbox(16.0f, {.fillX = true, .height = kRowHeight, .padding = {0.0f, 20.0f, 0.0f, 20.0f}}),
                c::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle}, elided(nodes::Text(std::move(text), 15.0f, colours.text))),
                c::named(std::move(label), {}, c::styled({.alignSelf = scene::align::kMiddle}, toggle_for<Act>(colours.widgets, std::move(act)))));
}


template <class Policy>
struct profile_editor : skiff::compose::Stacked {
  struct choose { void operator()(std::size_t) const {} };
  struct save {
    profile_editor* page;
    using Answer = request::save_profile;
    Answer operator()() const {
      page->pending = true;
      page->parts.feedback.setText("Saving…");
      page->parts.feedback.setVisible(true);
      return {page->by, page->answer()};
    }
  };
  struct presence_save {
    profile_editor* page;
    using Answer = request::save_presence;
    Answer operator()() const {
      const auto choices = Policy::presences();
      return {page->by, {choices.at(page->parts.availability.current).second,
          page->parts.status.text().empty() ? std::nullopt : std::optional(page->parts.status.text())}};
    }
  };
  account_id by;
  const model* known;
  user_profile loaded;
  std::uint64_t result = 0;
  bool pending = false, initialized = false, presence_initialized = false;
  struct parts_t {
    nodes::Text title;
    legacy::field name, language, pronouns, biography;
    button_for<sends<request::pick_profile_banner>> banner;
    button_for<sends<request::remove_profile_banner>> remove_banner;
    widgets::Button<save> submit;
    nodes::Text feedback;
    choice_menu<choose> availability;
    legacy::field status;
    widgets::Button<presence_save> presence;
  } parts;
  profile_editor(const palette& colours, const ui_shared&, const config::account_t& saved, const model& now)
      : Stacked(skiff::compose::vbox(12.0f, {.fillX = true, .autoSize = scene::axes::kY})),
        by{protocol_of(config::address_of(saved)), config::address_of(saved)}, known(&now),
        parts{.title = nodes::Text("Profile", 20.0f, colours.text, true),
            .name = legacy::field(colours, "Display name", "Your name"),
            .language = legacy::field(colours, "Pronouns language", "en", "en"),
            .pronouns = legacy::field(colours, "Pronouns", "One set per line"),
            .biography = legacy::field(colours, "About you", "Your biography"),
            .banner = button_for<sends<request::pick_profile_banner>>(colours.widgets, "Choose banner…", {by}),
            .remove_banner = button_for<sends<request::remove_profile_banner>>(colours.widgets, "Remove banner", {by}),
            .submit = widgets::Button<save>(colours.widgets, "Save profile", {this}),
            .feedback = wrapped(nodes::Text("", 13.0f, colours.dim)),
            .availability = make_choice_menu(colours, "Presence", std::ranges::to<std::vector<std::string>>(
                std::views::transform(Policy::presences(), [](const auto& p) { return p.first; })), 0, choose{}),
            .status = legacy::field(colours, "Status", "What you are doing"),
            .presence = widgets::Button<presence_save>(colours.widgets, "Set presence", {this})} {
    parts.pronouns.multi_line(3); parts.biography.multi_line(6);
    parts.banner.setVisible(Policy::banners); parts.remove_banner.setVisible(Policy::banners);
    std::ranges::for_each(std::array<scene::Node*, 4>{&parts.banner, &parts.remove_banner, &parts.submit, &parts.presence},
        [](auto* node) { node->apply({.fillX = true, .height = 36.0f}); });
    parts.feedback.setVisible(false);
    if (const auto* account = known->accounts().find(by)) result = account->profile_result;
    reload();
  }
  request::refresh_profile asked_as_it_opens() const { return {by}; }
  static std::vector<std::string> lines_of(std::string_view text) {
    return std::ranges::to<std::vector<std::string>>(std::views::transform(
        std::views::filter(std::views::split(text, '\n'), [](const auto& line) { return !std::ranges::empty(line); }),
        [](const auto& line) { return std::ranges::to<std::string>(line); }));
  }
  user_profile answer() const {
    auto out = loaded;
    out.name = parts.name.text();
    // Editing one language retains the other language representations.
    const auto same_language = [&](const auto& p) { return p.language == parts.language.text(); };
    const auto existing = std::ranges::to<std::vector<std::string>>(
        std::views::transform(std::views::filter(out.details.pronouns, same_language), &mux::pronouns::summary));
    const auto edited = lines_of(parts.pronouns.text());
    if (existing != edited) {
      const auto first = std::ranges::find_if(out.details.pronouns, same_language);
      const auto position = first - out.details.pronouns.begin();
      std::erase_if(out.details.pronouns, same_language);
      auto replacements = std::ranges::to<std::vector<mux::pronouns>>(std::views::transform(edited,
          [&](const auto& summary) { return mux::pronouns{parts.language.text(), summary}; }));
      out.details.pronouns.insert(out.details.pronouns.begin() + position, replacements.begin(), replacements.end());
    }
    if (parts.biography.text() != loaded.details.biography.plain)
      out.details.biography = {parts.biography.text(), {}};
    return out;
  }
  void reload() {
    const auto* account = known->accounts().find(by);
    if (!account) return;
    const auto profile = account->profiles.find(by.address);
    if (profile == account->profiles.end()) return;
    loaded = profile->second; initialized = true;
    parts.name.parts.box.setText(loaded.name.value_or(""));
    if (!loaded.details.pronouns.empty()) parts.language.parts.box.setText(loaded.details.pronouns.front().language);
    parts.pronouns.parts.box.setText(std::ranges::to<std::string>(std::views::join_with(
        std::views::transform(std::views::filter(loaded.details.pronouns,
            [&](const auto& p) { return p.language == parts.language.text(); }), &mux::pronouns::summary), std::string_view("\n"))));
    parts.biography.parts.box.setText(loaded.details.biography.plain);
  }
  bool wantsTick() const { return true; }
  void update(double) {
    // Pages move into the panel's variant; actions must point at that final address.
    parts.submit.action().page = this; parts.presence.action().page = this;
    const auto* account = known->accounts().find(by);
    if (account && account->profile_result != result) {
      result = account->profile_result;
      if (pending && !account->profile_error) reload();
      pending = false;
      parts.feedback.setText(account->profile_error.value_or("Profile saved."));
      parts.feedback.setVisible(true);
    }
    if (!initialized) reload();
    if (account) {
      if (!presence_initialized) {
        if (const auto current = account->presences.find(by.address); current != account->presences.end()) {
          const auto choices = Policy::presences();
          const auto selected = std::ranges::find(choices, current->second.state,
              [](const auto& choice) { return choice.second; });
          if (selected != choices.end()) parts.availability.read(selected - choices.begin());
          parts.status.parts.box.setText(current->second.status.value_or(""));
          presence_initialized = true;
        }
      }
      if (const auto current = account->profiles.find(by.address); current != account->profiles.end())
        loaded.details.banner = current->second.details.banner;
    }
    const auto current = answer();
    parts.submit.setEnabled(initialized && !pending && current != loaded &&
        (current.details.pronouns.empty() || !parts.language.text().empty()));
    parts.name.setDisabled(pending); parts.language.setDisabled(pending);
    parts.pronouns.setDisabled(pending); parts.biography.setDisabled(pending);
    parts.remove_banner.setEnabled(loaded.details.banner.has_value());
    parts.banner.setEnabled(!pending);
    parts.remove_banner.setEnabled(!pending && loaded.details.banner.has_value());
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

// Reactions are local compositions: inline pictures retain their own frame
// deadlines, and a click sends the original (unmodified) reaction key.
struct reaction_pictures {
  static std::optional<scene::PillPicture> pill(std::string_view) { return std::nullopt; }
  static const skia::Sp<skia::SkImage>* picture(std::string_view target) {
    if (const auto* moving = emoji_animations().at(target, animation_clock())) return moving;
    return emoji_images().find(target);
  }
  static bool animated(std::string_view target) { return emoji_animations().has(target); }
  static double wakeAt(std::string_view target) { return emoji_animations().next_frame_at(target, animation_clock()); }
};


struct trailer_facts {
  const palette& colours;
  const looks_shown& looks;
  const message& said;
  const conversation& in;
  std::string message_id;
};


[[nodiscard]] inline skiff::compose::Look list_dialog(float height) {
  return skiff::compose::vbox(
      0.0f,
      {.fillX = true, .height = height, .padding = {0.0f, 0.0f, 12.0f, 0.0f}});
}
template <class Rows> [[nodiscard]] auto dialog_list(Rows rows) {
  return skiff::compose::styled(
      {.fillX = true, .grow = scene::axes::kY},
      nodes::ScrollContainer<Rows>(skiff::compose::styled(
          {.fillX = true, .autoSize = scene::axes::kY}, std::move(rows))));
}

// A chat as the chats hold it now: none, where it is gone.
[[nodiscard]] inline const conversation& chat_or_none(const model* now, const conversation_id& in) {
  static const conversation none{};
  const conversation* found = now == nullptr ? nullptr : now->find(in);
  return found == nullptr ? none : *found;
}

// A member as a list shows them: their avatar, their name over how they
// are, their role; pressed, their info.
inline auto member_row(const palette& colours, const member& one, std::string how) {
  const std::string name = one.name.empty() ? one.id : one.name;
  return skiff::compose::recorded(skiff::compose::onClick(request::open_member_info{one.id}, skiff::compose::row(
      skiff::compose::hbox(12.0f, {.fillX = true, .height = 54.0f, .padding = {0.0f, 16.0f, 0.0f, 16.0f},
          .hoverBackground = colours.chosen}),
      legacy::avatar_mark(avatar_of::person{}, one.id, name, 40.0f), two_lines(colours, name, std::move(how), 14.0f, 4.0f),
      skiff::compose::visible(one.role.has_value(), skiff::compose::styled(
          {.alignSelf = scene::align::kStart, .margin = {10.0f, 0.0f, 0.0f, 0.0f}},
          widgets::Pill(one.role.value_or(""), {.plate = skia::colorSetARGB(255, 62, 52, 96),
              .text = skia::colorSetARGB(255, 190, 170, 250), .size = 12.0f, .height = 20.0f, .padX = 8.0f})))), name));
}
using member_row_t = decltype(member_row(std::declval<const palette&>(), std::declval<const member&>(), ""));

namespace legacy {
template <class Act>
struct switch_row : skiff::compose::Stacked {
  struct parts_t {
    nodes::Text label;
    toggle_for<Act> toggle;
  } parts;

  // Declared: the text taking the room, the switch at the end.
  switch_row(const palette& colours, std::string text, Act what)
      : Stacked(skiff::compose::hbox(16.0f, {.fillX = true, .height = kRowHeight, .padding = {0.0f, 20.0f, 0.0f, 20.0f}})),
        parts{.label = skiff::compose::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle}, elided(nodes::Text(std::move(text), 15.0f, colours.text))),
              .toggle = skiff::compose::styled({.alignSelf = scene::align::kMiddle}, toggle_for<Act>(colours.widgets, std::move(what)))} {}
};


}  // namespace legacy

}  // namespace mux::ui
