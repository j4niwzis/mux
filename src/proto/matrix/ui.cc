// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui.proto.matrix -- What Matrix shows of its own in the client's UI --
// its account form, and Manage room's tabs of its own and their pages:
// its overloads on its tag, found by ADL where the UI is made (the program,
// which imports mux.ui.proto). Its account form; and Manage room's tabs of
// Matrix's: a room's name, topic and addresses; its join rule, history and
// encryption; its power levels; its version.
export module mux.ui.proto.matrix;

import std;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes.flow;
import skiff.nodes.text;
import skiff.widgets.button;
import skiff.widgets.motion;
import skiff.widgets.sliderbar;
import skiff.widgets.textarea;
import mux.core;
import mux.config;
import mux.proto.kept;
import mux.ui;



namespace mux::proto::matrix::form_detail {
using namespace ::mux::ui;

// A Matrix account's settings: its user ID and password, the homeserver
// (found through the server's .well-known when left empty) and what this
// device is called.
template <class Actions>
struct matrix_form : nodes::Stack {
  // What the add-account pane says of the protocol.
  static constexpr std::string_view note = "A user ID like @user:example.org, on a homeserver such as Synapse.";
  Actions* actions = nullptr;
  std::optional<std::string> editing;

  struct parts_t {
    field user_id{"User ID", "@user:example.org"};
    field password{"Password", "Password"};
    field homeserver{"Homeserver", "found through the server's .well-known"};
    field device_name{"Device name", "mux", "mux"};
    form_end<Actions> end;
  } parts;

  matrix_form(Actions* a, const std::optional<config::matrix_account>& from)
      : actions(a), parts{.end = form_end<Actions>(a, from.has_value())} {
    auto& [user_id, password, homeserver, device_name, end] = parts;
    fState.apply({.fillX = true, .autoSize = scene::axes::kY});
    this->setGap(12.0f);
    password.parts.box.setMasked(true);
    if (from) {
      editing = from->user_id;
      user_id.parts.box.setText(from->user_id);
      password.parts.box.setText(from->password);
      if (from->homeserver)
        homeserver.parts.box.setText(*from->homeserver);
      device_name.parts.box.setText(from->device_name);
    }
  }

  [[nodiscard]] std::expected<config::matrix_account, std::string> account() const {
    config::matrix_account out{.user_id = parts.user_id.text(),
                               .password = parts.password.text(),
                               .homeserver = typed_or_nothing(parts.homeserver.text()),
                               .device_name = parts.device_name.text()};
    if (auto wrong = check(out))  // the protocol's own check, by ADL
      return std::unexpected(*wrong);
    return out;
  }

  void say(std::string text, bool error) { parts.end.say(std::move(text), error); }

  using Node::onKey;
  void onKey(scene::phase::bubble, const scene::key::down& press, scene::Reply& reply) {
    if (press.key == scene::keys::kEnter) {
      actions->submit_login();
      reply.handle();
    }
  }

};


}  // namespace mux::proto::matrix::form_detail

export namespace mux::proto::matrix {

// Its account form, for the panels: by the protocol's tag, and by what an
// account of it keeps.
template <class Actions>
using form = form_detail::matrix_form<Actions>;
template <class Actions>
constexpr std::type_identity<form<Actions>> form_type(const state&, std::type_identity<Actions>) {
  return {};
}
template <class Actions>
constexpr std::type_identity<form<Actions>> form_type_for(const kept&, std::type_identity<Actions>) {
  return {};
}

}  // namespace mux::proto::matrix

