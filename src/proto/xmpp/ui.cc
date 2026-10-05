// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui.proto.xmpp -- What XMPP shows of its own in the client's UI: its
// account form, found by ADL where the panels are made.
export module mux.ui.proto.xmpp;

import std;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
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

namespace mux::proto::xmpp::form_detail {
using namespace ::mux::ui;

// What "Advanced" folds out on an XMPP form: the resource, where to connect,
// and PLAIN without TLS. Its height is what its last layout took.
template <class Actions>
struct xmpp_advanced : nodes::Stack {
  using plain_toggle = widgets::Toggle<ask<Actions, &Actions::toggle_plain>>;
  // The switch and what it says, side by side.
  struct plain_row : nodes::Stack {
    struct parts_t {
      plain_toggle plain;
      nodes::Text label;
    } parts;
    plain_row(Actions* a, const palette& colours)
        : parts{.plain = plain_toggle(colours.widgets, {a}),
                .label = nodes::Text("Allow PLAIN without TLS. Only for a test server on this machine: never over a network.",
                                     13.0f, colours.error)} {
      this->setHorizontal();
      this->setGap(10.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY});
      parts.label.setWrapped(true);
      parts.label.apply({.grow = scene::axes::kX});
    }
  };
  struct parts_t {
    field resource;
    field host;
    field port;
    plain_row row;
  } parts;

  xmpp_advanced(Actions* a, const palette& colours) : parts{.resource = field(colours, "Device name (resource)", "mux", "mux"),
              .host = field(colours, "Host", "from the domain's SRV records"),
              .port = field(colours, "Port", "5222"),
              .row = plain_row(a, colours)} {
    fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 0.0f, 12.0f, 0.0f}});
    this->setGap(8.0f);
  }
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
    return lines | std::views::join_with(between) | std::ranges::to<std::string>();
  };
  const std::string read = splice::visit(
      splice::overloaded{[&](field_shown::read) { return one.label.empty() || one.value.empty() ? joined(one.value, "\n") + one.label
                                                                                                 : one.label + ": " + joined(one.value, "\n"); },
                         [](const auto&) { return std::string(); }},
      one.shown);
  const std::vector<std::string> said{read, one.choices.empty() ? std::string() : "One of: " + joined(one.choices, ", "),
                                      joined(one.links, "\n")};
  return joined(said | std::views::filter([](const std::string& line) { return !line.empty(); }) |
                    std::ranges::to<std::vector<std::string>>(),
                "\n");
}

// A field of what the server asks to register: its picture where it has
// one, what is said of it, and its box -- masked for a secret; none for
// one only read; nothing at all shown of a hidden one, sent back as it came.
struct asked_row : nodes::Stack {
  struct parts_t {
    nodes::Image<sent_picture> picture;
    nodes::Text note;
    field box;
  } parts;
  std::string var;
  field_shown_t shown;
  std::vector<std::string> held;

  asked_row(const palette& colours, const registration_field& one)
      : parts{.picture = nodes::Image<sent_picture>(picture_of(one), nodes::fit::contain{}),
              .note = nodes::Text(note_of(one), 13.0f, colours.dim),
              .box = field(colours, one.label + (one.required ? " (required)" : ""), one.desc,
                           one.value.empty() ? std::string() : one.value.front())},
        var(one.var),
        shown(one.shown),
        held(one.value) {
    this->setGap(6.0f);
    fState.apply({.fillX = true, .autoSize = scene::axes::kY});
    parts.picture.setVisible(!one.picture.empty());
    parts.picture.apply({.width = 240.0f, .height = 90.0f, .cornerRadius = 6.0f, .background = colours.tile});
    parts.note.setWrapped(true);
    parts.note.setSelectable(true);
    parts.note.apply({.fillX = true});
    parts.note.setVisible(!parts.note.text().empty());
    splice::visit(splice::overloaded{[&](field_shown::typed) {},
                                     [&](field_shown::masked) { parts.box.parts.box.setMasked(true); },
                                     [&](field_shown::hidden) { this->setVisible(false); },
                                     [&](field_shown::read) { parts.box.setVisible(false); }},
                  shown);
  }
  // Its answer: what is typed, or a hidden field's value as it came; none
  // for one only read.
  [[nodiscard]] std::optional<registration_answer> answer() const {
    return splice::visit(
        splice::overloaded{[](field_shown::read) { return std::optional<registration_answer>(); },
                           [&](field_shown::hidden) {
                             return std::optional(registration_answer{.var = var, .value = held.empty() ? std::string() : held.front()});
                           },
                           [&](const auto&) { return std::optional(registration_answer{.var = var, .value = parts.box.text()}); }},
        shown);
  }
};

