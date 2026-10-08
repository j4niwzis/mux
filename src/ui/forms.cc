// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:forms -- Form rows, and the account forms.
export module mux.ui:forms;

import std;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.compose;
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

struct field : skiff::compose::Stacked {
  struct parts_t {
    nodes::Text caption;
    widgets::TextArea<> box;
  } parts;

  // The colours its border turns as it has the focus.
  const palette* colours_ = nullptr;
  // Declared: the caption over the field, which sits on a plate.
  field(const palette& colours, std::string label, std::string placeholder, std::string text = {})
      : Stacked(skiff::compose::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY})),
        parts{.caption = nodes::Text(std::move(label), 13.0f, colours.dim),
              .box = skiff::compose::styled({.fillX = true, .padding = {0.0f, 10.0f, 0.0f, 10.0f}, .cornerRadius = 6.0f, .background = colours.tile,
                                             .border = scene::Border{colours.band, 1.0f}},
                                            one_line(widgets::TextArea<>(colours.widgets, std::move(placeholder)), std::move(text)))},
        colours_(&colours) {}
  // One line, holding what it starts with.
  [[nodiscard]] static widgets::TextArea<> one_line(widgets::TextArea<> box, std::string text) {
    box.setSingleLine(true);
    box.setText(std::move(text));
    return box;
  }
  // Its typing hidden, as a passphrase's.
  [[nodiscard]] field masked() && {
    parts.box.setMasked(true);
    return std::move(*this);
  }
  // Its border in the accent while it has the focus.
  bool lit = false;
  void update(double) {
    auto& box = parts.box;
    if (box.focused() == lit)
      return;
    lit = box.focused();
    box.apply({.border = scene::Border{lit ? colours_->accent : colours_->band, 1.0f}});
  }
  // What is typed in it.
  [[nodiscard]] const std::string& text() const { return parts.box.text(); }
  // Several lines, as a topic's: Enter starts a new one, up to `lines`
  // shown before it scrolls.
  void multi_line(int lines) {
    parts.box.setSingleLine(false);
    parts.box.setMaxLines(lines);
    parts.box.apply({.padding = {6.0f, 10.0f, 6.0f, 10.0f}});
  }
};

// A link put on what is selected in the message field, as tdesktop's
// EditLinkBox: its text and its URL, the forms' own fields; Done puts it on,
// Esc or Cancel leaves the field as it was.
// A link being put on what is selected: the text, and the link it has.
struct link_facts {
  std::string text;
  std::string url;
};
template <class Actions>
struct link_box : skiff::compose::Stacked {
  // The dialog it is shown in.
  [[nodiscard]] static dialog_look look_of_dialog() { return {.size = dialog_size::fitting{400.0f}}; }
  struct done {
    using Answer = ::mux::ui::request::set_link;
    link_box* box;
    ::mux::ui::request::set_link operator()() const { return ::mux::ui::request::set_link{box->parts.text.text(), box->parts.url.text()}; }
  };
  struct cancel {
    using Answer = ::mux::ui::request::close_link;
    ::mux::ui::request::close_link operator()() { return ::mux::ui::request::close_link{}; }
  };
  struct parts_t {
    nodes::Text title;
    field text;
    field url;
    widgets::Button<done> go;
    widgets::Button<cancel> back;
  } parts;
  link_box(const ui_needs<Actions>& n, const link_facts& facts) : link_box(n, facts.text, facts.url) {}
  link_box(const ui_needs<Actions>& n, std::string text, std::string url)
      : Stacked(skiff::compose::vbox(10.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {20.0f, 22.0f, 20.0f, 22.0f}})),
        parts{.title = skiff::compose::styled({.fillX = true}, nodes::Text(url.empty() ? "Add link" : "Edit link", 17.0f, n.colours->text, true)),
              .text = field(*n.colours, "Text", "Text", std::move(text)),
              .url = field(*n.colours, "URL", "https://", std::move(url)),
              .go = widgets::Button<done>(n.colours->widgets, "Done", {this}),
              .back = widgets::Button<cancel>(n.colours->widgets, "Cancel", {})} {}
};

// A passphrase asked for: to open local data at the start (not dismissed --
// nothing behind it is anything until it opens), to turn its encryption on
// or off, or to change it. Its fields are the forms' own, masked; what each
// purpose shows and says, by its type.
template <class Actions>
struct passphrase_box : skiff::compose::Stacked {
  // The dialog it is shown in.
  [[nodiscard]] static dialog_look look_of_dialog() { return {.size = dialog_size::fitting{420.0f}}; }
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
  // A protocol's own: as it words it (passphrase_text, by ADL).
  template <class Purpose>
  static constexpr words words_of(Purpose why) {
    const proto::passphrase_words said = passphrase_text(why);
    return {said.title, said.note, said.button, said.current, said.fresh, said.file};
  }
  static constexpr words words_of(config::passphrase_for::decrypt) {
    return {"Stop encrypting local data", "Everything kept is written in the clear again, readable by whoever can read "
                                          "these files.",
            "Decrypt", true, false};
  }
  struct submit {
    using Answer = ::mux::ui::request::give_passphrase;
    passphrase_box* box;
    Answer operator()() const {
      return ::mux::ui::request::give_passphrase{box->purpose, box->parts.current.text(), box->parts.fresh.text(), box->parts.again.text(),
                                    box->parts.file.text()};
    }
  };
  proto::passphrase_for_t purpose;
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