// ---- Manage room: Matrix's tabs and their pages --------------------------------
namespace mux::proto::matrix::manage_detail {
using namespace ::mux::ui;

// What Matrix keeps while its pages are made again: an encryption being
// confirmed (asked twice, as Element asks), and the level picked to give.
struct part {
  bool confirming_encryption = false;
  std::int64_t new_level = 50;
};

// What is done: asked of the program, and the facts kept as they will be --
// the page made again from them.
template <class Box>
void chose(Box* box, const join_rule_t& rule) {
  if (!box->facts.may(power_need::change_access{}))
    return;
  box->facts.join_rule = rule;
  box->actions->room_act(room_action::set_join_rule{rule});
  box->show_again();
}
template <class Box>
void chose(Box* box, const history_rule_t& rule) {
  if (!box->facts.may(power_need::change_history{}))
    return;
  box->facts.history = rule;
  box->actions->room_act(room_action::set_history{rule});
  box->show_again();
}
template <class Box>
void encrypt(Box* box) {
  auto& facts = box->facts;
  if (facts.encrypted || !facts.may(power_need::encrypt{}))
    return;
  part& kept = box->template part<state>();
  if (!kept.confirming_encryption) {
    kept.confirming_encryption = true;
  } else {
    kept.confirming_encryption = false;
    facts.encrypted = true;
    box->actions->room_act(room_action::encrypt{});
  }
  box->show_again();
}
template <class Box>
void needs_level(Box* box, const power_need_t& need, std::int64_t level) {
  auto& facts = box->facts;
  if (!facts.may(power_need::change_permissions{}) || level > facts.mine)
    return;
  box->actions->room_act(room_action::set_need{need, level});
  splice::visit(splice::overloaded{[&](power_need::default_role) { facts.needs.users_default = level; },
                                   [&](power_need::send_messages) { facts.needs.events_default = level; },
                                   [&](power_need::change_settings) { facts.needs.state_default = level; },
                                   [&](power_need::invite) { facts.needs.invite = level; },
                                   [&](power_need::kick) { facts.needs.kick = level; },
                                   [&](power_need::ban) { facts.needs.ban = level; },
                                   [&](power_need::redact) { facts.needs.redact = level; },
                                   [&](power_need::notify_everyone) { facts.needs.notify_room = level; },
                                   [&]<sends_state Need>(Need) { facts.needs.events.insert_or_assign(std::string(Need::event), level); }},
                need);
  box->show_again();
}
template <class Box>
void user_level(Box* box, const std::string& user, std::int64_t level) {
  auto& facts = box->facts;
  if (!facts.may(power_need::change_permissions{}) || level > facts.mine)
    return;
  box->actions->room_act(room_action::set_power{user, level});
  const auto found = std::ranges::find(facts.privileged, user, &room_settings_facts::person::id);
  if (found != facts.privileged.end())
    found->level = level;
  box->show_again();
}
template <class Box>
void event_level(Box* box, const std::string& event, std::int64_t level) {
  auto& facts = box->facts;
  if (!facts.may(power_need::change_permissions{}) || level > facts.mine || event.empty())
    return;
  box->actions->room_act(room_action::set_event_need{event, level});
  facts.needs.events.insert_or_assign(event, level);
  box->show_again();
}

template <class Box>
struct choose_join {
  Box* box;
  join_rule_t rule;
  void operator()() const { chose(box, rule); }
};
template <class Box>
struct choose_history {
  Box* box;
  history_rule_t rule;
  void operator()() const { chose(box, rule); }
};
template <class Box>
struct turn_encryption_on {
  Box* box;
  void operator()() const { encrypt(box); }
};
template <class Box>
struct set_need {
  Box* box;
  power_need_t need;
  std::int64_t level;
  void operator()() const { needs_level(box, need, level); }
};
template <class Box>
struct set_level {
  Box* box;
  std::string user;
  std::int64_t level;
  void operator()() const { user_level(box, user, level); }
};
template <class Box>
struct set_event_level {
  Box* box;
  std::string event;
  std::int64_t level;
  void operator()() const { event_level(box, event, level); }
};
template <class Box>
struct pick_new_level {
  Box* box;
  std::int64_t level;
  void operator()() const {
    box->template part<state>().new_level = level;
    box->show_again();
  }
};

// ---- Room: its picture, name, topic and addresses ----------------------------
template <class Box>
struct room_page : nodes::Stack {
  using Actions = typename Box::actions_type;
  struct save {
    Box* box;
    room_page* page;
    void operator()() const {
      auto& facts = box->facts;
      const std::string& name = page->parts.name.text();
      const std::string& topic = page->parts.topic.text();
      if (name != facts.name && facts.may(power_need::rename{})) {
        box->actions->room_act(room_action::rename{name});
        facts.name = name;
      }
      if (topic != facts.topic && facts.may(power_need::retopic{})) {
        box->actions->room_act(room_action::retopic{topic});
        facts.topic = topic;
      }
    }
  };
  struct cancel {
    Box* box;
    void operator()() const { box->show_again(); }
  };
  using buttons_row = dialog_buttons<cancel, save>;
  struct parts_t {
    nodes::Text heading = tab_heading("Room");
    avatar_mark photo;
    field name;
    field topic;
    buttons_row buttons;
    nodes::Text addresses = part_heading("Room Addresses");
    nodes::Text published = part_heading("Published Addresses");
    nodes::Text published_about = explained(
        "Published addresses can be used by anyone on any server to join your room. To publish an address, it "
        "needs to be set as a local address first.");
    nodes::Text main_address;
    nodes::Text others_title{"Other published addresses:", 14.0f, text_colour};
    std::vector<nodes::Text> others;
  } parts;
  room_page(Actions*, Box* box, const room_settings_facts& facts)
      : parts{.photo = avatar_mark(facts.id, facts.name, 88.0f),
              .name = field("Room Name", "", facts.name),
              .topic = field("Room Topic", "", facts.topic),
              .buttons = buttons_row("Save", {box}, {box, this}),
              .main_address = nodes::Text("Main address: " + facts.alias.value_or("none"), 14.0f, text_colour)} {
    this->setGap(6.0f);
    fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 28.0f, 24.0f, 12.0f}});
    parts.photo.apply({.alignSelf = scene::align::kStart});
    const bool rename = facts.may(power_need::rename{});
    const bool retopic = facts.may(power_need::retopic{});
    parts.name.apply({.disabled = !rename});
    parts.topic.apply({.disabled = !retopic});
    parts.buttons.setVisible(rename || retopic);
    parts.main_address.setWrapped(true);
    parts.main_address.apply({.fillX = true});
    for (const std::string& one : facts.other_aliases)
      parts.others.emplace_back(one, 14.0f, text_colour);
    if (facts.other_aliases.empty())
      parts.others.emplace_back("No other published addresses yet.", 13.0f, dim_colour);
  }
};

