// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:room_settings -- A room's settings, as Element's Room Settings
// dialog: its tabs down the left -- General, Security & Privacy, Roles &
// Permissions, Notifications, Advanced -- and the chosen one beside them,
// in Element's words. What the user's power level does not allow is not
// offered: shown as it is, not to be changed.
export module mux.ui:room_settings;

import std;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes.box;
import skiff.nodes.flow;
import skiff.nodes.icon;
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
import :base;
import :icons;
import :controls;
import :themes;
import :names;
import :forms;
import :info;

export namespace mux::ui {

// What the dialog shows of a room, as it was when it opened.
struct room_settings_facts {
  std::string id;  // the room's ID
  std::string name;
  std::string topic;
  std::optional<std::string> alias;
  std::vector<std::string> other_aliases;
  bool encrypted = false;
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
constexpr manage_tab_list<> manage_tabs(const auto&) { return {}; }
// And what a protocol keeps while its pages are made again: none by default.
struct no_part {};
constexpr type_tag<no_part> manage_part_type(const auto&) { return {}; }
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

// One of a choice, as Element's radio buttons: a ring, and a title over
// what it means.
template <class Act> struct radio_choice : pressable<skiff::compose::Stacked> {
  Act act;
  struct texts : skiff::compose::Stacked {
    struct parts_t {
      nodes::Text title;
      nodes::Text about;
    } parts;
    texts(const palette &colours, std::string title, std::string about)
        : Stacked(
              skiff::compose::vbox(2.0f, {.autoSize = scene::axes::kY,
                                          .grow = scene::axes::kX,
                                          .alignSelf = scene::align::kMiddle})),
          parts{
              .title = skiff::compose::styled(
                  {.fillX = true},
                  wrapped(nodes::Text(std::move(title), 14.0f, colours.text))),
              .about = skiff::compose::styled(
                  {.fillX = true},
                  wrapped(nodes::Text(std::move(about), 12.0f, colours.dim)))} {

      parts.about.setVisible(!parts.about.text().empty());
    }
  };
  struct parts_t {
    radio_mark ring;
    texts words;
  } parts;
  radio_choice(const palette &colours, std::string title, std::string about,
               Act what, bool on, bool allowed)
      : pressable<skiff::compose::Stacked>(
            skiff::compose::hbox(10.0f, {.fillX = true,
                                         .autoSize = scene::axes::kY,
                                         .padding = {6.0f, 6.0f, 6.0f, 6.0f},
                                         .cornerRadius = 6.0f,
                                         .hoverBackground = colours.chosen,
                                         .disabled = !allowed})),
        act(std::move(what)),
        parts{.ring = radio_mark(colours),
              .words = texts(colours, std::move(title), std::move(about))} {

    if (allowed)
      fState.setCursor(scene::cursor::hand{});
    parts.ring.set_on(on);
    if (!allowed)
      fState.setAlpha(0.55f);
  }
};

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

// A text to copy, as Element's "Internal room ID": the text, and a button.
struct copy_line : skiff::compose::Stacked {
  struct copy_it {
    std::string text;
    void operator()() const { skiff::scene::setClipboardText(text); }
  };
  struct parts_t {
    nodes::Text label;
    nodes::Text value;
    widgets::Button<copy_it> copy;
  } parts;
  copy_line(const palette &colours, std::string label, std::string value)
      : Stacked(
            skiff::compose::hbox(10.0f, {.fillX = true,
                                         .autoSize = scene::axes::kY,
                                         .padding = {4.0f, 0.0f, 4.0f, 0.0f}})),
        parts{
            .label = skiff::compose::styled(
                {.alignSelf = scene::align::kMiddle},
                nodes::Text(std::move(label), 14.0f, colours.dim)),
            .value = skiff::compose::styled(
                {.shrink = scene::axes::kX, .alignSelf = scene::align::kMiddle},
                elided(nodes::Text(value, 14.0f, colours.text))),
            .copy = skiff::compose::styled(
                {.width = 70.0f,
                 .height = 28.0f,
                 .alignSelf = scene::align::kMiddle},
                widgets::Button<copy_it>(colours.widgets, "Copy", {value}))} {}
};

// A chat's own choices, bound to them in the model: receipts, previews,
// where previews come from, typing sent -- Default as the level above.
inline auto chat_settings_view(const palette& colours, const conversation_id& chat) {
  using choices = config::chat_choices;
  const choice_level_t level = choice_level::chat{};
  return skiff::compose::scoped<choices>(
      skiff::compose::handlers(),
      skiff::compose::column(
          skiff::compose::vbox(6.0f, {.fillX = true, .autoSize = scene::axes::kY}),
          event_kinds_field<choices>(colours, level),
          skiff::compose::bound<skiff::model::Field<&choices::receipts>>(show_hide_field<receipts_setting, std::optional<bool>>(colours, level)),
          skiff::compose::bound<skiff::model::Field<&choices::previews>>(
              show_hide_field<link_previews_setting, std::optional<bool>>(colours, level)),
          skiff::compose::bound<skiff::model::Field<&choices::previews_direct>>(
              show_hide_field<previews_direct_setting, std::optional<bool>>(colours, level)),
          skiff::compose::bound<skiff::model::Field<&choices::typing>>(show_hide_field<typing_setting, std::optional<bool>>(colours, level)),
          skiff::compose::bound<skiff::model::Field<&choices::jump_search>>(jump_search_field<std::optional<std::int64_t>>(colours, level))),
      chat);
}

// A chat's notifications on or off: off is muted -- the chat list's mute,
// the same -- and on, said apart from it; Default, neither. So it reads
// and sets the chat's choices whole.
struct chat_on_field : show_hide_field<notify_on_setting, std::optional<bool>, config::chat_choices> {
  explicit chat_on_field(const palette& colours) : show_hide_field(colours, choice_level::chat{}) {}
  void read(const config::chat_choices& now) {
    show_hide_field::show_value(now.muted ? std::optional<bool>(false) : now.notify.on);
    const auto with = [&](std::optional<bool> on) {
      auto next = now;
      next.muted = on == false;
      next.notify.on = on == true ? on : std::nullopt;
      return next;
    };
    show_hide_field::set_nexts(with(std::nullopt), with(true), with(false));
  }
};
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
          bound<skiff::model::Field<&notify::mentions>>(show_hide_field<notify_mentions_setting, std::optional<bool>>(colours, level)),
          bound<skiff::model::Field<&notify::name>>(show_hide_field<notify_name_setting, std::optional<bool>>(colours, level)),
          bound<skiff::model::Field<&notify::text>>(show_hide_field<notify_text_setting, std::optional<bool>>(colours, level)),
          bound<skiff::model::Field<&notify::sound>>(show_hide_field<notify_sound_setting, std::optional<bool>>(colours, level))),
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
inline auto room_general_page(const palette& colours, const room_settings_facts& facts) {
  namespace c = skiff::compose;
  using choices = config::chat_choices;
  return c::column(c::vbox(6.0f, {.fillX = true, .autoSize = scene::axes::kY,
                                .padding = {0.0f, 28.0f, 24.0f, 12.0f}}),
      tab_heading(colours, "General"),
      explained(colours, "Room events shown in this room, for you: As above follows the inherited settings."),
      chat_settings_view(colours, facts.chat),
      c::visible(facts.space, c::scoped<choices>(c::handlers(),
          c::column(c::vbox(6.0f, {.fillX = true, .autoSize = scene::axes::kY}),
              part_heading(colours, "Shown as"),
              space_choice_row<&choices::forum>(colours, "One chat, its rooms as topics", !facts.holds_spaces),
              explained(colours, facts.holds_spaces ? "A space that holds spaces is shown as a space."
                  : "On: in the chat list as one chat; its rooms open inside it, as Telegram's topics."),
              c::shown_for<skiff::model::Field<&choices::forum>>([](bool forum) { return !forum; },
                  space_choice_row<&choices::hidden_from_home>(colours, "Its rooms not in Home"))), facts.chat)),
      part_heading(colours, "Leave room"),
      c::styled({.width = 130.0f, .height = 32.0f},
          button_for<sends<::mux::ui::request::leave_chat>>(colours.widgets, "Leave room", {})));
}
using room_general_page_t = decltype(room_general_page(std::declval<const palette&>(), std::declval<const room_settings_facts&>()));

template <class Actions> struct room_settings : skiff::compose::Stacked {
  // Child references and handlers require a fixed address.
  room_settings(const room_settings&) = delete;
  room_settings& operator=(const room_settings&) = delete;
  room_settings(room_settings&&) = delete;
  room_settings& operator=(room_settings&&) = delete;

  // The dialog it is shown in.
  [[nodiscard]] static dialog_look look_of_dialog() { return {.size = dialog_size::fixed{860.0f, 620.0f}}; }
  using actions_type = Actions;
  // The colours it and its pages are made in: what it was handed.
  const palette* colours_ = nullptr;
  // The looks shown: the program's.
  const looks_shown* looks_ = nullptr;
  room_settings_facts facts;

  // ---- the tabs and pages: the client's, then each protocol's -----------------
  template <class Tag>
  using tabs_of_t = decltype(manage_tabs_of(state_of<Tag>{}));
  template <class Tab>
  using page_of_t = typename decltype(page_type(Tab{}, type_tag<room_settings>{}))::type;
  template <class List>
  struct pages_of;
  template <class... Tabs>
  struct pages_of<type_list<Tabs...>> {
    using type = type_list<page_of_t<Tabs>...>;
  };
  template <class>
  struct protocol_lists;
  template <class... Tags>
  struct protocol_lists<protocol_list<Tags...>> {
    using tabs = typename joined<type_list<>, typename tab_types<tabs_of_t<Tags>>::type...>::type;
    using parts = std::tuple<typename decltype(manage_part_of(state_of<Tags>{}))::type...>;
  };
  using protocol_tabs = typename protocol_lists<protocols>::tabs;
  using protocol_pages = typename pages_of<protocol_tabs>::type;
  using settings_tab_t = typename variant_of_types<typename joined<
      type_list<settings_tab::general, settings_tab::notifications, settings_tab::looks>, protocol_tabs>::type>::type;

  // ---- what is asked ---------------------------------------------------------
  struct pick_tab {
    room_settings* box;
    settings_tab_t tab;
    void operator()() const {
      box->to_top = true;
      box->show_tab(tab);
    }
  };

  // ---- the tabs down the left ------------------------------------------------
  struct tab_row : pressable<skiff::compose::Stacked> {
    pick_tab act;
    struct parts_t {
      icon_mark_t mark;
      nodes::Text label;
    } parts;
    tab_row(const palette &colours, std::string text, icon_t icon,
            pick_tab what)
        : pressable<skiff::compose::Stacked>(skiff::compose::hbox(
              10.0f, {.fillX = true,
                      .height = 36.0f,
                      .padding = {0.0f, 12.0f, 0.0f, 12.0f},
                      .cornerRadius = 8.0f,
                      .hoverBackground = colours.chosen,
                      .selectedBackground = colours.chosen})),
          act(std::move(what)),
          parts{
              .mark =
                  skiff::compose::styled({.alignSelf = scene::align::kMiddle},
                                         icon_mark(colours, icon)),
              .label = skiff::compose::styled(
                  {.shrink = scene::axes::kX,
                   .alignSelf = scene::align::kMiddle},
                  elided(nodes::Text(std::move(text), 14.0f, colours.text)))} {}
    void set_chosen(bool on) { fState.apply({.selected = on}); }
  };
  struct tab_list : skiff::compose::Stacked {
    struct parts_t {
      tab_row general;
      std::vector<tab_row> protocol;  // the room's protocol's tabs, as it lists them
      tab_row notifications, looks;
    } parts;
    template <class... Tabs>
    void add(room_settings* box, manage_tab_list<Tabs...>) {
      (parts.protocol.emplace_back(*box->colours_, std::string(tab_title(Tabs{})), tab_icon(Tabs{}), pick_tab{box, settings_tab_t{Tabs{}}}), ...);
    }
    explicit tab_list(room_settings *box)
        : Stacked(skiff::compose::vbox(
              2.0f, {.fillY = true,
                     .width = 220.0f,
                     .padding = {4.0f, 12.0f, 12.0f, 12.0f}})),
          parts{.general = tab_row(*box->colours_, "General", icon::gear{},
                                   {box, settings_tab::general{}}),
                .notifications =
                    tab_row(*box->colours_, "Notifications", icon::bell{},
                            {box, settings_tab::notifications{}}),
                .looks = tab_row(*box->colours_, "Appearance", icon::eye{},
                                 {box, settings_tab::looks{}})} {
      spl::visit([&](auto of) { this->add(box, tabs_of_t<decltype(of)>{}); }, box->facts.speaks);
    }
    [[nodiscard]] std::vector<tab_row*> rows() {
      std::vector<tab_row*> out{&parts.general};
      for (tab_row& one : parts.protocol)
        out.push_back(&one);
      out.push_back(&parts.notifications);
      out.push_back(&parts.looks);
      return out;
    }
    void show(const settings_tab_t& tab) {
      for (tab_row* one : this->rows())
        one->set_chosen(one->act.tab.index() == tab.index());
    }
  };

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
      look_choices<Actions> choices;
    } parts;
    looks_page(room_settings *box, const room_settings_facts &)
        : Stacked(skiff::compose::vbox(
              6.0f, {.fillX = true,
                     .autoSize = scene::axes::kY,
                     .padding = {0.0f, 28.0f, 24.0f, 12.0f}})),
          parts{.heading = tab_heading(*box->colours_, "Appearance"),
                .choices = look_choices<Actions>(*box->colours_, *box->looks_,
                                                 choice_level::chat{})} {}
  };

  using page_t = typename variant_of_types<
      typename joined<type_list<room_general_page_t, notifications_page, looks_page>, protocol_pages>::type>::type;
  struct page_holder : skiff::compose::Stacked {
    struct parts_t {
      page_t page;
    } parts;
    explicit page_holder(page_t first)
        : Stacked(skiff::compose::vbox(
              0.0f, {.fillX = true, .autoSize = scene::axes::kY})),
          parts{.page = std::move(first)} {}
  };

  // ---- the dialog ---------------------------------------------------------------------
  struct close_it {
    using Answer = ::mux::ui::request::close_manage;
    ::mux::ui::request::close_manage operator()() { return ::mux::ui::request::close_manage{}; }
  };
  using header_t = page_header_t<no_back, close_it>;
  struct body_row : skiff::compose::Stacked {
    struct parts_t {
      tab_list tabs;
      nodes::ScrollContainer<page_holder> content;
    } parts;
    body_row(room_settings *box, page_t first)
        : Stacked(skiff::compose::hbox(
              0.0f, {.fillX = true, .grow = scene::axes::kY})),
          parts{.tabs = tab_list(box),
                .content = skiff::compose::styled(
                    {.fillY = true, .grow = scene::axes::kX},
                    nodes::ScrollContainer<page_holder>(
                        page_holder(std::move(first))))} {}
    // Narrow -- a phone's -- the tabs a thin column of their icons beside the
    // page, the page the rest of the width: beside a column of 220 it had a
    // hundred. Their sizing as it is, only narrower -- flipped from a column
    // to a row and back, the sizes clashed and the page was left empty.
    bool narrow = false;
    void layoutChildren() {
      if (const bool now = fState.contentBox().width() < 560.0f; now != narrow) {
        narrow = now;
        auto& [tabs, content] = parts;
        tabs.apply({.width = narrow ? 60.0f : 220.0f,
                    .padding = narrow ? scene::Margin{4.0f, 6.0f, 12.0f, 6.0f} : scene::Margin{4.0f, 12.0f, 12.0f, 12.0f}});
        // Narrow, the rows' sides 4, not 12: their icon (28) had 24 of 48.
        for (tab_row* one : tabs.rows()) {
          one->parts.label.setVisible(!narrow);
          one->apply({.padding = narrow ? scene::Margin{0.0f, 4.0f, 0.0f, 4.0f} : scene::Margin{0.0f, 12.0f, 0.0f, 12.0f}});
        }
        tabs.invalidateLayout();
        content.invalidateLayout();
        this->invalidateLayout();
      }
      this->nodes::Stack::layoutChildren();
    }
  };
  struct parts_t {
    header_t header;
    body_row body;
  } parts;

  settings_tab_t tab = settings_tab::general{};
  // What each protocol keeps while its pages are made again -- Matrix's: an
  // encryption being confirmed, the level being picked -- in the list's order.
  typename protocol_lists<protocols>::parts protocol_parts;
  // A tab to be made again, at the next frame: not from inside a press on
  // what it replaces.
  bool rebuild_due = false;
  bool to_top = false;  // another tab: shown from its top

  room_settings(const ui_needs<Actions>& n, const room_settings_facts& shown) : room_settings(n.colours, n.looks, shown) {}
  room_settings(const palette *colours, const looks_shown *looks,
                const room_settings_facts &shown)
      : Stacked(skiff::compose::vbox(0.0f, {.fill = true})), colours_(colours),
        looks_(looks), facts(shown),
        parts{.header = page_header<no_back, close_it>(*colours, "Room Settings - " + shown.name, {},
                                 {}, false, true),
              .body =
                  body_row(this, page_t(std::in_place_index<0>, room_general_page(*colours, shown)))} {
    parts.body.parts.tabs.show(tab);
  }

  [[nodiscard]] page_holder& holder() { return std::get<0>(parts.body.parts.content.fChildren); }
  // What a protocol keeps here: by its state type.
  template <class State>
  [[nodiscard]] auto& part() {
    return std::get<index_in<proto::id<State>>(protocols{})>(protocol_parts);
  }
  template <class Tag, class... Tags>
  static constexpr std::size_t index_in(protocol_list<Tags...>) {
    constexpr std::array<bool, sizeof...(Tags)> is{std::derived_from<Tag, Tags>...};
    return static_cast<std::size_t>(std::ranges::find(is, true) - is.begin());
  }
  // A tab shown, made from the facts as they now are -- at the next frame.
  void show_tab(const settings_tab_t& to) {
    tab = to;
    rebuild_due = true;
    this->markDamaged();
  }
  // The tab up, made again: what a page's act asks, once it changed the facts.
  void show_again() { this->show_tab(tab); }
  [[nodiscard]] bool settling() const { return rebuild_due; }
  void update(double) {
    if (std::exchange(rebuild_due, false))
      this->rebuild();
  }
  void rebuild() {
    const settings_tab_t to = tab;
    auto& page = holder().parts.page;
    spl::visit(spl::overloaded{
                      [&](settings_tab::general) { page.template emplace<room_general_page_t>(room_general_page(*colours_, facts)); },
                      [&](settings_tab::notifications) { page.template emplace<notifications_page>(this, facts); },
                      [&](settings_tab::looks) { page.template emplace<looks_page>(this, facts); },
                      // A protocol's tab: the page its page_type() gives.
                      [&](auto theirs) { page.template emplace<page_of_t<decltype(theirs)>>(this, facts); }},
                  to);
    parts.body.parts.tabs.show(tab);
    if (std::exchange(to_top, false))
      parts.body.parts.content.scrollTo(0.0f);
    this->invalidateLayout();
  }
};

}  // namespace mux::ui
