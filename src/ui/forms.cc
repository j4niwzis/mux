// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:forms -- Form rows, and the account forms.
export module mux.ui:forms;

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
import :base;
import :themes;

export namespace mux::ui {

// ---- a form row: a caption and a field -----------------------------------

struct field : nodes::Stack {
  struct parts_t {
    nodes::Text caption;
    widgets::TextArea<> box;
  } parts;

  // Declared: the caption over the field, which sits on a plate.
  field(std::string label, std::string placeholder, std::string text = {})
      : parts{.caption = nodes::Text(std::move(label), 13.0f, dim_colour), .box = widgets::TextArea<>(std::move(placeholder))} {
    auto& box = parts.box;
    this->setGap(4.0f);
    fState.apply({.fillX = true, .autoSize = scene::axes::kY});
    box.setSingleLine(true);
    box.apply({.fillX = true, .padding = {0.0f, 10.0f, 0.0f, 10.0f}, .cornerRadius = 6.0f, .background = tile_colour,
               .border = scene::Border{band_colour, 1.0f}});
    box.setText(std::move(text));
  }
  // Its border in the accent while it has the focus.
  bool lit = false;
  void update(double) {
    auto& box = parts.box;
    if (box.focused() == lit)
      return;
    lit = box.focused();
    box.apply({.border = scene::Border{lit ? accent_colour : band_colour, 1.0f}});
  }
  // What is typed in it.
  [[nodiscard]] const std::string& text() const { return parts.box.text(); }
};

// A passphrase asked for: to open local data at the start (not dismissed --
// nothing behind it is anything until it opens), to turn its encryption on
// or off, or to change it. Its fields are the forms' own, masked; what each
// purpose shows and says, by its type.
template <class Actions>
struct passphrase_box : nodes::Stack {
  struct words {
    std::string_view title, note, button;
    bool current, fresh;  // the passphrase now asked; a new one, twice
    bool file = false;    // a file's path asked too
  };
  static constexpr words words_of(config::passphrase_for::unlock) {
    return {"Local data is encrypted", "Type its passphrase to open your settings, chats and keys.", "Unlock", true, false};
  }
  static constexpr words words_of(config::passphrase_for::encrypt) {
    return {"Encrypt local data",
            "Settings, passwords and tokens, chats kept, drafts and encryption keys are sealed under a passphrase, "
            "asked for at every start. Forgotten, it cannot be recovered, and neither can they.",
            "Encrypt", false, true};
  }
  static constexpr words words_of(config::passphrase_for::change) {
    return {"Change the passphrase", "Everything kept is sealed again under the new one.", "Change", true, true};
  }
  static constexpr words words_of(config::passphrase_for::export_keys) {
    return {"Export room keys",
            "This account's room keys are written to your Downloads folder, sealed under a new passphrase: with "
            "them and it, any client reads every encrypted message they open. Keep both safe.",
            "Export", false, true};
  }
  static constexpr words words_of(config::passphrase_for::import_keys) {
    return {"Import room keys", "Room keys from a key file Element or mux wrote, under its passphrase.", "Import", true, false,
            true};
  }
  static constexpr words words_of(config::passphrase_for::decrypt) {
    return {"Stop encrypting local data", "Everything kept is written in the clear again, readable by whoever can read "
                                          "these files.",
            "Decrypt", true, false};
  }
  struct submit {
    passphrase_box* box;
    void operator()() const {
      box->actions->give_passphrase(box->purpose, box->parts.current.text(), box->parts.fresh.text(), box->parts.again.text(),
                                    box->parts.file.text());
    }
  };
  Actions* actions = nullptr;
  config::passphrase_for_t purpose;
  struct parts_t {
    nodes::Text title;
    nodes::Text note;
    field file;
    field current;
    field fresh;
    field again;
    nodes::Text error;
    widgets::Button<submit> go;
  } parts;