// ---- Security & Privacy -----------------------------------------------------------
template <class Box>
struct security_page : nodes::Stack {
  using Actions = typename Box::actions_type;
  using join_choice = radio_choice<choose_join<Box>>;
  using history_choice = radio_choice<choose_history<Box>>;
  struct parts_t {
    nodes::Text heading = tab_heading("Security & Privacy");
    nodes::Text encryption = part_heading("Encryption");
    nodes::Text encryption_about = explained("Once enabled, encryption cannot be disabled.");
    toggle_line<turn_encryption_on<Box>> encrypted;
    nodes::Text encryption_warning;
    nodes::Text access = part_heading("Access");
    nodes::Text access_about;
    join_choice invite, knock, open;
    nodes::Text history = part_heading("Who can read history?");
    nodes::Text history_about = explained(
        "Changes to who can read history will only apply to future messages in this room. The visibility of "
        "existing history will be unchanged.");
    history_choice anyone, shared, invited, joined;
  } parts;
  security_page(Actions*, Box* box, const room_settings_facts& facts)
      : parts{.encrypted = toggle_line<turn_encryption_on<Box>>("Encrypted", {box}, facts.encrypted,
                                                                !facts.encrypted && facts.may(power_need::encrypt{})),
              .encryption_warning = nodes::Text(box->template part<state>().confirming_encryption
                                                    ? "Press again to enable encryption. mux cannot read encrypted "
                                                      "rooms yet: what is sent after this will not show here."
                                                    : "",
                                                13.0f, error_colour),
              .access_about = explained("Decide who can join " + facts.name + "."),
              .invite = join_choice("Private (invite only)", "Only invited people can join.", {box, join_rule::invite{}},
                                    is_rule<join_rule::invite>(facts.join_rule), facts.may(power_need::change_access{})),
              .knock = join_choice("Ask to join", "People cannot join unless access is granted.", {box, join_rule::knock{}},
                                   is_rule<join_rule::knock>(facts.join_rule), facts.may(power_need::change_access{})),
              .open = join_choice("Public", "Anyone can find and join.", {box, join_rule::open{}},
                                  is_rule<join_rule::open>(facts.join_rule), facts.may(power_need::change_access{})),
              .anyone = history_choice("Anyone", "", {box, history_rule::world_readable{}},
                                       is_rule<history_rule::world_readable>(facts.history),
                                       facts.may(power_need::change_history{})),
              .shared = history_choice("Members only (since the point in time of selecting this option)", "",
                                       {box, history_rule::shared{}}, is_rule<history_rule::shared>(facts.history),
                                       facts.may(power_need::change_history{})),
              .invited = history_choice("Members only (since they were invited)", "", {box, history_rule::invited{}},
                                        is_rule<history_rule::invited>(facts.history), facts.may(power_need::change_history{})),
              .joined = history_choice("Members only (since they joined)", "", {box, history_rule::joined{}},
                                       is_rule<history_rule::joined>(facts.history), facts.may(power_need::change_history{}))} {
    this->setGap(4.0f);
    fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 28.0f, 24.0f, 12.0f}});
    parts.encryption_warning.setWrapped(true);
    parts.encryption_warning.apply({.fillX = true});
    parts.encryption_warning.setVisible(box->template part<state>().confirming_encryption);
  }
};

