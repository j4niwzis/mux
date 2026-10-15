// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui.proto.xmpp -- What XMPP shows of its own in the client's UI: its
// account form, found by ADL where the panels are made.
export module mux.ui.proto.xmpp;

import std;
import tern;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.compose;
import skiff.nodes.flow;
import skiff.nodes.image;
import skiff.nodes.text;
import skiff.nodes.scroll;
import skiff.widgets.dropdown;
import skiff.widgets.button;
import skiff.widgets.motion;
import skiff.widgets.sliderbar;
import skiff.widgets.textarea;
import skiff.widgets.textbox;
import skiff.widgets.model;
import skiff.model;
import mux.core;
import mux.config;
import mux.proto.kept;
import mux.proto.xmpp.changes;
import mux.ui.base;
import mux.proto.xmpp;

namespace mux::proto::xmpp::form_detail {
using namespace ::mux::ui;

// What "Advanced" folds out on an XMPP form: the resource, where to connect,
// and PLAIN without TLS. Its height is what its last layout took.
struct xmpp_advanced : skiff::compose::Stacked {
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
    legacy::field resource;
    legacy::field host;
    legacy::field port;
    plain_row row;
  } parts;

  xmpp_advanced(const palette &colours)
      : Stacked(
            skiff::compose::vbox(8.0f, {.fillX = true,
                                        .autoSize = scene::axes::kY,
                                        .padding = {0.0f, 0.0f, 12.0f, 0.0f}})),
        parts{.resource =
                  legacy::field(colours, "Device name (resource)", "mux", "mux"),
              .host = legacy::field(colours, "Host", "from the domain's SRV records"),
              .port = legacy::field(colours, "Port", "5222"),
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
    legacy::field box;
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
              .box = legacy::field(
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
struct xmpp_form : skiff::compose::Stacked {
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
    legacy::field address;
    legacy::field password;
    choice_menu<pick_mode> mode;
    asked_part asked;
    advanced_button_t advanced_button;
    widgets::Collapsible<xmpp_advanced> more;
    legacy::form_end end;
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
        parts{.address = legacy::field(colours, "Address (JID)", "user@example.com"),
              .password = legacy::field(colours, "Password", "Password"),
              .mode = skiff::compose::visible(
                  !from || creating,
                  make_choice_menu<pick_mode>(colours, "Account",
                      {"Sign in to an account", "Create a new account"},
                      creating ? 1 : 0, pick_mode{this})),
              .asked = asked_part(colours),
              .advanced_button = skiff::compose::styled(
                  {.width = 120.0f, .height = 36.0f},
                  advanced_button_t(colours.widgets, "Advanced", {})),
              .more = widgets::Collapsible<xmpp_advanced>(colours),
              .end = legacy::form_end(colours, from.has_value())} {
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
using form = form_detail::xmpp_form;
constexpr type_tag<form> form_type(const state&) {
  return {};
}
constexpr type_tag<form> form_type_for(const kept&) {
  return {};
}

}  // namespace mux::proto::xmpp

namespace mux::proto::xmpp::manage_detail {
using namespace ::mux::ui;
inline const muc_room& settings_of(const room_settings_facts& facts) {
  static const muc_room empty;
  return spl::visit(spl::overloaded{[](const muc_room& room) -> const muc_room& { return room; },
                                    [](const auto&) -> const muc_room& { return empty; }},
                    facts.theirs);
}
inline std::vector<std::string> lines_of(std::string_view text) {
  if (text.empty()) return {};
  return std::ranges::to<std::vector<std::string>>(std::views::transform(
      std::views::split(text, '\n'), [](auto line) {
        auto value = std::ranges::to<std::string>(line);
        if (!value.empty() && value.back() == '\r') value.pop_back();
        return value;
      }));
}
// A field's label beside its editor: its words, required marked, its
// description read out as its hint.
inline auto config_label(const palette& colours, const muc_field& f) {
  namespace c = skiff::compose;
  return c::named(f.label, f.description,
                  c::styled({.width = 120.0f, .alignSelf = scene::align::kMiddle},
                            wrapped(nodes::Text(f.label + (f.required ? " *" : ""), 13.0f, colours.text))));
}

// The configuration being written: each field's values by its var, and each
// JID list's rows, by a row's number -- its address, and whether it is
// picked to be removed. The page's own until it is saved.
struct jid_line {
  std::uint32_t row = 0;
  std::string address;
  bool picked = false;
  friend bool operator==(const jid_line&, const jid_line&) = default;
};
struct muc_form {
  std::map<std::string, std::vector<std::string>> values;
  std::map<std::string, std::vector<jid_line>> jids;
  std::uint32_t next_row = 1;
  friend bool operator==(const muc_form&, const muc_form&) = default;
};
// What an editor answers of its field.
struct config_set {
  std::string var;
  std::vector<std::string> values;
};
struct config_option_flipped {
  std::string var, value;
};
struct jid_added {
  std::string var;
};
struct jids_removed {
  std::string var;
};
struct jid_typed {
  std::string var;
  std::uint32_t row = 0;
  std::string address;
};
struct jid_picked {
  std::string var;
  std::uint32_t row = 0;
};
struct muc_form_events {
  auto put(muc_form next) const { return skiff::model::over<muc_form>(skiff::model::setTo(std::move(next))); }
  auto on(const config_set& one, const muc_form& now) const {
    auto next = now;
    next.values.insert_or_assign(one.var, one.values);
    return put(std::move(next));
  }
  auto on(const config_option_flipped& one, const muc_form& now) const {
    auto next = now;
    auto& picked = next.values[one.var];
    if (std::ranges::contains(picked, one.value))
      std::erase(picked, one.value);
    else
      picked.push_back(one.value);
    return put(std::move(next));
  }
  auto on(const jid_added& one, const muc_form& now) const {
    auto next = now;
    next.jids[one.var].push_back(jid_line{next.next_row++, {}, false});
    return put(std::move(next));
  }
  auto on(const jids_removed& one, const muc_form& now) const {
    auto next = now;
    std::erase_if(next.jids[one.var], [](const jid_line& line) { return line.picked; });
    return put(std::move(next));
  }
  auto on(const jid_typed& one, const muc_form& now) const {
    auto next = now;
    if (const auto line = std::ranges::find(next.jids[one.var], one.row, &jid_line::row); line != next.jids[one.var].end())
      line->address = one.address;
    return put(std::move(next));
  }
  auto on(const jid_picked& one, const muc_form& now) const {
    auto next = now;
    if (const auto line = std::ranges::find(next.jids[one.var], one.row, &jid_line::row); line != next.jids[one.var].end())
      line->picked = !line->picked;
    return put(std::move(next));
  }
};
// A field's values as the form has them now: its JID list's addresses, not
// blank; else what its editor said; else as it came.
[[nodiscard]] inline std::vector<std::string> values_in(const muc_form& form, const muc_field& f) {
  if (const auto lines = form.jids.find(f.var); lines != form.jids.end())
    return lines->second | std::views::transform(&jid_line::address) | std::views::filter(std::not_fn(&std::string::empty)) |
           std::ranges::to<std::vector>();
  const auto set = form.values.find(f.var);
  return set == form.values.end() ? f.values : set->second;
}
// The form as it starts: each JID list's rows from what the server sent.
[[nodiscard]] inline muc_form form_of(const muc_room& room) {
  muc_form form;
  for (const muc_field& f : room.fields)
    spl::visit(spl::overloaded{[&](muc_kind::jid_multi) {
                                 auto& lines = form.jids[f.var];
                                 for (const std::string& one : f.values)
                                   lines.push_back(jid_line{form.next_row++, one, false});
                               },
                               [](const auto&) {}},
               f.kind);
  return form;
}
// The answer for a field, as the form has it: one that does not apply -- a
// password where the room is not protected -- as it came.
[[nodiscard]] inline bool applies(const muc_form& form, const muc_room& room, const muc_field& f) {
  return spl::visit(spl::overloaded{[&](muc_field_use::password) {
                                      const auto protection = std::ranges::find_if(room.fields, [](const muc_field& one) {
                                        return spl::visit(spl::overloaded{[](muc_field_use::password_protection) { return true; },
                                                                          [](const auto&) { return false; }},
                                                          one.use);
                                      });
                                      if (protection == room.fields.end())
                                        return true;
                                      const auto now = values_in(form, *protection);
                                      return !now.empty() && muc_boolean_of(now.front()).value_or(false);
                                    },
                                    [](const auto&) { return true; }},
                    f.use);
}
[[nodiscard]] inline muc_field answer_of(const muc_form& form, const muc_room& room, const muc_field& f) {
  auto out = f;
  if (applies(form, room, f))
    out.values = values_in(form, f);
  return out;
}
[[nodiscard]] inline std::optional<std::string> trouble_of(const muc_form& form, const muc_room& room, const muc_field& f) {
  if (!applies(form, room, f))
    return std::nullopt;
  const auto now = answer_of(form, room, f);
  auto error = validate_muc_value(now, now.values);
  if (!error && rules_of(now.kind).jid &&
      !std::ranges::all_of(now.values, [](const std::string& jid) { return jid.empty() || tern::jid::parse(jid).has_value(); }))
    error = "Enter valid JIDs.";
  return error;
}
[[nodiscard]] inline bool modified(const muc_form& form, const muc_room& room, const muc_field& f) {
  const auto now = answer_of(form, room, f).values;
  const auto empty = [](const auto& values) { return values.empty() || (values.size() == 1 && values.front().empty()); };
  if (empty(now) && empty(f.values))
    return false;
  if (rules_of(f.kind).boolean && now.size() == 1 && f.values.size() == 1)
    return muc_boolean_of(now.front()) != muc_boolean_of(f.values.front());
  return now != f.values;
}
[[nodiscard]] inline std::string joined_values(const muc_field& f) {
  return std::ranges::to<std::string>(std::views::join_with(f.values, std::string_view("\n")));
}

// Each editor's acts: what it answers of its field.
struct field_checked {
  std::string var;
  bool on = false;
  using Answer = config_set;
  config_set operator()() const { return {var, {on ? "0" : "1"}}; }
};
struct field_chosen {
  std::string var;
  std::vector<std::string> options;
  using Answer = std::optional<config_set>;
  Answer operator()(std::size_t at) const { return at < options.size() ? Answer(config_set{var, {options[at]}}) : std::nullopt; }
};
struct field_typed {
  static constexpr bool kEveryEdit = true;
  std::string var;
  bool lines = false;
  using Answer = config_set;
  config_set operator()(std::string_view text) const { return {var, lines ? lines_of(text) : std::vector<std::string>{std::string(text)}}; }
};
struct option_flipped {
  std::string var, value;
  using Answer = config_option_flipped;
  config_option_flipped operator()() const { return {var, value}; }
};
struct jid_line_typed {
  static constexpr bool kEveryEdit = true;
  std::string var;
  std::uint32_t row = 0;
  using Answer = jid_typed;
  jid_typed operator()(std::string_view text) const { return {var, row, std::string(text)}; }
};

// A checkbox showing whether the form's values for a field say it is on --
// or an option is among them -- and answering its flip.
template <class Act>
auto form_checkbox(const palette& colours, std::string var, std::optional<std::string> option, Act act) {
  namespace c = skiff::compose;
  using box_t = widgets::internal::Toggle<Act, widgets::internal::CheckboxLook>;
  return c::applied<muc_form>(
      [var, option](const muc_form& form) {
        const auto found = form.values.find(var);
        if (found == form.values.end())
          return std::optional<bool>();
        return std::optional(option ? std::ranges::contains(found->second, *option)
                                    : !found->second.empty() && muc_boolean_of(found->second.front()).value_or(false));
      },
      [](box_t& box, const std::optional<bool>& on) {
        if (on)
          box.setOnNow(*on);
      },
      c::styled({.alignSelf = scene::align::kMiddle}, box_t(colours.widgets, std::move(act))));
}
// A JID list's rows: each address, picked or not, its field's own; Add, and
// Remove for those picked. Rows kept by their numbers, made again only as
// one is picked.
struct jid_row_of {
  const palette* colours;
  std::string var;
  auto operator()(const std::tuple<std::uint32_t, bool, std::string>& item) const {
    namespace c = skiff::compose;
    const auto& [row, picked, address] = item;
    auto box = widgets::internal::Toggle<sends<jid_picked>, widgets::internal::CheckboxLook>(colours->widgets, {{var, row}});
    box.setOnNow(picked);
    box.apply({.alignSelf = scene::align::kMiddle});
    auto field = widgets::internal::TextBox<jid_line_typed>(colours->widgets, "user@example.org", jid_line_typed{var, row});
    field.setText(address);
    return c::row(c::hbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}), std::move(box),
                  c::styled({.height = 36.0f, .grow = scene::axes::kX}, std::move(field)));
  }
};
inline auto jid_list(const palette& colours, const muc_field& f) {
  namespace c = skiff::compose;
  using row_t = std::invoke_result_t<jid_row_of, const std::tuple<std::uint32_t, bool, std::string>&>;
  using rows_t = nodes::MemoRows<std::uint32_t, bool, row_t, jid_row_of>;
  const std::string var = f.var;
  return c::column(c::vbox(6.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      c::spec_for<muc_form>([var](const muc_form& form) {
        const auto lines = form.jids.find(var);
        const auto count = lines == form.jids.end() ? 0u : lines->second.size();
        return scene::Spec{.fillX = true, .height = std::clamp(static_cast<float>(count) * 40.0f, 100.0f, 300.0f)};
      },
          nodes::ScrollContainer(c::projected<muc_form>([var](const muc_form& form) {
            const auto lines = form.jids.find(var);
            return lines == form.jids.end() ? std::vector<std::tuple<std::uint32_t, bool, std::string>>{}
                                            : lines->second | std::views::transform([](const jid_line& one) {
                                                return std::tuple{one.row, one.picked, one.address};
                                              }) | std::ranges::to<std::vector>();
          },
              c::styled({.fillX = true, .autoSize = scene::axes::kY}, rows_t(jid_row_of{&colours, var}))))),
      c::row(c::hbox(6.0f, {.fillX = true, .height = 32.0f}),
             c::styled({.height = 32.0f}, widgets::Button<sends<jid_added>>(colours.widgets, "Add", {{var}})),
             c::spec_for<muc_form>([var](const muc_form& form) {
               const auto lines = form.jids.find(var);
               return scene::Spec{.height = 32.0f, .disabled = lines == form.jids.end() || std::ranges::none_of(lines->second, &jid_line::picked)};
             },
                 widgets::Button<sends<jids_removed>>(colours.widgets, "Remove", {{var}}))));
}
// What a field is edited with, by its kind; its trouble under it.
using field_editor_t = spl::variant<nodes::Text, decltype(form_checkbox(std::declval<const palette&>(), "", std::nullopt, field_checked{})),
                                    choice_menu<field_chosen>,
                                    decltype(skiff::compose::many(skiff::compose::vbox(),
                                        std::vector<decltype(skiff::compose::row(skiff::compose::hbox(), form_checkbox(std::declval<const palette&>(), "", std::optional<std::string>(), option_flipped{}), nodes::Text("", 13.0f, 0u)))>{})),
                                    decltype(jid_list(std::declval<const palette&>(), std::declval<const muc_field&>())),
                                    widgets::internal::TextBox<field_typed>, widgets::internal::TextArea<field_typed>>;
inline field_editor_t field_editor(const palette& colours, const muc_field& f) {
  namespace c = skiff::compose;
  const auto text = [&](bool masked) {
    auto box = widgets::internal::TextBox<field_typed>(colours.widgets, "", field_typed{f.var, false});
    box.setText(joined_values(f));
    box.setMasked(masked);
    box.apply({.fillX = true, .height = 36.0f});
    return field_editor_t(std::in_place_index<5>, std::move(box));
  };
  const auto fixed = [&](std::string said) { return field_editor_t(std::in_place_index<0>, wrapped(nodes::Text(std::move(said), 13.0f, colours.text))); };
  return spl::visit(
      spl::overloaded{
          [&](muc_kind::boolean) {
            return field_editor_t(std::in_place_index<1>,
                                  form_checkbox(colours, f.var, std::nullopt,
                                                field_checked{f.var, !f.values.empty() && muc_boolean_of(f.values.front()).value_or(false)}));
          },
          [&](muc_kind::list_single) {
            const auto selected = f.values.empty() ? f.options.size()
                                                   : static_cast<std::size_t>(std::ranges::find(f.options, f.values.front(),
                                                                                                &std::pair<std::string, std::string>::second) -
                                                                              f.options.begin());
            return field_editor_t(std::in_place_index<2>,
                                  make_choice_menu(colours, "", f.options | std::views::keys | std::ranges::to<std::vector<std::string>>(), selected,
                                                   field_chosen{f.var, f.options | std::views::values | std::ranges::to<std::vector<std::string>>()}));
          },
          [&](muc_kind::list_multi) {
            return field_editor_t(std::in_place_index<3>,
                c::many(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                        f.options | std::views::transform([&](const std::pair<std::string, std::string>& option) {
                          return c::row(c::hbox(8.0f, {.fillX = true, .height = 32.0f}),
                                        form_checkbox(colours, f.var, option.second, option_flipped{f.var, option.second}),
                                        c::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle},
                                                  elided(nodes::Text(option.first, 13.0f, colours.text))));
                        }) | std::ranges::to<std::vector>()));
          },
          [&](muc_kind::text_single) { return text(false); }, [&](muc_kind::text_private) { return text(true); },
          [&](muc_kind::jid_single) { return text(false); },
          [&](muc_kind::text_multi) {
            auto area = widgets::internal::TextArea<field_typed>(colours.widgets, "", field_typed{f.var, true});
            area.setText(joined_values(f));
            area.setSingleLine(false);
            area.setMaxLines(5);
            area.apply({.fillX = true});
            return field_editor_t(std::in_place_index<6>, std::move(area));
          },
          [&](muc_kind::jid_multi) { return field_editor_t(std::in_place_index<4>, jid_list(colours, f)); },
          [&](muc_kind::fixed) { return fixed(joined_values(f)); }, [&](muc_kind::hidden) { return fixed({}); },
          [&](muc_kind::unsupported) { return fixed("Unsupported configuration field."); }},
      f.kind);
}
// A field's row: its label beside its editor, what is wrong with it under
// that; not there where it is hidden, nor where it does not apply.
inline auto config_row(const palette& colours, const muc_room& room, const muc_field& f) {
  namespace c = skiff::compose;
  const bool labelled = spl::visit(spl::overloaded{[](muc_kind::fixed) { return false; }, [](const auto&) { return true; }}, f.kind);
  const bool hidden = spl::visit(spl::overloaded{[](muc_kind::hidden) { return true; }, [](const auto&) { return false; }}, f.kind);
  return c::shown_for<muc_form>([room, f, hidden](const muc_form& form) { return !hidden && applies(form, room, f); },
      c::row(c::hbox(12.0f, {.fillX = true, .autoSize = scene::axes::kY, .maxWidth = 435.0f, .alignSelf = scene::align::kStart}),
             c::visible(labelled, config_label(colours, f)),
             c::column(c::vbox(4.0f, {.autoSize = scene::axes::kY, .grow = scene::axes::kX}),
                       field_editor(colours, f),
                       c::shown_for<muc_form>([room, f](const muc_form& form) { return trouble_of(form, room, f).has_value(); },
                           c::text_for<muc_form>([room, f](const muc_form& form) { return trouble_of(form, room, f).value_or(""); },
                                                 wrapped(nodes::Text("", 12.0f, colours.error)))))));
}
// The room's configuration, as the server gives it to its owners: its title
// and instructions, each field, Refresh and Save -- Save while all is valid
// and something changed, and not while a save is on its way; what was wrong
// with the last, the server's word, under them.
// The settings as the program says, and the room's part of them: what a
// page is made from -- without what a save on its way says, which the
// pages read as it moves.
using facts_t = std::optional<room_settings_facts>;
[[nodiscard]] inline muc_room settled(muc_room room) {
  room.saving = false;
  room.failure.reset();
  return room;
}
[[nodiscard]] inline const muc_room& room_of(const facts_t& facts) {
  static const muc_room none;
  return facts ? settings_of(*facts) : none;
}
inline auto configuration_page(const palette& colours, const muc_room& room) {
  namespace c = skiff::compose;
  const auto changed_and_valid = [room](const muc_form& form) {
    return std::ranges::none_of(room.fields, [&](const muc_field& f) { return trouble_of(form, room, f).has_value(); }) &&
           std::ranges::any_of(room.fields, [&](const muc_field& f) { return modified(form, room, f); });
  };
  const auto saving = [](const facts_t& facts) { return room_of(facts).saving; };
  return c::local<muc_form>(muc_form_events{},
      c::column(c::vbox(12.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {12.0f, 12.0f, 18.0f, 12.0f}}),
          c::visible(!room.title.empty(), wrapped(nodes::Text(room.title, 16.0f, colours.text, true))),
          wrapped(nodes::Text(room.configuring ? std::ranges::to<std::string>(std::views::join_with(room.instructions, std::string_view("\n")))
                                               : "You need Owner permission to change the configuration.",
                              13.0f, colours.dim)),
          // Neither while a save is on its way.
          c::spec_for<facts_t>([saving](const facts_t& facts) { return scene::Spec{.disabled = saving(facts)}; },
              c::column(c::vbox(12.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                  c::styled({.width = 100.0f, .height = 32.0f, .alignSelf = scene::align::kStart},
                            button_for<sends<request::refresh_muc>>(colours.widgets, "Refresh", {})),
                  c::visible(room.configuring,
                      c::spec_for<muc_form>([changed_and_valid](const muc_form& form) {
                        return scene::Spec{.width = 100.0f, .height = 32.0f, .alignSelf = scene::align::kStart, .disabled = !changed_and_valid(form)};
                      },
                          c::asking_for<muc_form>([room](const muc_form& form) {
                            return request::configure_muc{room.fields | std::views::transform([&](const muc_field& f) { return answer_of(form, room, f); }) |
                                                          std::ranges::to<std::vector>()};
                          },
                              widgets::Button<scene::NoAction>(colours.widgets, "Save", {})))))),
          c::shown_for<facts_t>([](const facts_t& facts) { return room_of(facts).saving || room_of(facts).failure.has_value(); },
              c::text_for<facts_t>([](const facts_t& facts) {
                return room_of(facts).saving ? std::string("Saving\u2026") : room_of(facts).failure.value_or("");
              },
                  wrapped(nodes::Text("", 13.0f, colours.dim)))),
          c::many(c::vbox(12.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                  room.fields | std::views::transform([&](const muc_field& f) { return config_row(colours, room, f); }) |
                      std::ranges::to<std::vector>())),
      form_of(room));
}
using configuration_page_t = decltype(configuration_page(std::declval<const palette&>(), std::declval<const muc_room&>()));

// The room: its subject, one's nickname in it -- or, not in it, joining it
// again with a password where it asks one -- and its settings fetched again.
struct room_form {
  std::string topic, nick, password;
  friend bool operator==(const room_form&, const room_form&) = default;
};
// What the room page is made from: the room, and its subject.
struct room_basis {
  muc_room room;
  std::string topic;
  friend bool operator==(const room_basis&, const room_basis&) = default;
};
inline auto room_page(const palette& colours, const room_basis& basis) {
  namespace c = skiff::compose;
  const muc_room& room = basis.room;
  const scene::Spec button{.height = 32.0f, .alignSelf = scene::align::kStart};
  return c::local<room_form>(
      c::column(c::vbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 28.0f, 24.0f, 12.0f}}),
          tab_heading(colours, "Room"),
          wrapped(nodes::Text("Nickname: " + room.nick + " · " + room.affiliation + " · " + room.role, 13.0f, colours.dim)),
          model_field<&room_form::topic>(colours, "Subject", "Room subject"),
          c::asking_for<room_form>([](const room_form& form) { return ::mux::ui::request::room_act{room_action::retopic{form.topic}}; },
                                   c::styled(button, widgets::Button<scene::NoAction>(colours.widgets, "Change subject", {}))),
          model_field<&room_form::nick>(colours, "Nickname", "Your nickname"),
          c::visible(room.joined, c::asking_for<room_form>([](const room_form& form) { return request::muc_identity{form.nick}; },
                                                           c::styled(button, widgets::Button<scene::NoAction>(colours.widgets, "Change nickname", {})))),
          c::visible(!room.joined, model_field<&room_form::password>(colours, "Room password", "Password, if required", true)),
          c::visible(!room.joined, c::asking_for<room_form>([](const room_form& form) { return request::rejoin_muc{form.nick, form.password}; },
                                                            c::styled(button, widgets::Button<scene::NoAction>(colours.widgets, "Join room", {})))),
          c::styled(button, button_for<sends<request::refresh_muc>>(colours.widgets, "Refresh room settings", {}))),
      room_form{basis.topic, room.nick, {}});
}
using room_page_t = decltype(room_page(std::declval<const palette&>(), std::declval<const room_basis&>()));