  passphrase_box(Actions* a, config::passphrase_for_t why)
      : actions(a), purpose(why),
        parts{.title = nodes::Text(std::string(said().title), 17.0f, text_colour, true),
              .note = nodes::Text(std::string(said().note), 14.0f, dim_colour),
              .file = field("Key file", "/home/you/element-keys.txt"),
              .current = field(said().fresh ? "Passphrase now" : "Passphrase", "Passphrase"),
              .fresh = field("New passphrase", "New passphrase"),
              .again = field("The new one again", "New passphrase"),
              .error = nodes::Text("", 13.0f, error_colour),
              .go = widgets::Button<submit>(std::string(said().button), {this})} {
    fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {20.0f, 22.0f, 20.0f, 22.0f}});
    this->setGap(10.0f);
    for (nodes::Text* each : {&parts.title, &parts.note, &parts.error}) {
      each->setWrapped(true);
      each->apply({.fillX = true});
    }
    for (field* each : {&parts.current, &parts.fresh, &parts.again})
      each->parts.box.setMasked(true);
    parts.current.setVisible(said().current);
    parts.file.setVisible(said().file);
    parts.fresh.setVisible(said().fresh);
    parts.again.setVisible(said().fresh);
    parts.error.setVisible(false);
    parts.go.setPrimary(true);
    parts.go.apply({.width = 110.0f, .height = 34.0f, .alignSelf = scene::align::kEnd});
  }
  [[nodiscard]] words said() const {
    return splice::visit([](auto why) { return words_of(why); }, purpose);
  }
  // Why it was not taken: said under the fields, which are emptied.
  void say(std::string what) {
    parts.error.setText(std::move(what));
    parts.error.setVisible(true);
    for (field* each : {&parts.current, &parts.fresh, &parts.again})
      each->parts.box.setText("");
  }
};

// ---- the account forms ---------------------------------------------------------

// Buttons side by side, as a form ends.
template <class... Buttons>
struct button_row : nodes::Stack {
  struct parts_t {
    std::tuple<Buttons...> buttons;
  } parts;
  explicit button_row(Buttons... all) : parts{.buttons = std::tuple<Buttons...>(std::move(all)...)} {
    this->setHorizontal();
    this->setGap(10.0f);
    fState.apply({.autoSize = scene::axes::kBoth});
  }
};

// What every account form ends with: what went wrong or what is happening,
// and its buttons. Enter in any of the form's fields submits it.
template <class Actions>
struct form_end : nodes::Stack {
  using submit_button = widgets::Button<ask<Actions, &Actions::submit_login>>;
  using close_button = widgets::Button<ask<Actions, &Actions::pop_panel>>;
  struct parts_t {
    nodes::Text message{"", 13.0f, error_colour};
    button_row<submit_button, close_button> buttons;
  } parts;

  form_end(Actions* a, bool editing)
      : parts{.buttons = button_row<submit_button, close_button>(submit_button(editing ? "Save" : "Log in", {a}),
                                                                 close_button("Close", {a}))} {
    auto& message = parts.message;
    fState.apply({.fillX = true, .autoSize = scene::axes::kY});
    this->setGap(12.0f);
    auto& [submit, close] = parts.buttons.parts.buttons;
    submit.setPrimary(true);
    submit.apply({.width = 120.0f, .height = 36.0f});
    close.apply({.width = 120.0f, .height = 36.0f});
    close.setVisible(editing);
    message.setWrapped(true);
    message.apply({.fillX = true});
  }

  void say(std::string text, bool error) {
    parts.message.setText(std::move(text));
    parts.message.setColour(error ? error_colour : dim_colour);
  }
};

// Text typed into an optional: nothing when the field is empty.
[[nodiscard]] inline std::optional<std::string> typed_or_nothing(const std::string& text) {
  if (text.empty())
    return std::nullopt;
  return text;
}

// What "Advanced" folds out on an XMPP form: the resource, where to connect,
// and PLAIN without TLS. Its height is what its last layout took.
template <class Actions>
struct xmpp_advanced : nodes::Stack {
  using plain_toggle = widgets::Toggle<ask<Actions, &Actions::toggle_plain>>;
  // The switch and what it says, side by side.
  struct plain_row : nodes::Stack {
    struct parts_t {
      plain_toggle plain;
      nodes::Text label{"Allow PLAIN without TLS. Only for a test server on this machine: never over a network.",
                        13.0f, error_colour};
    } parts;
    explicit plain_row(Actions* a) : parts{.plain = plain_toggle({a})} {
      this->setHorizontal();
      this->setGap(10.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY});
      parts.label.setWrapped(true);
      parts.label.apply({.grow = scene::axes::kX});
    }
  };
  struct parts_t {
    field resource{"Device name (resource)", "mux", "mux"};
    field host{"Host", "from the domain's SRV records"};
    field port{"Port", "5222"};
    plain_row row;
  } parts;

  explicit xmpp_advanced(Actions* a) : parts{.row = plain_row(a)} {
    fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 0.0f, 12.0f, 0.0f}});
    this->setGap(8.0f);
  }
  [[nodiscard]] plain_toggle& plain() { return parts.row.parts.plain; }
  [[nodiscard]] const plain_toggle& plain() const { return parts.row.parts.plain; }
};