// ---- Roles & Permissions -------------------------------------------------------------
// A level chosen of three, as Element's selects: Default, Moderator, Admin.
template <class Act, class Make>
struct level_choice : nodes::Stack {
  struct parts_t {
    segment<Act> fallback, moderator, admin;
    nodes::Text custom;
  } parts;
  level_choice(Make make, std::int64_t now, std::int64_t fallback, bool allowed)
      : parts{.fallback = segment<Act>("Default", make(fallback)),
              .moderator = segment<Act>("Moderator", make(50)),
              .admin = segment<Act>("Admin", make(100)),
              .custom = nodes::Text("", 12.0f, dim_colour)} {
    this->setHorizontal();
    this->setGap(4.0f);
    fState.apply({.autoSize = scene::axes::kBoth, .alignSelf = scene::align::kMiddle, .disabled = !allowed});
    parts.fallback.set_active(now == fallback);
    parts.moderator.set_active(now == 50 && fallback != 50);
    parts.admin.set_active(now == 100 && fallback != 100);
    const bool custom = now != fallback && now != 50 && now != 100;
    parts.custom.setText(!custom ? std::string() : now == kCreatorPower ? std::string("Creator") : std::format("Custom ({})", now));
    parts.custom.setVisible(custom);
    parts.custom.apply({.alignSelf = scene::align::kMiddle});
    if (!allowed)
      fState.setAlpha(0.55f);
  }
};
template <class Box>
struct roles_page : nodes::Stack {
  using Actions = typename Box::actions_type;
  struct need_maker {
    Box* box;
    power_need_t need;
    set_need<Box> operator()(std::int64_t level) const { return {box, need, level}; }
  };
  struct user_maker {
    Box* box;
    std::string user;
    set_level<Box> operator()(std::int64_t level) const { return {box, user, level}; }
  };
  struct event_maker {
    Box* box;
    std::string event;
    set_event_level<Box> operator()(std::int64_t level) const { return {box, event, level}; }
  };
  // A user given the level picked: added to the privileged, where not there.
  struct add_privileged {
    Box* box;
    roles_page* page;
    void operator()() const {
      const std::string user = page->parts.adding.parts.user.text();
      if (user.empty())
        return;
      auto& facts = box->facts;
      if (std::ranges::find(facts.privileged, user, &room_settings_facts::person::id) == facts.privileged.end())
        facts.privileged.push_back({user, user, facts.needs.users_default});
      user_level(box, user, box->template part<state>().new_level);
    }
  };
  struct add_event_need {
    Box* box;
    roles_page* page;
    void operator()() const {
      event_level(box, page->parts.adding_event.parts.event.text(), box->template part<state>().new_level);
    }
  };
  // A kind of event the list does not name, as the power levels set it.
  struct event_row : nodes::Stack {
    struct parts_t {
      nodes::Text label;
      level_choice<set_event_level<Box>, event_maker> levels;
    } parts;
    event_row(Box* box, const std::string& event, std::int64_t level, const room_settings_facts& facts)
        : parts{.label = nodes::Text(event, 14.0f, text_colour),
                .levels = level_choice<set_event_level<Box>, event_maker>(
                    event_maker{box, event}, level, facts.needs.state_default,
                    facts.may(power_need::change_permissions{}) && level <= facts.mine)} {
      this->setHorizontal();
      this->setGap(10.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {4.0f, 0.0f, 4.0f, 0.0f}});
      parts.label.setElided(true);
      parts.label.apply({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
    }
  };
  // Any kind of event, by its type: the level it asks, set.
  struct new_event_row : nodes::Stack {
    struct parts_t {
      field event;
      segment<pick_new_level<Box>> moderator, admin;
      widgets::Button<add_event_need> apply;
    } parts;
    new_event_row(Box* box, roles_page* page)
        : parts{.event = field("", "Event type, as m.room.server_acl"),
                .moderator = segment<pick_new_level<Box>>("Moderator", {box, 50}),
                .admin = segment<pick_new_level<Box>>("Admin", {box, 100}),
                .apply = widgets::Button<add_event_need>("Apply", {box, page})} {
      this->setHorizontal();
      this->setGap(6.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY});
      parts.event.apply({.relativeSize = scene::axes::kNone, .grow = scene::axes::kX, .alignSelf = scene::align::kEnd});
      parts.moderator.apply({.alignSelf = scene::align::kEnd, .margin = {0.0f, 0.0f, 4.0f, 0.0f}});
      parts.admin.apply({.alignSelf = scene::align::kEnd, .margin = {0.0f, 0.0f, 4.0f, 0.0f}});
      parts.apply.setPrimary(true);
      parts.apply.apply({.width = 80.0f, .height = 30.0f, .alignSelf = scene::align::kEnd, .margin = {0.0f, 0.0f, 3.0f, 0.0f}});
    }
  };
  struct permission_row : nodes::Stack {
    struct parts_t {
      nodes::Text label;
      level_choice<set_need<Box>, need_maker> levels;
    } parts;
    permission_row(Box* box, std::string text, power_need_t need, const room_settings_facts& facts)
        : parts{.label = nodes::Text(std::move(text), 14.0f, text_colour),
                .levels = level_choice<set_need<Box>, need_maker>(
                    need_maker{box, need}, facts.needs.of(need), facts.needs.users_default,
                    facts.may(power_need::change_permissions{}) && facts.needs.of(need) <= facts.mine)} {
      this->setHorizontal();
      this->setGap(10.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {4.0f, 0.0f, 4.0f, 0.0f}});
      parts.label.setWrapped(true);
      parts.label.apply({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
    }
  };
  struct privileged_row : nodes::Stack {
    struct parts_t {
      avatar_mark face;
      two_lines texts;
      level_choice<set_level<Box>, user_maker> levels;
    } parts;
    privileged_row(Box* box, const room_settings_facts::person& one, const room_settings_facts& facts)
        : parts{.face = avatar_mark(one.id, one.name, 32.0f),
                .texts = two_lines(one.name, one.id, 14.0f, 2.0f),
                .levels = level_choice<set_level<Box>, user_maker>(
                    user_maker{box, one.id}, one.level, facts.needs.users_default,
                    facts.may(power_need::change_permissions{}) && (one.level < facts.mine))} {
      this->setHorizontal();
      this->setGap(10.0f);
      fState.apply({.fillX = true, .height = 48.0f});
    }
  };
  struct new_level_row : nodes::Stack {
    struct parts_t {
      field user;
      segment<pick_new_level<Box>> moderator, admin;
      widgets::Button<add_privileged> apply;
    } parts;
    new_level_row(Box* box, roles_page* page)
        : parts{.user = field("", "User ID, as @someone:server"),
                .moderator = segment<pick_new_level<Box>>("Moderator", {box, 50}),
                .admin = segment<pick_new_level<Box>>("Admin", {box, 100}),
                .apply = widgets::Button<add_privileged>("Apply", {box, page})} {
      this->setHorizontal();
      this->setGap(6.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY});
      parts.user.apply({.relativeSize = scene::axes::kNone, .grow = scene::axes::kX, .alignSelf = scene::align::kEnd});
      parts.moderator.apply({.alignSelf = scene::align::kEnd, .margin = {0.0f, 0.0f, 4.0f, 0.0f}});
      parts.admin.apply({.alignSelf = scene::align::kEnd, .margin = {0.0f, 0.0f, 4.0f, 0.0f}});
      parts.apply.setPrimary(true);
      parts.apply.apply({.width = 80.0f, .height = 30.0f, .alignSelf = scene::align::kEnd, .margin = {0.0f, 0.0f, 3.0f, 0.0f}});
    }
  };
  struct parts_t {
    nodes::Text heading = tab_heading("Roles & Permissions");
    nodes::Text privileged = part_heading("Privileged Users");
    std::vector<privileged_row> users;
    nodes::Text none_privileged = explained("No users have specific privileges in this room.");
    nodes::Text add = part_heading("Add privileged users");
    nodes::Text add_about = explained("Give one or multiple users in this room more privileges.");
    new_level_row adding;
    nodes::Text permissions = part_heading("Permissions");
    nodes::Text permissions_about = explained("Select the roles required to change various parts of the room.");
    std::vector<permission_row> rows;
    // Every other kind of event the power levels set, and any kind added.
    std::vector<event_row> others;
    nodes::Text add_event = part_heading("Any other event");
    new_event_row adding_event;
  } parts;
  roles_page(Actions*, Box* box, const room_settings_facts& facts)
      : parts{.adding = new_level_row(box, this), .adding_event = new_event_row(box, this)} {
    this->setGap(4.0f);
    fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 28.0f, 24.0f, 12.0f}});
    parts.users.reserve(facts.privileged.size());
    for (const auto& one : facts.privileged)
      parts.users.emplace_back(box, one, facts);
    parts.none_privileged.setVisible(facts.privileged.empty());
    const bool may_add = facts.may(power_need::change_permissions{});
    for (scene::Node* each : std::initializer_list<scene::Node*>{&parts.add, &parts.add_about, &parts.adding})
      each->setVisible(may_add);
    // Element's list, in its order and words.
    const std::pair<const char*, power_need_t> all[] = {
        {"Default role", power_need::default_role{}},
        {"Send messages", power_need::send_messages{}},
        {"Invite users", power_need::invite{}},
        {"Change settings", power_need::change_settings{}},
        {"Remove users", power_need::kick{}},
        {"Ban users", power_need::ban{}},
        {"Remove messages sent by others", power_need::redact{}},
        {"Notify everyone", power_need::notify_everyone{}},
        {"Change avatar", power_need::change_avatar{}},
        {"Change room name", power_need::rename{}},
        {"Change main address for the room", power_need::change_address{}},
        {"Change history visibility", power_need::change_history{}},
        {"Change who can join", power_need::change_access{}},
        {"Change permissions", power_need::change_permissions{}},
        {"Change topic", power_need::retopic{}},
        {"Upgrade the room", power_need::upgrade{}},
        {"Enable room encryption", power_need::encrypt{}},
        {"Change server ACLs", power_need::change_acl{}},
        {"Manage pinned events", power_need::pin{}},
    };
    parts.rows.reserve(std::size(all));
    for (const auto& [text, need] : all)
      parts.rows.emplace_back(box, text, need, facts);
    // Those the list has by their own row are not again.
    std::set<std::string_view> listed;
    for (const auto& [text, need] : all)
      splice::visit(splice::overloaded{[&]<sends_state Need>(const Need&) { listed.insert(Need::event); }, [](const auto&) {}}, need);
    for (const auto& [event, level] : facts.needs.events)
      if (!listed.contains(event))
        parts.others.emplace_back(box, event, level, facts);
    for (scene::Node* each : std::initializer_list<scene::Node*>{&parts.add_event, &parts.adding_event})
      each->setVisible(may_add);
    // The level picked, lit.
    const std::int64_t picked = box->template part<state>().new_level;
    parts.adding.parts.moderator.set_active(picked == 50);
    parts.adding.parts.admin.set_active(picked == 100);
    parts.adding_event.parts.moderator.set_active(picked == 50);
    parts.adding_event.parts.admin.set_active(picked == 100);
  }
};

