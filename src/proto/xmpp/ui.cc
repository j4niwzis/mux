// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui.proto.xmpp -- What XMPP shows of its own in the client's UI: its
// account form, found by ADL where the panels are made.
export module mux.ui.proto.xmpp;

import std;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.compose;
import skiff.nodes.flow;
import skiff.nodes.image;
import skiff.nodes.text;
import skiff.widgets.button;
import skiff.widgets.motion;
import skiff.widgets.sliderbar;
import skiff.widgets.textarea;
import mux.core;
import mux.config;
import mux.proto.kept;
import mux.proto.xmpp.changes;
import mux.ui;
import mux.proto.xmpp;

namespace mux::proto::xmpp::form_detail {
using namespace ::mux::ui;

// What "Advanced" folds out on an XMPP form: the resource, where to connect,
// and PLAIN without TLS. Its height is what its last layout took.
template <class Actions> struct xmpp_advanced : skiff::compose::Stacked {
  using plain_toggle = toggle_for<sends<::mux::ui::request::toggle_plain>>;
  // The switch and what it says, side by side.
  struct plain_row : skiff::compose::Stacked {
    struct parts_t {
      plain_toggle plain;
      nodes::Text label;
    } parts;
    plain_row(const palette &colours)
        : Stacked(skiff::compose::hbox(
              10.0f, {.fillX = true, .autoSize = scene::axes::kY})),
          parts{.plain = plain_toggle(colours.widgets, {}),
                .label = skiff::compose::styled(
                    {.grow = scene::axes::kX},
                    wrapped(nodes::Text(
                        "Allow PLAIN without TLS. Only for a test server on "
                        "this machine: never over a network.",
                        13.0f, colours.error)))} {}
  };
  struct parts_t {
    field resource;
    field host;
    field port;
    plain_row row;
  } parts;

  xmpp_advanced(const palette &colours)
      : Stacked(
            skiff::compose::vbox(8.0f, {.fillX = true,
                                        .autoSize = scene::axes::kY,
                                        .padding = {0.0f, 0.0f, 12.0f, 0.0f}})),
        parts{.resource =
                  field(colours, "Device name (resource)", "mux", "mux"),
              .host = field(colours, "Host", "from the domain's SRV records"),
              .port = field(colours, "Port", "5222"),
              .row = plain_row(colours)} {}
  [[nodiscard]] plain_toggle& plain() { return parts.row.parts.plain; }
  [[nodiscard]] const plain_toggle& plain() const { return parts.row.parts.plain; }
};

// A picture the server sent with its form -- a captcha's -- decoded once,
// held by the row that shows it; shared, so the row moves freely.
struct sent_picture {
  std::shared_ptr<const skia::Sp<skia::SkImage>> held;
  const skia::Sp<skia::SkImage>* operator()() const { return held && *held ? held.get() : nullptr; }
};
[[nodiscard]] inline sent_picture picture_of(const registration_field& one) {
  if (one.picture.empty())
    return {};
  return {std::make_shared<const skia::Sp<skia::SkImage>>(skia::decodeImage(one.picture.data(), one.picture.size()))};
}
// What is said of a field beside its box: a field only read, its text; the
// choices it has; the addresses that go with it -- a captcha's picture or
// sound elsewhere.
[[nodiscard]] inline std::string note_of(const registration_field& one) {
  const auto joined = [](const std::vector<std::string>& lines, std::string_view between) {
    return std::ranges::to<std::string>(std::views::join_with(lines, between));
  };
  const std::string read = spl::visit(
      spl::overloaded{[&](field_shown::read) { return one.label.empty() || one.value.empty() ? joined(one.value, "\n") + one.label
                                                                                                 : one.label + ": " + joined(one.value, "\n"); },
                         [](const auto&) { return std::string(); }},
      one.shown);
  const std::vector<std::string> said{read, one.choices.empty() ? std::string() : "One of: " + joined(one.choices, ", "),
                                      joined(one.links, "\n")};
  return joined(std::ranges::to<std::vector<std::string>>(std::views::filter(said, [](const std::string& line) { return !line.empty(); })),
                "\n");
}

// A field of what the server asks to register: its picture where it has
// one, what is said of it, and its box -- masked for a secret; none for
// one only read; nothing at all shown of a hidden one, sent back as it came.
struct asked_row : skiff::compose::Stacked {
  struct parts_t {
    nodes::Image<sent_picture> picture;
    nodes::Text note;
    field box;
  } parts;
  std::string var;
  field_shown_t shown;
  std::vector<std::string> held;