// An XMPP account's settings: its JID and password, and "Advanced" folds out
// the rest. It fills the column it is given.
template <class Actions>
struct xmpp_form : nodes::Stack {
  Actions* actions = nullptr;
  // The address the account was saved under, when this is an edit of one.
  std::optional<std::string> editing;
  bool advanced = false;

  using advanced_button_t = widgets::Button<ask<Actions, &Actions::toggle_advanced>>;
  struct parts_t {
    field address{"Address (JID)", "user@example.com"};
    field password{"Password", "Password"};
    advanced_button_t advanced_button;
    widgets::Collapsible<xmpp_advanced<Actions>> more;
    form_end<Actions> end;
  } parts;

  xmpp_form(Actions* a, const std::optional<config::xmpp_account>& from)
      : actions(a),
        parts{.advanced_button = advanced_button_t("Advanced", {a}),
              .more = widgets::Collapsible<xmpp_advanced<Actions>>(a),
              .end = form_end<Actions>(a, from.has_value())} {
    auto& [address, password, advanced_button, more, end] = parts;
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

  // The account as typed, or what is wrong with it. What is folded away is
  // kept as it is: folding is not clearing.
  [[nodiscard]] std::expected<config::xmpp_account, std::string> account() const {
    const auto& folded = parts.more.child().parts;
    config::xmpp_account out{.address = parts.address.text(),
                             .password = parts.password.text(),
                             .resource = folded.resource.text(),
                             .host = typed_or_nothing(folded.host.text()),
                             .plain_without_tls = parts.more.child().plain().on()};
    if (const std::string& text = folded.port.text(); !text.empty()) {
      std::int64_t number = 0;
      const auto [last, failed] = std::from_chars(text.data(), text.data() + text.size(), number);
      if (failed != std::errc{} || last != text.data() + text.size())
        return std::unexpected("A port is a number from 1 to 65535");
      out.port = number;
    }
    if (auto wrong = config::check(out))
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

// A Matrix account's settings: its user ID and password, the homeserver
// (found through the server's .well-known when left empty) and what this
// device is called.
template <class Actions>
struct matrix_form : nodes::Stack {
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
    if (auto wrong = config::check(out))
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

// Either form, as the panels hold them.
template <class Actions>
using account_form = splice::variant<xmpp_form<Actions>, matrix_form<Actions>>;

// The form of an account's own protocol, filled in from it.
template <class Actions>
[[nodiscard]] account_form<Actions> form_of(Actions* a, const config::account_t& saved) {
  return splice::visit(splice::overloaded{[a](const config::xmpp_account& one) {
                                 return account_form<Actions>(std::in_place_index<0>, a, one);
                               },
                               [a](const config::matrix_account& one) {
                                 return account_form<Actions>(std::in_place_index<1>, a, one);
                               }},
                    saved);
}

// The XMPP form among them, when that is the one up.
template <class Actions>
[[nodiscard]] xmpp_form<Actions>* xmpp_form_in(account_form<Actions>& form) {
  return splice::visit(splice::overloaded{[](xmpp_form<Actions>& one) { return &one; },
                               [](matrix_form<Actions>&) -> xmpp_form<Actions>* { return nullptr; }},
                    form);
}

// A form laid out in the column under `top`.
template <class Actions>
void place_form(account_form<Actions>& form, const skia::SkRect& column, float top) {
  splice::visit(
      [&](auto& one) {
        one.fState.arrange(0.0f, 0.0f);
        scene::layout(one, skia::SkRect::MakeLTRB(column.fLeft, column.fTop + top, column.fRight, column.fBottom));
      },
      form);
}

// Esc closes a panel: back to what is under it.
template <class Actions>
struct closes_on_escape : nodes::Stack {
  Actions* actions = nullptr;
  explicit closes_on_escape(Actions* a) : actions(a) {}

  using Node::onKey;
  void onKey(scene::phase::bubble, const scene::key::down& press, scene::Reply& reply) {
    if (press.key == scene::keys::kEscape) {
      actions->pop_panel();
      reply.handle();
    }
  }
};

}  // namespace mux::ui