// What the server asked, under the form: what it says, and its fields.
struct asked_part : nodes::Stack {
  struct parts_t {
    nodes::Text instructions;
    std::vector<asked_row> rows;
  } parts;
  explicit asked_part(const palette& colours) : parts{.instructions = nodes::Text("", 13.0f, colours.text)} {
    this->setGap(10.0f);
    fState.apply({.fillX = true, .autoSize = scene::axes::kY});
    parts.instructions.setWrapped(true);
    parts.instructions.setSelectable(true);
    parts.instructions.apply({.fillX = true});
    this->setVisible(false);
  }
  void show(const palette& colours, const registration_asked& asked) {
    parts.instructions.setText(asked.instructions + (asked.page ? "\nOr make the account on the server's page: " + *asked.page : std::string()));
    parts.instructions.setVisible(!parts.instructions.text().empty());
    parts.rows = asked.fields | std::views::transform([&](const registration_field& one) { return asked_row(colours, one); }) |
                 std::ranges::to<std::vector<asked_row>>();
    this->setVisible(true);
    this->invalidateLayout();
  }
  [[nodiscard]] std::vector<registration_answer> answers() const {
    return parts.rows | std::views::transform([](const asked_row& row) { return row.answer(); }) | std::views::filter([](const auto& one) { return one.has_value(); }) |
           std::views::transform([](const auto& one) { return *one; }) | std::ranges::to<std::vector<registration_answer>>();
  }
};

// An XMPP account's settings: its JID and password, and "Advanced" folds out
// the rest. It fills the column it is given.
template <class Actions>
struct xmpp_form : nodes::Stack {
  // What the add-account pane says of the protocol.
  static constexpr std::string_view note = "An address like user@example.com, on a server such as Prosody or ejabberd.";
  Actions* actions = nullptr;
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

  using advanced_button_t = widgets::Button<ask<Actions, &Actions::toggle_advanced>>;
  struct parts_t {
    field address;
    field password;
    choice_menu<pick_mode> mode;
    asked_part asked;
    advanced_button_t advanced_button;
    widgets::Collapsible<xmpp_advanced<Actions>> more;
    form_end<Actions> end;
  } parts;

  xmpp_form(Actions* a, const palette& colours, const std::optional<::mux::proto::xmpp::kept>& from)
      : actions(a),
        creating(from && from->create.value_or(false)),
        answered(from ? from->answers.value_or(std::vector<registration_answer>{}) : std::vector<registration_answer>{}),
        colours_(&colours),
        parts{.address = field(colours, "Address (JID)", "user@example.com"),
              .password = field(colours, "Password", "Password"),
              .mode = choice_menu<pick_mode>(colours, "Account", {"Sign in to an account", "Create a new account"},
                                             creating ? 1 : 0, pick_mode{this}),
              .asked = asked_part(colours),
              .advanced_button = advanced_button_t(colours.widgets, "Advanced", {a}),
              .more = widgets::Collapsible<xmpp_advanced<Actions>>(a, colours),
              .end = form_end<Actions>(colours, a, from.has_value())} {
    auto& [address, password, mode, asked, advanced_button, more, end] = parts;
    // An account kept and signed in to: one already, nothing to choose.
    mode.setVisible(!from || creating);
    auto& folded = more.child().parts;
    fState.apply({.fillX = true, .autoSize = scene::axes::kY});
    this->setGap(12.0f);
    password.parts.box.setMasked(true);
    advanced_button.apply({.width = 120.0f, .height = 36.0f});
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
  void onKey(scene::phase::bubble, const scene::key::down& press, scene::Reply& reply) {
    if (press.key == scene::keys::kEnter) {
      actions->submit_login();
      reply.handle();
    }
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