  asked_row(const palette &colours, const registration_field &one)
      : Stacked(skiff::compose::vbox(
            6.0f, {.fillX = true, .autoSize = scene::axes::kY})),
        parts{.picture = skiff::compose::styled(
                  {.width = 240.0f,
                   .height = 90.0f,
                   .cornerRadius = 6.0f,
                   .background = colours.tile},
                  skiff::compose::visible(
                      !one.picture.empty(),
                      nodes::Image<sent_picture>(picture_of(one),
                                                 nodes::fit::contain{}))),
              .note = skiff::compose::styled(
                  {.fillX = true},
                  wrapped(nodes::Text(note_of(one), 13.0f, colours.dim))),
              .box = field(
                  colours, one.label + (one.required ? " (required)" : ""),
                  one.desc,
                  one.value.empty() ? std::string() : one.value.front())},
        var(one.var), shown(one.shown), held(one.value) {

    parts.note.setSelectable(true);
    parts.note.setVisible(!parts.note.text().empty());
    spl::visit(spl::overloaded{[&](field_shown::typed) {},
                                     [&](field_shown::masked) { parts.box.parts.box.setMasked(true); },
                                     [&](field_shown::hidden) { this->setVisible(false); },
                                     [&](field_shown::read) { parts.box.setVisible(false); }},
                  shown);
  }
  // Its answer: what is typed, or a hidden field's value as it came; none
  // for one only read.
  [[nodiscard]] std::optional<registration_answer> answer() const {
    return spl::visit(
        spl::overloaded{[](field_shown::read) { return std::optional<registration_answer>(); },
                           [&](field_shown::hidden) {
                             return std::optional(registration_answer{.var = var, .value = held.empty() ? std::string() : held.front()});
                           },
                           [&](const auto&) { return std::optional(registration_answer{.var = var, .value = parts.box.text()}); }},
        shown);
  }
};

// What the server asked, under the form: what it says, and its fields.
struct asked_part : skiff::compose::Stacked {
  struct parts_t {
    nodes::Text instructions;
    std::vector<asked_row> rows;
  } parts;
  explicit asked_part(const palette &colours)
      : Stacked(skiff::compose::vbox(
            10.0f, {.fillX = true, .autoSize = scene::axes::kY})),
        parts{.instructions = skiff::compose::styled(
                  {.fillX = true},
                  wrapped(nodes::Text("", 13.0f, colours.text)))} {

    parts.instructions.setSelectable(true);
    this->setVisible(false);
  }
  void show(const palette& colours, const registration_asked& asked) {
    parts.instructions.setText(asked.instructions + (asked.page ? "\nOr make the account on the server's page: " + *asked.page : std::string()));
    parts.instructions.setVisible(!parts.instructions.text().empty());
    parts.rows = std::ranges::to<std::vector<asked_row>>(std::views::transform(asked.fields, [&](const registration_field& one) { return asked_row(colours, one); }));
    this->setVisible(true);
    this->invalidateLayout();
  }
  [[nodiscard]] std::vector<registration_answer> answers() const {
    return std::ranges::to<std::vector<registration_answer>>(std::views::transform(std::views::filter(std::views::transform(parts.rows, [](const asked_row& row) { return row.answer(); }), [](const auto& one) { return one.has_value(); }), [](const auto& one) { return *one; }));
  }
};

// An XMPP account's settings: its JID and password, and "Advanced" folds out
// the rest. It fills the column it is given.
template <class Actions> struct xmpp_form : skiff::compose::Stacked {
  // What its handlers ask for, returned.
  using Answer = ::mux::ui::request::submit_login;
  // What the add-account pane says of the protocol.
  static constexpr std::string_view note = "An address like user@example.com, on a server such as Prosody or ejabberd.";
  // The address the account was saved under, when this is an edit of one.
  std::optional<std::string> editing;
  bool advanced = false;
  // A new account, made on the server first (XEP-0077); the answers to what
  // it asked, as saved, until it asks again.
  bool creating = false;
  std::vector<registration_answer> answered;
  const palette* colours_ = nullptr;
  struct pick_mode {
    xmpp_form* form;
    void operator()(std::size_t index) const { form->set_creating(index == 1); }
  };

  using advanced_button_t = button_for<sends<::mux::ui::request::toggle_advanced>>;
  struct parts_t {
    field address;
    field password;
    choice_menu<pick_mode> mode;
    asked_part asked;
    advanced_button_t advanced_button;
    widgets::Collapsible<xmpp_advanced<Actions>> more;
    form_end<Actions> end;
  } parts;

  xmpp_form(const palette &colours,
            const std::optional<::mux::proto::xmpp::kept> &from)
      : Stacked(skiff::compose::vbox(
            12.0f, {.fillX = true, .autoSize = scene::axes::kY})),
        creating(from && from->create.value_or(false)),
        answered(
            from ? from->answers.value_or(std::vector<registration_answer>{})
                 : std::vector<registration_answer>{}),
        colours_(&colours),
        parts{.address = field(colours, "Address (JID)", "user@example.com"),
              .password = field(colours, "Password", "Password"),
              .mode = skiff::compose::visible(
                  !from || creating,
                  make_choice_menu<pick_mode>(colours, "Account",
                      {"Sign in to an account", "Create a new account"},
                      creating ? 1 : 0, pick_mode{this})),
              .asked = asked_part(colours),
              .advanced_button = skiff::compose::styled(
                  {.width = 120.0f, .height = 36.0f},
                  advanced_button_t(colours.widgets, "Advanced", {})),
              .more = widgets::Collapsible<xmpp_advanced<Actions>>(colours),
              .end = form_end<Actions>(colours, from.has_value())} {
    auto& [address, password, mode, asked, advanced_button, more, end] = parts;
    // An account kept and signed in to: one already, nothing to choose.
    auto& folded = more.child().parts;
    password.parts.box.setMasked(true);
    if (from) {
      editing = from->address;
      address.parts.box.setText(from->address);
      password.parts.box.setText(from->password);
      folded.resource.parts.box.setText(from->resource);
      if (from->host)
        folded.host.parts.box.setText(*from->host);
      if (from->port)
        folded.port.parts.box.setText(std::to_string(*from->port));
      more.child().plain().setOn(from->plain_without_tls);
      advanced = from->resource != "mux" || from->host || from->port || from->plain_without_tls;
    }
    more.setOpenNow(advanced);
  }

  // Folded out or away: smoothly, where things move.
  void show_advanced(bool shown) {
    advanced = shown;
    parts.more.setOpen(shown);
  }

  void flip_plain() { parts.more.child().plain().setOn(!parts.more.child().plain().on()); }

  // A new account or one there is: what the server asked shown with it.
  void set_creating(bool on) {
    creating = on;
    parts.asked.setVisible(on && !parts.asked.parts.rows.empty());
    this->invalidateLayout();
  }
  // What the server asks to register, to be answered here and saved.
  void show_registration(const registration_asked& asked) {
    creating = true;
    parts.mode.setVisible(true);
    parts.asked.show(*colours_, asked);
    this->say("The server asks more to make the account: answer below, then save.", false);
    this->invalidateLayout();
  }

  // The account as typed, or what is wrong with it. What is folded away is
  // kept as it is: folding is not clearing.
  [[nodiscard]] std::expected<::mux::proto::xmpp::kept, std::string> account() const {
    const auto& folded = parts.more.child().parts;
    ::mux::proto::xmpp::kept out{.address = parts.address.text(),
                             .password = parts.password.text(),
                             .resource = folded.resource.text(),
                             .host = typed_or_nothing(folded.host.text()),
                             .plain_without_tls = parts.more.child().plain().on()};
    if (creating) {
      out.create = true;
      out.answers = parts.asked.parts.rows.empty() ? answered : parts.asked.answers();
      if (out.password.empty())
        return std::unexpected(std::string("A new account needs a password"));
    }
    if (const std::string& text = folded.port.text(); !text.empty()) {
      std::int64_t number = 0;
      const auto [last, failed] = std::from_chars(text.data(), text.data() + text.size(), number);
      if (failed != std::errc{} || last != text.data() + text.size())
        return std::unexpected("A port is a number from 1 to 65535");
      out.port = number;
    }
    if (auto wrong = check(out))  // the protocol's own check, by ADL
      return std::unexpected(*wrong);
    return out;
  }

  void say(std::string text, bool error) { parts.end.say(std::move(text), error); }

  using Node::onKey;
  std::optional<Answer> onKey(scene::phase::bubble, const scene::key::down& press, scene::Reply& reply) {
    std::optional<Answer> answer;
    if (press.key == scene::keys::kEnter) {
      answer = ::mux::ui::request::submit_login{};
      reply.handle();
    }
    return answer;
  }
};

}  // namespace mux::proto::xmpp::form_detail