// ---- Advanced -----------------------------------------------------------------------
template <class Box>
struct advanced_page : nodes::Stack {
  using Actions = typename Box::actions_type;
  // Upgraded to the version written: asked of the server.
  struct upgrade_press {
    Box* box;
    advanced_page* page;
    void operator()() const {
      if (!box->facts.may(power_need::upgrade{}))
        return;
      if (const std::string version = page->parts.upgrade_to.text(); !version.empty())
        box->actions->room_act(room_action::upgrade{version});
    }
  };
  struct parts_t {
    nodes::Text heading = tab_heading("Advanced");
    nodes::Text information = part_heading("Room information");
    copy_line id;
    nodes::Text version;
    // Upgraded, as Element's: the version to go to, and the button. The
    // server makes the new room and tombstones this one.
    field upgrade_to;
    widgets::Button<upgrade_press> upgrade;
    nodes::Text tools = part_heading("Developer tools");
    widgets::Button<ask<Actions, &Actions::explore_state>> explore;
    widgets::Button<ask<Actions, &Actions::open_send_custom>> send_custom;
    nodes::Text packs_heading = part_heading("Emojis & Stickers");
    widgets::Button<ask<Actions, &Actions::open_room_packs>> packs;
  } parts;
  advanced_page(Actions* a, Box* box, const room_settings_facts& facts)
      : parts{.id = copy_line("Internal room ID", facts.id),
              .version = nodes::Text("Room version: " + facts.version, 14.0f, text_colour),
              .upgrade_to = field("Upgrade to room version", "12", "12"),
              .upgrade = widgets::Button<upgrade_press>("Upgrade this room", {box, this}),
              .explore = widgets::Button<ask<Actions, &Actions::explore_state>>("Explore room state", {a}),
              .send_custom = widgets::Button<ask<Actions, &Actions::open_send_custom>>("Send custom event", {a}),
              .packs = widgets::Button<ask<Actions, &Actions::open_room_packs>>("Edit room packs", {a})} {
    this->setGap(6.0f);
    fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 28.0f, 24.0f, 12.0f}});
    for (scene::Node* each : std::initializer_list<scene::Node*>{&parts.explore, &parts.send_custom, &parts.packs, &parts.upgrade})
      each->apply({.width = 180.0f, .height = 32.0f});
    const bool may = facts.may(power_need::upgrade{});
    parts.upgrade_to.setVisible(may);
    parts.upgrade.setVisible(may);
  }
};

}  // namespace mux::proto::matrix::manage_detail

