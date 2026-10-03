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
  static constexpr words words_of(config::passphrase_for::cross_signing) {
    return {"Set up cross-signing",
            "Three keys are made for your account and kept on this device: with them it signs your devices and "
            "the people you verify. Your server asks for your account's password to take them.",
            "Set up", true, false};
  }
  static constexpr words words_of(config::passphrase_for::reset_identity) {
    return {"Reset your identity",
            "New cross-signing keys replace your account's: everyone who verified you, and every session of yours, "
            "must verify again, and messages only your old keys could read stay unreadable here. Only if you have "
            "lost every verified session and the recovery key. Your server asks for your account's password.",
            "Reset", true, false};
  }
  static constexpr words words_of(config::passphrase_for::sign_out_unverified) {
    return {"Sign out unverified sessions",
            "Every session of yours that is not verified -- not cross-signed, nor verified by emoji here -- is "
            "signed out. Your server asks for your account's password.",
            "Sign out", true, false};
  }
  static constexpr words words_of(config::passphrase_for::recovery) {
    return {"Restore with the recovery key",
            "The recovery key written down when cross-signing was set up: with it, this device takes your "
            "cross-signing keys back from your server.",
            "Restore", true, false};
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

// Each protocol's account form -- its form_type(tag), found by ADL where the
// panels are made: a template on Actions, made in the program, which imports
// mux.ui.proto -- and any of them, as the panels hold them, made from the
// list of protocols.
template <class Tag, class Actions>
using form_of_t = typename decltype(form_type(Tag{}, std::type_identity<Actions>{}))::type;
template <class Actions, class>
struct form_list;
template <class Actions, class... Tags>
struct form_list<Actions, protocol_list<Tags...>> {
  using type = splice::variant<form_of_t<Tags, Actions>...>;
};
template <class Actions>
using account_form = typename form_list<Actions, protocols>::type;

// The form of an account's own protocol, filled in from what it keeps.
template <class Actions>
[[nodiscard]] account_form<Actions> form_of(Actions* a, const config::account_t& saved) {
  return splice::visit([a](const auto& kept) {
    using form = typename decltype(form_type_for(kept, std::type_identity<Actions>{}))::type;
    return account_form<Actions>(std::in_place_type<form>, a, std::optional(kept));
  }, saved.own);
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
