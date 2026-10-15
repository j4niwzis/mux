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
  const auto* room = facts.theirs.template get_if<muc_room>();
  return room ? *room : empty;
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
struct config_label : nodes::Text {
  std::string description;
  config_label(const palette& colours, const muc_field& f)
      : nodes::Text(f.label + (f.required ? " *" : ""), 13.0f, colours.text), description(f.description) {
    this->setWrapped(true);
    this->apply({.width = 120.0f, .alignSelf = scene::align::kMiddle});
  }
  scene::Semantics semantics() const {
    auto out = nodes::Text::semantics(); out.fHint = description; return out;
  }
};
struct choice_press { void operator()(std::size_t) const {} };
struct config_option : skiff::compose::Stacked {
  struct parts_t { legacy::check_box check; nodes::Text label; } parts;
  std::string value;
  config_option(const palette& colours, const std::pair<std::string, std::string>& option, bool selected)
      : Stacked(skiff::compose::hbox(8.0f, {.fillX = true, .height = 32.0f})),
        parts{.check = legacy::check_box(colours, selected, option.first),
              .label = skiff::compose::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle},
                  elided(nodes::Text(option.first, 13.0f, colours.text)))}, value(option.second) {}
};
struct config_options : skiff::compose::Stacked {
  struct parts_t { std::vector<config_option> rows; } parts;
  config_options(const palette& colours, const muc_field& field)
      : Stacked(skiff::compose::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY})),
        parts{.rows = std::ranges::to<std::vector<config_option>>(std::views::transform(field.options, [&](const auto& option) {
          return config_option(colours, option, std::ranges::contains(field.values, option.second));
        }))} {}
  auto values() const {
    return std::ranges::to<std::vector<std::string>>(std::views::transform(
        std::views::filter(parts.rows, [](const auto& row) { return row.parts.check.on(); }), &config_option::value));
  }
};
// Gajim's JID-multi field is an editable list with Add and Remove controls,
// rather than a text area whose line breaks stand for separate addresses.
struct jid_list : skiff::compose::Stacked {
  enum class edit { add, remove };
  struct press {
    std::shared_ptr<std::optional<edit>> pending;
    edit what;
    void operator()() const { *pending = what; }
  };
  struct entry : skiff::compose::Stacked {
    struct parts_t { legacy::check_box selected; legacy::field address; } parts;
    entry(const palette& colours, const std::string& value)
        : Stacked(skiff::compose::hbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY})),
          parts{.selected = legacy::check_box(colours, false, "Select address"),
              .address = legacy::field(colours, "", "user@example.org", value)} {
      parts.selected.apply({.alignSelf = scene::align::kMiddle});
      parts.address.apply({.grow = scene::axes::kX});
      parts.address.parts.caption.setVisible(false);
    }
  };
  struct entries : skiff::compose::Stacked {
    struct parts_t { std::vector<entry> rows; } parts;
    entries(const palette& colours, const auto& values)
        : Stacked(skiff::compose::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY})),
          parts{.rows = std::ranges::to<std::vector<entry>>(std::views::transform(values,
              [&](const auto& value) { return entry(colours, value); }))} {}
  };
  struct toolbar : skiff::compose::Stacked {
    struct parts_t { widgets::Button<press> add, remove; } parts;
    toolbar(const palette& colours, const std::shared_ptr<std::optional<edit>>& pending)
        : Stacked(skiff::compose::hbox(6.0f, {.fillX = true, .height = 32.0f})),
          parts{.add = widgets::Button<press>(colours.widgets, "Add", {pending, edit::add}),
              .remove = widgets::Button<press>(colours.widgets, "Remove", {pending, edit::remove})} {
      parts.add.apply({.height = 32.0f}); parts.remove.apply({.height = 32.0f});
    }
  };
  const palette* colours;
  std::shared_ptr<std::optional<edit>> pending = std::make_shared<std::optional<edit>>();
  struct parts_t { nodes::ScrollContainer<entries> list; toolbar buttons; } parts;
  jid_list(const palette& colours, const muc_field& field)
      : Stacked(skiff::compose::vbox(6.0f, {.fillX = true, .autoSize = scene::axes::kY})), colours(&colours),
        parts{.list = nodes::ScrollContainer<entries>(entries(colours, field.values)), .buttons = toolbar(colours, pending)} {
    parts.list.apply({.fillX = true, .height = 100.0f});
  }
  auto values() const {
    const auto& rows = std::get<0>(parts.list.fChildren).parts.rows;
    return std::ranges::to<std::vector<std::string>>(std::views::transform(
        std::views::filter(rows, [](const auto& row) { return !row.parts.address.text().empty(); }),
        [](const auto& row) { return row.parts.address.text(); }));
  }
  bool wantsTick() const { return true; }
  void update(double) {
    auto& rows = std::get<0>(parts.list.fChildren).parts.rows;
    if (const auto asked = std::exchange(*pending, std::nullopt)) {
      switch (*asked) {
        case edit::add: rows.emplace_back(*colours, ""); break;
        case edit::remove: std::erase_if(rows, [](const auto& row) { return row.parts.selected.on(); }); break;
      }
      parts.list.apply({.height = std::clamp(static_cast<float>(rows.size()) * 40.0f, 100.0f, 300.0f)});
      this->invalidateLayout();
    }
    parts.buttons.parts.remove.setEnabled(std::ranges::any_of(rows, [](const auto& row) { return row.parts.selected.on(); }));
  }
};
struct config_row : skiff::compose::Stacked {
  struct editor : skiff::compose::Stacked {
    struct parts_t {
      nodes::Text fixed;
      legacy::check_box check;
      choice_menu<choice_press> single;
      nodes::ScrollContainer<config_options> multiple;
      jid_list addresses;
      legacy::field text;
      nodes::Text error;
    } parts;
    editor(const palette& colours, const muc_field& f)
        : Stacked(skiff::compose::vbox(4.0f, {.autoSize = scene::axes::kY, .grow = scene::axes::kX})),
          parts{.fixed = wrapped(nodes::Text(joined(f), 13.0f, colours.text)),
              .check = legacy::check_box(colours, !f.values.empty() && muc_boolean_of(f.values.front()).value_or(false), f.label, f.description),
              .single = make_choice_menu(colours, "", std::ranges::to<std::vector<std::string>>(
                  std::views::transform(f.options, &std::pair<std::string, std::string>::first)), selected(f), choice_press{}),
              .multiple = nodes::ScrollContainer<config_options>(config_options(colours, f)),
              .addresses = jid_list(colours, f),
              .text = legacy::field(colours, "", "", joined(f)),
              .error = wrapped(nodes::Text("", 12.0f, colours.error))} {
      parts.fixed.setVisible(false); parts.check.setVisible(false); parts.single.setVisible(false);
      parts.multiple.setVisible(false); parts.addresses.setVisible(false); parts.text.setVisible(false); parts.error.setVisible(false);
      parts.text.parts.caption.setVisible(false);
      parts.multiple.apply({.fillX = true, .height = std::clamp(static_cast<float>(f.options.size()) * 32.0f, 100.0f, 300.0f)});
      spl::visit(spl::overloaded{
          [&](muc_kind::boolean) { parts.check.setVisible(true); },
          [&](muc_kind::list_single) { parts.single.setVisible(true); },
          [&](muc_kind::list_multi) { parts.multiple.setVisible(true); },
          [&](muc_kind::text_single) { parts.text.setVisible(true); },
          [&](muc_kind::text_private) { parts.text.setVisible(true); parts.text.parts.box.setMasked(true); },
          [&](muc_kind::text_multi) { multiline(); },
          [&](muc_kind::jid_single) { parts.text.setVisible(true); },
          [&](muc_kind::jid_multi) { parts.addresses.setVisible(true); },
          [&](muc_kind::fixed) { parts.fixed.setVisible(true); },
          [&](muc_kind::hidden) {},
          [&](muc_kind::unsupported) { parts.fixed.setText("Unsupported configuration field."); parts.fixed.setVisible(true); }}, f.kind);
    }
    static std::string joined(const muc_field& f) {
      return std::ranges::to<std::string>(std::views::join_with(f.values, std::string_view("\n")));
    }
    static std::size_t selected(const muc_field& f) {
      if (f.values.empty()) return f.options.size();
      return static_cast<std::size_t>(std::ranges::find(f.options, f.values.front(), &std::pair<std::string, std::string>::second) - f.options.begin());
    }
    void multiline() { parts.text.setVisible(true); parts.text.multi_line(5); }
  };
  struct parts_t { config_label label; editor input; } parts;
  muc_field original;
  bool applicable = true, field_visible = true;
  void set_applicable(bool on) {
    applicable = on;
    this->setVisible(on && field_visible);
  }
  config_row(const palette& colours, const muc_field& f)
      : Stacked(skiff::compose::hbox(12.0f, {.fillX = true, .autoSize = scene::axes::kY, .maxWidth = 435.0f, .alignSelf = scene::align::kStart})),
        parts{.label = config_label(colours, f), .input = editor(colours, f)}, original(f) {
    spl::visit(spl::overloaded{
        [&](muc_kind::text_single) {}, [&](muc_kind::text_private) {}, [&](muc_kind::text_multi) {},
        [&](muc_kind::jid_single) {}, [&](muc_kind::jid_multi) {}, [&](muc_kind::boolean) {},
        [&](muc_kind::list_single) {}, [&](muc_kind::list_multi) {},
        [&](muc_kind::fixed) { parts.label.setVisible(false); },
        [&](muc_kind::hidden) { field_visible = false; setVisible(false); }, [&](muc_kind::unsupported) {}}, f.kind);
  }
  muc_field answer() const {
    if (!applicable) return original;
    auto out = original;
    const auto& input = parts.input.parts;
    spl::visit(spl::overloaded{
        [&](muc_kind::boolean) { out.values = {input.check.on() ? "1" : "0"}; },
        [&](muc_kind::list_single) {
          out.values = input.single.current < out.options.size()
              ? std::vector<std::string>{out.options[input.single.current].second} : std::vector<std::string>{};
        },
        [&](muc_kind::list_multi) { out.values = std::get<0>(input.multiple.fChildren).values(); },
        [&](muc_kind::text_single) { out.values = {input.text.text()}; },
        [&](muc_kind::text_private) { out.values = {input.text.text()}; },
        [&](muc_kind::text_multi) { out.values = lines_of(input.text.text()); },
        [&](muc_kind::jid_single) { out.values = {input.text.text()}; },
        [&](muc_kind::jid_multi) { out.values = input.addresses.values(); },
        [&](muc_kind::fixed) {}, [&](muc_kind::hidden) {}, [&](muc_kind::unsupported) {}}, out.kind);
    return out;
  }
  bool validate() {
    if (!applicable) return true;
    const auto current = answer();
    auto error = validate_muc_value(current, current.values);
    if (!error && rules_of(current.kind).jid && !std::ranges::all_of(current.values, [](const auto& jid) {
          return jid.empty() || tern::jid::parse(jid).has_value();
        })) error = "Enter valid JIDs.";
    parts.input.parts.error.setText(error.value_or(""));
    parts.input.parts.error.setVisible(error.has_value());
    return !error;
  }
  bool modified() const {
    const auto now = answer().values;
    const auto empty = [](const auto& values) { return values.empty() || (values.size() == 1 && values.front().empty()); };
    if (empty(now) && empty(original.values)) return false;
    if (rules_of(original.kind).boolean && now.size() == 1 && original.values.size() == 1)
      return muc_boolean_of(now.front()) != muc_boolean_of(original.values.front());
    return now != original.values;
  }
};
struct room_page : skiff::compose::Stacked {
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
    legacy::field topic;
    widgets::Button<topic_press> set_topic;
    legacy::field nick;
    widgets::Button<nick_press> set_nick;
    legacy::field password;
    widgets::Button<rejoin_press> rejoin;
    button_for<sends<request::refresh_muc>> refresh;

  } parts;
  room_page(const palette& colours, const room_settings_facts& facts)
      : Stacked(skiff::compose::vbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 28.0f, 24.0f, 12.0f}})),
        parts{.heading = tab_heading(colours, "Room"),
            .status = wrapped(nodes::Text("Nickname: " + settings_of(facts).nick + " · " + settings_of(facts).affiliation + " · " + settings_of(facts).role, 13.0f, colours.dim)),
            .topic = legacy::field(colours, "Subject", "Room subject", facts.topic),
            .set_topic = widgets::Button<topic_press>(colours.widgets, "Change subject", {this}),
            .nick = legacy::field(colours, "Nickname", "Your nickname", settings_of(facts).nick),
            .set_nick = widgets::Button<nick_press>(colours.widgets, "Change nickname", {this}),
            .password = legacy::field(colours, "Room password", "Password, if required"),
            .rejoin = widgets::Button<rejoin_press>(colours.widgets, "Join room", {this}),
            .refresh = button_for<sends<request::refresh_muc>>(colours.widgets, "Refresh room settings", {})} {
    parts.password.parts.box.setMasked(true);
    parts.password.setVisible(!settings_of(facts).joined); parts.rejoin.setVisible(!settings_of(facts).joined);
    parts.set_nick.setVisible(settings_of(facts).joined);
    parts.rejoin.apply({.height = 32.0f});
    for (scene::Node* n : std::initializer_list<scene::Node*>{&parts.set_topic, &parts.set_nick, &parts.refresh}) n->apply({.height = 32.0f});
  }
};
struct configuration_page : skiff::compose::Stacked {
  struct save {
    configuration_page* page;
    using Answer = request::configure_muc;
    Answer operator()() const {
      page->submitted = true;
      page->parts.submit.setEnabled(false);
      page->parts.feedback.setText("Saving…"); page->parts.feedback.setVisible(true);
      page->parts.refresh.setEnabled(false);
      std::ranges::for_each(page->parts.fields, [](auto& row) { row.setDisabled(true); });
      return {std::ranges::to<std::vector<muc_field>>(std::views::transform(page->parts.fields, &config_row::answer))};
    }
  };
  struct parts_t {
    nodes::Text title, instructions;
    button_for<sends<request::refresh_muc>> refresh;
    widgets::Button<save> submit;
    nodes::Text feedback;
    std::vector<config_row> fields;
  } parts;
  configuration_page(const palette& colours, const room_settings_facts& facts)
      : Stacked(skiff::compose::vbox(12.0f, {.fillX = true, .autoSize = scene::axes::kY,
            .padding = {12.0f, 12.0f, 18.0f, 12.0f}})),
        parts{.title = wrapped(nodes::Text(settings_of(facts).title, 16.0f, colours.text, true)),
            .instructions = wrapped(nodes::Text(settings_of(facts).configuring
                ? std::ranges::to<std::string>(std::views::join_with(settings_of(facts).instructions, std::string_view("\n")))
                : "You need Owner permission to change the configuration.", 13.0f, colours.dim)),
            .refresh = button_for<sends<request::refresh_muc>>(colours.widgets, "Refresh", {}),
            .submit = widgets::Button<save>(colours.widgets, "Save", {this}),
            .feedback = wrapped(nodes::Text("", 13.0f, colours.dim)),
            .fields = std::ranges::to<std::vector<config_row>>(std::views::transform(settings_of(facts).fields,
                [&](const auto& field) { return config_row(colours, field); }))} {
    parts.title.setVisible(!settings_of(facts).title.empty());
    parts.instructions.setVisible(!parts.instructions.text().empty());
    parts.submit.setEnabled(false);
    parts.submit.setVisible(settings_of(facts).configuring);
    parts.submit.apply({.width = 100.0f, .height = 32.0f, .alignSelf = scene::align::kStart});
    parts.refresh.apply({.width = 100.0f, .height = 32.0f, .alignSelf = scene::align::kStart});
    parts.feedback.setVisible(false);
    refresh_dependencies();
  }
  void refresh_dependencies() {
    const auto protection = std::ranges::find_if(parts.fields, [](const auto& row) {
      return spl::visit(spl::overloaded{
          [](muc_field_use::password_protection) { return true; },
          [](const auto&) { return false; }}, row.original.use);
    });
    const bool protected_room = protection == parts.fields.end() ||
        protection->parts.input.parts.check.on();
    std::ranges::for_each(parts.fields, [&](auto& row) {
      spl::visit(spl::overloaded{
          [&](muc_field_use::password) { row.set_applicable(protected_room); },
          [](const auto&) {}}, row.original.use);
    });
  }
  void configuration_failed(std::string reason) {
    submitted = false;
    parts.refresh.setEnabled(true);
    std::ranges::for_each(parts.fields, [](auto& row) { row.setDisabled(false); });
    parts.feedback.setText(std::move(reason)); parts.feedback.setVisible(true);
    this->markDamaged();
  }
  bool submitted = false;
  bool wantsTick() const { return !submitted; }
  void update(double) {
    parts.submit.action().page = this;
    refresh_dependencies();
    const bool valid = std::ranges::count_if(parts.fields, [](auto& row) { return !row.validate(); }) == 0;
    const bool modified = std::ranges::any_of(parts.fields, &config_row::modified);
    parts.submit.setEnabled(!submitted && valid && modified);
  }
};
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
struct roles_page : skiff::compose::Stacked {
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
    legacy::field user, rank;
    widgets::Button<grant> affiliation, role;
    nodes::Text destruction;
    legacy::field reason, confirm;
    widgets::Button<destroy> remove;
  } parts;
  bool wantsTick() const { return true; }
  void update(double) {
    parts.affiliation.action().page = this;
    parts.role.action().page = this;
    parts.remove.action().page = this;
  }
  roles_page(const palette& colours, const room_settings_facts& facts)
      : Stacked(skiff::compose::vbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 28.0f, 24.0f, 12.0f}})),
        parts{.heading = tab_heading(colours, "Roles & Permissions"),
            .explanation = wrapped(nodes::Text("Affiliations use a real bare JID: none, member, admin, owner, outcast (ban). Roles use an occupant nickname: visitor, participant, moderator, none (kick). The server checks your privileges.", 13.0f, colours.dim)),
            .user = legacy::field(colours, "User JID or occupant nickname", "user@example.org or nickname"),
            .rank = legacy::field(colours, "Affiliation or role", "member"),
            .affiliation = widgets::Button<grant>(colours.widgets, "Set affiliation", {this, true}),
            .role = widgets::Button<grant>(colours.widgets, "Set occupant role", {this, false}),
            .destruction = wrapped(nodes::Text("Destroy room permanently (owner only). Type the room address to confirm.", 13.0f, colours.error)),
            .reason = legacy::field(colours, "Reason", "Optional reason"),
            .confirm = legacy::field(colours, "Confirm room address", facts.id),
            .remove = widgets::Button<destroy>(colours.widgets, "Destroy room", {this, facts.id})} {
    const auto& room = settings_of(facts);
    parts.people = std::ranges::to<std::vector<nodes::Text>>(std::views::transform(permission_rows(room), [&](auto text) {
      return wrapped(nodes::Text(std::move(text), 13.0f, colours.text));
    }));
    parts.affiliation.apply({.height = 32.0f, .disabled = room.affiliation != "owner" && room.affiliation != "admin"});
    parts.role.apply({.height = 32.0f, .disabled = room.role != "moderator"});
    const bool owner = room.affiliation == "owner";
    parts.destruction.setVisible(owner); parts.reason.setVisible(owner); parts.confirm.setVisible(owner);
    parts.remove.setVisible(owner); parts.remove.apply({.height = 32.0f});
  }
};
}
export namespace mux::proto::xmpp {
namespace manage { struct room {}; struct configuration {}; struct roles {}; }
constexpr ui::manage_tab_list<manage::room, manage::configuration, manage::roles> manage_tabs(const state&) { return {}; }
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
template <class Box> constexpr type_tag<manage_detail::room_page> page_type(room, type_tag<Box>) { return {}; }
template <class Box> constexpr type_tag<manage_detail::configuration_page> page_type(configuration, type_tag<Box>) { return {}; }
template <class Box> constexpr type_tag<manage_detail::roles_page> page_type(roles, type_tag<Box>) { return {}; }
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