// Affiliation belongs to a bare account; role belongs to a joined occupant.
// Merge the two lists without inventing duplicate accounts, while keeping
// each nickname's role when one account joins from several clients.
inline std::vector<std::string> permission_rows(const muc_room& room) {
  struct entry { std::string jid, affiliation; std::map<std::string, std::string> occupants; };
  std::map<std::pair<bool, std::string>, entry> people;
  std::ranges::for_each(room.people, [&](const auto& person) {
    const auto key = std::pair{person.jid.empty(), person.jid.empty() ? person.nick : person.jid};
    auto& kept = people[key];
    kept.jid = person.jid;
    kept.affiliation = person.affiliation;
    kept.occupants.insert_or_assign(person.nick, person.role);
  });
  std::ranges::for_each(room.affiliations, [&](const auto& person) {
    auto& kept = people[{false, person.jid}];
    kept.jid = person.jid;
    kept.affiliation = person.affiliation;
  });
  return std::ranges::to<std::vector<std::string>>(std::views::transform(people, [](const auto& item) {
    const auto& person = item.second;
    auto text = person.jid.empty() ? item.first.second : person.jid;
    text += " · Affiliation: " + person.affiliation;
    const auto occupants = std::views::transform(person.occupants, [](const auto& occupant) {
      return occupant.first + " (Role: " + occupant.second + ")";
    });
    if (!person.occupants.empty())
      text += " · " + std::ranges::to<std::string>(std::views::join_with(occupants, std::string_view(", ")));
    return text;
  }));
}
// Roles & Permissions: who is in it and how; an affiliation or a role set
// on an account or a nickname; and, for its owner, the room destroyed --
// its address typed again to confirm.
struct roles_form {
  std::string user, rank, reason, confirm;
  friend bool operator==(const roles_form&, const roles_form&) = default;
};
// And the roles page: the room, and its address.
struct roles_basis {
  muc_room room;
  std::string id;
  friend bool operator==(const roles_basis&, const roles_basis&) = default;
};
inline auto roles_page(const palette& colours, const roles_basis& basis) {
  namespace c = skiff::compose;
  const muc_room& room = basis.room;
  const bool owner = room.affiliation == "owner";
  return c::local<roles_form>(
      c::column(c::vbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 28.0f, 24.0f, 12.0f}}),
          tab_heading(colours, "Roles & Permissions"),
          wrapped(nodes::Text("Affiliations use a real bare JID: none, member, admin, owner, outcast (ban). Roles use an occupant nickname: visitor, "
                              "participant, moderator, none (kick). The server checks your privileges.",
                              13.0f, colours.dim)),
          c::many(c::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                  permission_rows(room) | std::views::transform([&](std::string text) { return wrapped(nodes::Text(std::move(text), 13.0f, colours.text)); }) |
                      std::ranges::to<std::vector>()),
          model_field<&roles_form::user>(colours, "User JID or occupant nickname", "user@example.org or nickname"),
          model_field<&roles_form::rank>(colours, "Affiliation or role", "member"),
          c::asking_for<roles_form>([](const roles_form& form) { return request::muc_privilege{form.user, form.rank, ""}; },
              c::styled({.height = 32.0f, .alignSelf = scene::align::kStart, .disabled = room.affiliation != "owner" && room.affiliation != "admin"},
                        widgets::Button<scene::NoAction>(colours.widgets, "Set affiliation", {}))),
          c::asking_for<roles_form>([](const roles_form& form) { return request::muc_privilege{form.user, "", form.rank}; },
              c::styled({.height = 32.0f, .alignSelf = scene::align::kStart, .disabled = room.role != "moderator"},
                        widgets::Button<scene::NoAction>(colours.widgets, "Set occupant role", {}))),
          c::visible(owner, c::column(c::vbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}),
              wrapped(nodes::Text("Destroy room permanently (owner only). Type the room address to confirm.", 13.0f, colours.error)),
              model_field<&roles_form::reason>(colours, "Reason", "Optional reason"),
              model_field<&roles_form::confirm>(colours, "Confirm room address", basis.id),
              c::asking_for<roles_form>([room = basis.id](const roles_form& form) {
                return form.confirm == room ? std::optional(request::destroy_muc{form.reason}) : std::nullopt;
              },
                  c::styled({.height = 32.0f, .alignSelf = scene::align::kStart},
                            widgets::Button<scene::NoAction>(colours.widgets, "Destroy room", {})))))));
}
using roles_page_t = decltype(roles_page(std::declval<const palette&>(), std::declval<const roles_basis&>()));
// A page made from what the room is, again where that changes.
template <class Page, class Basis, class Of, class Make>
auto made_from(const palette& colours, Of of, Make make) {
  namespace c = skiff::compose;
  return c::projected<facts_t>([of](const facts_t& facts) { return facts ? std::optional<Basis>(of(*facts)) : std::nullopt; },
      c::mount<Page, Basis>([at = &colours, make](const Basis& basis) { return std::tuple{make(*at, basis)}; },
                            {.fillX = true, .autoSize = scene::axes::kY}));
}
}
export namespace mux::proto::xmpp::manage {
inline bool tab_visible(room, const ui::room_settings_facts& facts) { return facts.group; }
inline bool tab_visible(configuration, const ui::room_settings_facts& facts) { return facts.group; }
inline bool tab_visible(roles, const ui::room_settings_facts& facts) { return facts.group; }
constexpr std::string_view tab_title(room) { return "Room"; }
constexpr std::string_view tab_title(configuration) { return "Configuration"; }
constexpr std::string_view tab_title(roles) { return "Roles & Permissions"; }
inline ui::icon_t tab_icon(room) { return ui::icon::people{}; }
inline ui::icon_t tab_icon(configuration) { return ui::icon::gear{}; }
inline ui::icon_t tab_icon(roles) { return ui::icon::people{}; }
// Each tab's page, made again from the room as the server says it.
inline auto make_manage_page(room, const ui::palette& colours, const ui::room_settings_facts&) {
  using namespace manage_detail;
  return made_from<room_page_t, room_basis>(colours, [](const ui::room_settings_facts& facts) { return room_basis{settled(settings_of(facts)), facts.topic}; },
                                            [](const ui::palette& at, const room_basis& basis) { return room_page(at, basis); });
}
inline auto make_manage_page(configuration, const ui::palette& colours, const ui::room_settings_facts&) {
  using namespace manage_detail;
  return made_from<configuration_page_t, muc_room>(colours, [](const ui::room_settings_facts& facts) { return settled(settings_of(facts)); },
                                                   [](const ui::palette& at, const muc_room& room) { return configuration_page(at, room); });
}
inline auto make_manage_page(roles, const ui::palette& colours, const ui::room_settings_facts&) {
  using namespace manage_detail;
  return made_from<roles_page_t, roles_basis>(colours, [](const ui::room_settings_facts& facts) { return roles_basis{settled(settings_of(facts)), facts.id}; },
                                              [](const ui::palette& at, const roles_basis& basis) { return roles_page(at, basis); });
}
}

export namespace mux::proto::xmpp::settings {
struct profile_policy {
  static constexpr bool banners = false;
  static auto presences() { return std::vector<std::pair<std::string, availability_t>>{{"Online", availability::online{}}, {"Free to chat", availability::chat{}}, {"Away", availability::away{}}, {"Extended away", availability::extended_away{}}, {"Do not disturb", availability::do_not_disturb{}}, {"Offline", availability::offline{}}}; }
};
constexpr std::string_view page_title(profile) { return "Profile"; }
inline ui::icon_t page_icon(profile) { return ui::icon::info{}; }
constexpr type_tag<ui::profile_editor<profile_policy>> page_type(profile) { return {}; }
}