export namespace mux::proto::xmpp {

// Its account form, for the panels: by the protocol's tag, and by what an
// account of it keeps.
template <class Actions>
using form = form_detail::xmpp_form<Actions>;
template <class Actions>
constexpr type_tag<form<Actions>> form_type(const state&, type_tag<Actions>) {
  return {};
}
template <class Actions>
constexpr type_tag<form<Actions>> form_type_for(const kept&, type_tag<Actions>) {
  return {};
}

}  // namespace mux::proto::xmpp

namespace mux::proto::xmpp::manage_detail {
using namespace ::mux::ui;
inline const muc_room& settings_of(const room_settings_facts& facts) {
  static const muc_room empty;
  const auto* room = facts.theirs.template get_if<muc_room>();
  return room ? *room : empty;
}
inline std::vector<std::string> lines_of(std::string text) {
  std::vector<std::string> lines;
  std::size_t start = 0;
  while (start < text.size()) {
    const auto end = text.find('\n', start);
    auto line = text.substr(start, end == text.npos ? text.npos : end - start);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    lines.push_back(std::move(line));
    if (end == text.npos) break;
    start = end + 1;
  }
  return lines;
}
struct config_row : skiff::compose::Stacked {
  struct parts_t { nodes::Text note; field input; } parts;
  muc_field original;
  config_row(const palette& colours, const muc_field& f)
      : Stacked(skiff::compose::vbox(6.0f, {.fillX = true, .autoSize = scene::axes::kY})),
        parts{.note = wrapped(nodes::Text(help(f), 12.0f, colours.dim)),
            .input = field(colours, f.label + (f.required ? " (required)" : ""), f.type == "boolean" ? "0 or 1" : "Value",
                std::ranges::to<std::string>(std::views::join_with(f.values, std::string_view("\n"))))}, original(f) {
    this->setVisible(f.type != "hidden");
    parts.input.setVisible(f.type != "fixed");
    parts.input.parts.box.setMasked(f.type == "text-private");
    if (f.type == "text-multi" || f.type == "jid-multi" || f.type == "list-multi") {
      parts.input.parts.box.setSingleLine(false);
      parts.input.parts.box.apply({.height = 90.0f});
    }
  }
  static std::string help(const muc_field& f) {
    std::string out = f.description;
    if (f.type == "fixed") for (const auto& v : f.values) out += "\n" + v;
    if (f.type == "boolean") out += "\n0: off; 1: on.";
    if (f.type == "list-multi" || f.type == "jid-multi" || f.type == "text-multi") out += "\nOne value per line.";
    for (const auto& [label, value] : f.options) out += "\n" + label + ": " + value;
    return out;
  }
  muc_field answer() const {
    auto out = original;
    if (out.type == "hidden" || out.type == "fixed") return out;
    out.values = lines_of(parts.input.text());
    if (out.type != "text-multi" && out.type != "jid-multi" && out.type != "list-multi")
      out.values = {parts.input.text()};
    if (!out.required && out.type != "boolean" && parts.input.text().empty()) out.values.clear();
    return out;
  }
};
template <class Box> struct room_page : skiff::compose::Stacked {
  struct save {
    room_page* page;
    using Answer = request::configure_muc;
    Answer operator()() const {
      Answer out;
      for (const auto& row : page->parts.fields) out.fields.push_back(row.answer());
      return out;
    }
  };
  struct topic_press {
    room_page* page;
    using Answer = ::mux::ui::request::room_act;
    Answer operator()() const { return {room_action::retopic{page->parts.topic.text()}}; }
  };
  struct nick_press {
    room_page* page;
    using Answer = request::muc_identity;
    Answer operator()() const { return {page->parts.nick.text()}; }
  };
  struct rejoin_press {
    room_page* page;
    using Answer = request::rejoin_muc;
    Answer operator()() const { return {page->parts.nick.text(), page->parts.password.text()}; }
  };
  struct parts_t {
    nodes::Text heading, status;
    field topic;
    widgets::Button<topic_press> set_topic;
    field nick;
    widgets::Button<nick_press> set_nick;
    field password;
    widgets::Button<rejoin_press> rejoin;
    button_for<sends<request::refresh_muc>> refresh;
    nodes::Text configuration;
    std::vector<config_row> fields;
    widgets::Button<save> submit;
  } parts;
  room_page(Box* box, const room_settings_facts& facts)
      : Stacked(skiff::compose::vbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 28.0f, 24.0f, 12.0f}})),
        parts{.heading = tab_heading(*box->colours_, "Room"),
            .status = wrapped(nodes::Text("Nickname: " + settings_of(facts).nick + " · " + settings_of(facts).affiliation + " · " + settings_of(facts).role, 13.0f, box->colours_->dim)),
            .topic = field(*box->colours_, "Subject", "Room subject", facts.topic),
            .set_topic = widgets::Button<topic_press>(box->colours_->widgets, "Change subject", {this}),
            .nick = field(*box->colours_, "Nickname", "Your nickname", settings_of(facts).nick),
            .set_nick = widgets::Button<nick_press>(box->colours_->widgets, "Change nickname", {this}),
            .password = field(*box->colours_, "Room password", "Password, if required"),
            .rejoin = widgets::Button<rejoin_press>(box->colours_->widgets, "Join room", {this}),
            .refresh = button_for<sends<request::refresh_muc>>(box->colours_->widgets, "Refresh room settings", {}),
            .configuration = wrapped(nodes::Text(settings_of(facts).configuring ? "Room configuration (server fields)" : "Room configuration is available to the room owner. Refresh to load it.", 13.0f, box->colours_->dim)),
            .submit = widgets::Button<save>(box->colours_->widgets, "Save configuration", {this})} {
    for (const auto& f : settings_of(facts).fields) parts.fields.emplace_back(*box->colours_, f);
    parts.submit.setVisible(settings_of(facts).configuring);
    parts.password.parts.box.setMasked(true);
    parts.password.setVisible(!settings_of(facts).joined); parts.rejoin.setVisible(!settings_of(facts).joined);
    parts.set_nick.setVisible(settings_of(facts).joined);
    parts.rejoin.apply({.height = 32.0f});
    for (scene::Node* n : std::initializer_list<scene::Node*>{&parts.submit, &parts.set_topic, &parts.set_nick, &parts.refresh}) n->apply({.height = 32.0f});
  }
};
template <class Box> struct roles_page : skiff::compose::Stacked {
  struct grant {
    roles_page* page;
    bool affiliation;
    using Answer = request::muc_privilege;
    Answer operator()() const {
      const auto rank = page->parts.rank.text();
      return {page->parts.user.text(), affiliation ? rank : "", affiliation ? "" : rank};
    }
  };
  struct destroy {
    roles_page* page;
    std::string room;
    using Answer = std::optional<request::destroy_muc>;
    Answer operator()() const {
      if (page->parts.confirm.text() != room) return {};
      return request::destroy_muc{page->parts.reason.text()};
    }
  };
  struct parts_t {
    nodes::Text heading, explanation;
    std::vector<nodes::Text> people;
    field user, rank;
    widgets::Button<grant> affiliation, role;
    nodes::Text destruction;
    field reason, confirm;
    widgets::Button<destroy> remove;
  } parts;
  roles_page(Box* box, const room_settings_facts& facts)
      : Stacked(skiff::compose::vbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 28.0f, 24.0f, 12.0f}})),
        parts{.heading = tab_heading(*box->colours_, "Roles & Permissions"),
            .explanation = wrapped(nodes::Text("Affiliations use a real bare JID: none, member, admin, owner, outcast (ban). Roles use an occupant nickname: visitor, participant, moderator, none (kick). The server checks your privileges.", 13.0f, box->colours_->dim)),
            .user = field(*box->colours_, "User JID or occupant nickname", "user@example.org or nickname"),
            .rank = field(*box->colours_, "Affiliation or role", "member"),
            .affiliation = widgets::Button<grant>(box->colours_->widgets, "Set affiliation", {this, true}),
            .role = widgets::Button<grant>(box->colours_->widgets, "Set occupant role", {this, false}),
            .destruction = wrapped(nodes::Text("Destroy room permanently (owner only). Type the room address to confirm.", 13.0f, box->colours_->error)),
            .reason = field(*box->colours_, "Reason", "Optional reason"),
            .confirm = field(*box->colours_, "Confirm room address", facts.id),
            .remove = widgets::Button<destroy>(box->colours_->widgets, "Destroy room", {this, facts.id})} {
    const auto& room = settings_of(facts);
    for (const auto& person : room.people) parts.people.push_back(wrapped(nodes::Text(person.nick + " · " + person.affiliation + " · " + person.role + (person.jid.empty() ? "" : " · " + person.jid), 13.0f, box->colours_->text)));
    for (const auto& person : room.affiliations) parts.people.push_back(wrapped(nodes::Text(person.jid + " · " + person.affiliation, 13.0f, box->colours_->text)));
    parts.affiliation.apply({.height = 32.0f, .disabled = room.affiliation != "owner" && room.affiliation != "admin"});
    parts.role.apply({.height = 32.0f, .disabled = room.role != "moderator"});
    const bool owner = room.affiliation == "owner";
    parts.destruction.setVisible(owner); parts.reason.setVisible(owner); parts.confirm.setVisible(owner);
    parts.remove.setVisible(owner); parts.remove.apply({.height = 32.0f});
  }
};
}
export namespace mux::proto::xmpp {
namespace manage { struct room {}; struct roles {}; }
constexpr ui::manage_tab_list<manage::room, manage::roles> manage_tabs(const state&) { return {}; }
}
export namespace mux::proto::xmpp::manage {
inline bool tab_visible(room, const ui::room_settings_facts& facts) { return facts.group; }
inline bool tab_visible(roles, const ui::room_settings_facts& facts) { return facts.group; }
constexpr std::string_view tab_title(room) { return "Room"; }
constexpr std::string_view tab_title(roles) { return "Roles & Permissions"; }
inline ui::icon_t tab_icon(room) { return ui::icon::people{}; }
inline ui::icon_t tab_icon(roles) { return ui::icon::people{}; }
template <class Box> constexpr type_tag<manage_detail::room_page<Box>> page_type(room, type_tag<Box>) { return {}; }
template <class Box> constexpr type_tag<manage_detail::roles_page<Box>> page_type(roles, type_tag<Box>) { return {}; }
}