  passphrase_box(const ui_needs<Actions>& n, proto::passphrase_for_t why) : passphrase_box(*n.colours, why) {}
  // Not dismissed where it is asked at the start: the local data is locked
  // until it is given.
  [[nodiscard]] static bool dismissable(const proto::passphrase_for_t& why) {
    return spl::visit(spl::overloaded{[](config::passphrase_for::unlock) { return false; }, [](const auto&) { return true; }}, why);
  }
  passphrase_box(const palette& colours, proto::passphrase_for_t why)
      : Stacked(skiff::compose::vbox(10.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {20.0f, 22.0f, 20.0f, 22.0f}})),
        purpose(why),
        parts{.title = skiff::compose::styled({.fillX = true}, wrapped(nodes::Text(std::string(said().title), 17.0f, colours.text, true))),
              .note = skiff::compose::styled({.fillX = true}, wrapped(nodes::Text(std::string(said().note), 14.0f, colours.dim))),
              .file = skiff::compose::visible(said().file, field(colours, "Key file", "/home/you/element-keys.txt")),
              .current = skiff::compose::visible(said().current, field(colours, said().fresh ? "Passphrase now" : "Passphrase", "Passphrase").masked()),
              .fresh = skiff::compose::visible(said().fresh, field(colours, "New passphrase", "New passphrase").masked()),
              .again = skiff::compose::visible(said().fresh, field(colours, "The new one again", "New passphrase").masked()),
              .error = skiff::compose::visible(false, skiff::compose::styled({.fillX = true}, wrapped(nodes::Text("", 13.0f, colours.error)))),
              .go = skiff::compose::styled({.width = 110.0f, .height = 34.0f, .alignSelf = scene::align::kEnd},
                                           primary(widgets::Button<submit>(colours.widgets, std::string(said().button), {this})))} {}
  [[nodiscard]] words said() const {
    return spl::visit([](auto why) { return words_of(why); }, purpose);
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
struct button_row : skiff::compose::Stacked {
  struct parts_t {
    std::tuple<Buttons...> buttons;
  } parts;
  explicit button_row(Buttons... all)
      : Stacked(skiff::compose::hbox(10.0f, {.autoSize = scene::axes::kBoth})), parts{.buttons = std::tuple<Buttons...>(std::move(all)...)} {}
};

// What every account form ends with: what went wrong or what is happening,
// and its buttons. Enter in any of the form's fields submits it.
template <class Actions>
struct form_end : skiff::compose::Stacked {
  using submit_button = button_for<sends<::mux::ui::request::submit_login>>;
  using close_button = button_for<sends<::mux::ui::request::pop_panel>>;
  // The colours it is made in, for what it says later.
  const palette* colours_ = nullptr;
  struct parts_t {
    nodes::Text message;
    button_row<submit_button, close_button> buttons;
  } parts;

  form_end(const palette& colours, bool editing)
      : Stacked(skiff::compose::vbox(12.0f, {.fillX = true, .autoSize = scene::axes::kY})),
        colours_(&colours),
        parts{.message = skiff::compose::styled({.fillX = true}, wrapped(nodes::Text("", 13.0f, colours.error))),
              .buttons = button_row<submit_button, close_button>(
                  skiff::compose::styled({.width = 120.0f, .height = 36.0f}, primary(submit_button(colours.widgets, editing ? "Save" : "Log in", {}))),
                  skiff::compose::visible(editing, skiff::compose::styled({.width = 120.0f, .height = 36.0f},
                                                                          close_button(colours.widgets, "Close", {}))))} {}

  void say(std::string text, bool error) {
    parts.message.setText(std::move(text));
    parts.message.setColour(error ? colours_->error : colours_->dim);
  }
};

// Text typed into an optional: nothing when the field is empty.
[[nodiscard]] inline std::optional<std::string> typed_or_nothing(const std::string& text) {
  if (text.empty())
    return std::nullopt;
  return text;
}

// Each protocol's account form -- its form_type(state), found by ADL where the
// panels are made: a template on Actions, made in the program, which imports
// mux.ui.proto -- and any of them, as the panels hold them, made from the
// list of protocols.
template <class Tag, class Actions>
using form_of_t = typename decltype(form_type(state_of<Tag>{}, type_tag<Actions>{}))::type;
template <class Actions, class>
struct form_list;
template <class Actions, class... Tags>
struct form_list<Actions, protocol_list<Tags...>> {
  using type = spl::variant<form_of_t<Tags, Actions>...>;
};
template <class Actions>
using account_form = typename form_list<Actions, protocols>::type;

// The form of an account's own protocol, filled in from what it keeps.
template <class Actions>
[[nodiscard]] account_form<Actions> form_of(const palette& colours, const config::account_t& saved) {
  return spl::visit([&](const auto& kept) {
    using form = typename decltype(form_type_for(kept, type_tag<Actions>{}))::type;
    return account_form<Actions>(std::in_place_type<form>, colours, std::optional(kept));
  }, saved.own);
}
// A form laid out in the column under `top`.
template <class Actions>
void place_form(account_form<Actions>& form, const skia::SkRect& column, float top) {
  spl::visit(
      [&](auto& one) {
        one.fState.arrange(0.0f, 0.0f);
        scene::layout(one, skia::SkRect::MakeLTRB(column.fLeft, column.fTop + top, column.fRight, column.fBottom));
      },
      form);
}

// Esc leaves a panel: back to what is under it, or a step back within it
// first, as Back says.
template <class Actions, class Back = sends<::mux::ui::request::pop_panel>>
struct closes_on_escape : nodes::Stack {
  // Esc: what Back answers.
  using Answer = typename Back::Answer;
  explicit closes_on_escape() {}

  using Node::onKey;
  std::optional<Answer> onKey(scene::phase::bubble, const scene::key::down& press, scene::Reply& reply) {
    if (press.key != scene::keys::kEscape)
      return std::nullopt;
    reply.handle();
    return Back{}();
  }
};

}  // namespace mux::ui