export namespace mux::proto::matrix {

// Manage room's tabs of Matrix's, each with its title, its icon and its page.
namespace manage {
struct room {};
struct security {};
struct roles {};
struct advanced {};
}  // namespace manage
constexpr ui::manage_tab_list<manage::room, manage::security, manage::roles, manage::advanced> manage_tabs(const state&) {
  return {};
}
constexpr std::type_identity<manage_detail::part> manage_part_type(const state&) { return {}; }

}  // namespace mux::proto::matrix

export namespace mux::proto::matrix::manage {

constexpr std::string_view tab_title(room) { return "Room"; }
constexpr std::string_view tab_title(security) { return "Security & Privacy"; }
constexpr std::string_view tab_title(roles) { return "Roles & Permissions"; }
constexpr std::string_view tab_title(advanced) { return "Advanced"; }
inline ui::icon_t tab_icon(room) { return ui::icon::people{}; }
inline ui::icon_t tab_icon(security) { return ui::icon::eye{}; }
inline ui::icon_t tab_icon(roles) { return ui::icon::people{}; }
inline ui::icon_t tab_icon(advanced) { return ui::icon::sliders{}; }
template <class Box>
constexpr std::type_identity<manage_detail::room_page<Box>> page_type(room, std::type_identity<Box>) {
  return {};
}
template <class Box>
constexpr std::type_identity<manage_detail::security_page<Box>> page_type(security, std::type_identity<Box>) {
  return {};
}
template <class Box>
constexpr std::type_identity<manage_detail::roles_page<Box>> page_type(roles, std::type_identity<Box>) {
  return {};
}
template <class Box>
constexpr std::type_identity<manage_detail::advanced_page<Box>> page_type(advanced, std::type_identity<Box>) {
  return {};
}

}  // namespace mux::proto::matrix::manage
