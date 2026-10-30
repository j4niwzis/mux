// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:forms -- Form rows, and the account forms.
module;
#include "../inline_in_release.h"
#include "skiff/named.h"
export module mux.ui.base:forms;

import std;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.compose;
import skiff.model;
import skiff.widgets.model;
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

template <auto Member, class Caption>
  requires std::derived_from<Caption, scene::Node>
auto model_field(const palette& colours, Caption caption, std::string placeholder, bool masked = false,
                 scene::Margin margin = {}) {
  return SKIFF_NAMED(model_field, skiff::compose::column(
      skiff::compose::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY, .margin = margin}),
      std::move(caption),
      skiff::compose::bound<skiff::model::Field<Member>>(skiff::compose::styled(
          {.fillX = true, .height = 36.0f}, widgets::TextField<std::string>(colours.widgets, std::move(placeholder), masked)))));
}
template <auto Member>
auto model_field(const palette& colours, std::string label, std::string placeholder, bool masked = false,
                 scene::Margin margin = {}) {
  return model_field<Member>(colours, nodes::Text(std::move(label), 13.0f, colours.dim), std::move(placeholder), masked, margin);
}

// Several lines of a part, as a topic's or a biography's: Enter starts a
// new one, up to `lines` shown before it scrolls; its caption over it.
template <auto Member>
auto model_area(const palette& colours, std::string label, std::string placeholder, int lines, scene::Margin margin = {}) {
  return SKIFF_NAMED(model_area, skiff::compose::column(
      skiff::compose::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY, .margin = margin}),
      nodes::Text(std::move(label), 13.0f, colours.dim),
      skiff::compose::bound<skiff::model::Field<Member>>(skiff::compose::styled(
          {.fillX = true, .padding = {6.0f, 10.0f, 6.0f, 10.0f}, .cornerRadius = 6.0f, .background = colours.tile,
           .border = scene::Border{colours.band, 1.0f}},
          widgets::TextAreaField<typename skiff::bind::BoundValueT<skiff::model::Field<Member>>::type>(
              colours.widgets, std::move(placeholder), lines)))));
}
// A checkbox of a bool part, its words beside it -- and read out with them:
// pressed, the part set to the other.
template <auto Member>
auto check_field(const palette& colours, std::string label, std::string hint = {}) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(check_field, c::row(c::hbox(10.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                c::named(label, std::move(hint),
                         c::bound<skiff::model::Field<Member>>(c::styled({.alignSelf = scene::align::kMiddle},
                             widgets::ToggleField<bool, widgets::internal::CheckboxLook>(colours.widgets)))),
                c::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle},
                          wrapped(nodes::Text(label, 14.0f, colours.text)))));
}

// A link put on what is selected in the message field, as tdesktop's
// EditLinkBox: its text and its URL, the forms' own fields; Apply puts it on,
// Esc or Cancel leaves the field as it was.
// A link being put on what is selected: the text, and the link it has.
struct link_facts {
  std::string text;
  std::string url;
};
struct link_draft {
  std::string text, url;
  link_facts original;
};
struct submit_link {};
struct link_events {
  auto on(submit_link, const link_draft& draft) const {
    return skiff::model::Up{request::set_link{draft.text, draft.url}};
  }
};
MUX_INLINE_IN_RELEASE auto link_box(const palette& colours, const link_facts& facts) {
  return SKIFF_NAMED(link_box, skiff::compose::local<link_draft>(link_events{}, skiff::compose::column(
      skiff::compose::vbox(10.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {20.0f, 22.0f, 20.0f, 22.0f}}),
      skiff::compose::styled({.fillX = true}, nodes::Text(facts.url.empty() ? "Add link" : "Edit link", 17.0f, colours.text, true)),
      model_field<&link_draft::text>(colours, "Text", "Text"),
      model_field<&link_draft::url>(colours, "URL", "https://"),
      skiff::compose::row(skiff::compose::hbox(8.0f, {.fillX = true, .height = 36.0f}),
          skiff::compose::styled({.height = 36.0f, .relativeSize = scene::axes::kNone, .grow = scene::axes::kX},
              widgets::SendButton<request::close_link>(colours.widgets, "Cancel", {})),
          skiff::compose::styled({.height = 36.0f, .relativeSize = scene::axes::kNone, .grow = scene::axes::kX},
              primary(widgets::SendButton<submit_link>(colours.widgets, "Apply", {}))))),
      link_draft{facts.text, facts.url, facts}));
}
using link_box_t = decltype(link_box(std::declval<const palette&>(), std::declval<const link_facts&>()));
// Keep the local draft and focused fields while the same link is shown.
inline bool content_persistent(std::type_identity<link_facts>) { return true; }
inline bool content_matches(const link_facts& was, const link_facts& now) { return was.text == now.text && was.url == now.url; }
inline dialog_look content_look(std::type_identity<link_facts>) { return {.size = dialog_size::fitting{400.0f}}; }
template <class Needs>
auto make_content(const Needs& needs, const link_facts& facts) {
  return link_box(*needs.colours, facts);
}

// A passphrase asked for: to open local data at the start (not dismissed --
// nothing behind it is anything until it opens), to turn its encryption on
// or off, or to change it. Its fields are the forms' own, masked; what each
// purpose shows and says, by its type.
// A passphrase asked for: why, and why the last one given was not taken.
struct passphrase_facts {
  passphrase_purpose_t why;
  std::optional<std::string> refused;
  std::string current, fresh, again, file;
};
struct passphrase_words {
  std::string_view title, note, button;
  bool current, fresh;  // the passphrase now asked; a new one, twice
  bool file = false;    // a file's path asked too
};
inline constexpr passphrase_words words_of(config::passphrase_for::unlock) {
  return {"Local data is encrypted", "Type its passphrase to open your settings, chats and keys.", "Unlock", true, false};
}
inline constexpr passphrase_words words_of(config::passphrase_for::encrypt) {
  return {"Encrypt local data",
          "Settings, passwords and tokens, chats kept, drafts and encryption keys are sealed under a passphrase, "
          "asked for at every start. Forgotten, it cannot be recovered, and neither can they.",
          "Encrypt", false, true};
}
inline constexpr passphrase_words words_of(config::passphrase_for::change) {
  return {"Change the passphrase", "Everything kept is sealed again under the new one.", "Change", true, true};
}
// A protocol's own, or a plugin's: as it words it (passphrase_text, by ADL).
template <class Purpose>
inline constexpr passphrase_words words_of(Purpose why) {
  const proto::passphrase_words said = passphrase_text(why);
  return {said.title, said.note, said.button, said.current, said.fresh, said.file};
}
inline constexpr passphrase_words words_of(config::passphrase_for::decrypt) {
  return {"Stop encrypting local data", "Everything kept is written in the clear again, readable by whoever can read "
                                        "these files.",
          "Decrypt", true, false};
}
inline passphrase_words words_of(const passphrase_purpose_t& purpose) {
  return spl::visit([](auto why) { return words_of(why); }, purpose);
}
MUX_INLINE_IN_RELEASE auto passphrase_box(const palette& colours) {
  using purpose = skiff::model::Field<&passphrase_facts::why>;
  using refused = skiff::model::Field<&passphrase_facts::refused>;
  return SKIFF_NAMED(passphrase_box, skiff::compose::scoped<passphrase_facts>(skiff::compose::handlers(), skiff::compose::column(
      skiff::compose::vbox(10.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {20.0f, 22.0f, 20.0f, 22.0f}}),
      skiff::compose::text_for<purpose>([](const auto& why) { return std::string(words_of(why).title); },
          skiff::compose::styled({.fillX = true}, wrapped(nodes::Text("", 17.0f, colours.text, true)))),
      skiff::compose::text_for<purpose>([](const auto& why) { return std::string(words_of(why).note); },
          skiff::compose::styled({.fillX = true}, wrapped(nodes::Text("", 14.0f, colours.dim)))),
      skiff::compose::shown_if<purpose>([](const auto& why) { return words_of(why).file; },
          model_field<&passphrase_facts::file>(colours, "Key file", "/home/you/element-keys.txt")),
      skiff::compose::shown_if<purpose>([](const auto& why) { return words_of(why).current; },
          model_field<&passphrase_facts::current>(colours,
              skiff::compose::text_for<purpose>([](const auto& why) {
                return std::string(words_of(why).fresh ? "Passphrase now" : "Passphrase");
              }, nodes::Text("", 13.0f, colours.dim)), "Passphrase", true)),
      skiff::compose::shown_if<purpose>([](const auto& why) { return words_of(why).fresh; },
          model_field<&passphrase_facts::fresh>(colours, "New passphrase", "New passphrase", true)),
      skiff::compose::shown_if<purpose>([](const auto& why) { return words_of(why).fresh; },
          model_field<&passphrase_facts::again>(colours, "The new one again", "New passphrase", true)),
      skiff::compose::shown_if<refused>([](const auto& error) { return error.has_value(); },
          skiff::compose::column(skiff::compose::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
              skiff::compose::text_for<refused>([](const auto& error) { return error.value_or(""); },
                  skiff::compose::styled({.fillX = true}, wrapped(nodes::Text("", 13.0f, colours.error)))))),
      skiff::compose::projected<passphrase_facts>([](const passphrase_facts& facts) {
            return std::pair{std::string(words_of(facts.why).button),
                request::give_passphrase{facts.why, facts.current, facts.fresh, facts.again, facts.file}};
          },
          skiff::compose::styled({.width = 110.0f, .height = 34.0f, .alignSelf = scene::align::kEnd},
              primary(widgets::SendButton<request::give_passphrase>(colours.widgets, "", {})))))));
}
using passphrase_box_t = decltype(passphrase_box(std::declval<const palette&>()));
inline dialog_look content_look(std::type_identity<passphrase_facts>) { return {.size = dialog_size::fitting{420.0f}}; }
inline bool content_persistent(std::type_identity<passphrase_facts>) { return true; }
inline bool content_dismissable(std::type_identity<passphrase_facts>, const passphrase_facts& facts) {
  return spl::visit(spl::overloaded{[](config::passphrase_for::unlock) { return false; }, [](const auto&) { return true; }}, facts.why);
}
template <class Needs>
auto make_content(const Needs& needs, const passphrase_facts&) {
  return passphrase_box(*needs.colours);
}

// ---- the account forms ---------------------------------------------------------

// Tabs for a few values of a part, in the window's theme.
template <class T>
[[nodiscard]] widgets::ChoiceTabs<T> themed_tabs(const palette& colours, std::vector<std::pair<std::string, T>> choices) {
  widgets::ChoiceTabs<T> tabs(std::move(choices));
  tabs.setTheme(colours.widgets);
  return tabs;
}
// A part of a model chosen among a few values, its words over the tabs:
// pressed, the part set to the one picked.
template <auto Member, class T>
auto choice_field(const palette& colours, std::string label, std::vector<std::pair<std::string, T>> choices) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(choice_field, c::column(c::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                   nodes::Text(std::move(label), 13.0f, colours.dim),
                   c::bound<skiff::model::Field<Member>>(c::styled({.fillX = true}, themed_tabs<T>(colours, std::move(choices))))));
}
// An account form's end: what it says, as the program says it, and Log in
// (Save, editing an account) sending its draft -- and, editing, Close.
template <class Draft>
auto account_form_end(const palette& colours, bool editing) {
  namespace c = skiff::compose;
  using facts = std::optional<account_form_facts>;
  using close_button = button_for<sends<request::pop_panel>>;
  return SKIFF_NAMED(account_form_end, c::column(c::vbox(12.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      c::applied<facts>([](const facts& now) { return now ? now->said : form_said{}; },
                        [at = &colours](nodes::Text& message, const form_said& now) {
                          message.setText(now.text);
                          message.setColour(now.error ? at->error : at->dim);
                        },
                        c::styled({.fillX = true}, wrapped(nodes::Text("", 13.0f, colours.error)))),
      c::row(c::hbox(10.0f, {.autoSize = scene::axes::kBoth}),
             c::asking_for<Draft>([](const Draft& now) { return request::submit_account{account_draft_t{now}}; },
                                  c::styled({.width = 120.0f, .height = 36.0f},
                                            primary(widgets::Button<scene::NoAction>(colours.widgets, editing ? "Save" : "Log in", {})))),
             c::visible(editing, c::styled({.width = 120.0f, .height = 36.0f}, close_button(colours.widgets, "Close", {}))))));
}
// Enter in any of a form's fields: its draft sent, as Log in sends it.
template <class Draft>
struct enter_submits {
  [[nodiscard]] std::optional<request::submit_account> operator()(const Draft& now, const scene::key::down& press) const {
    return press.key == scene::keys::kEnter ? std::optional(request::submit_account{account_draft_t{now}}) : std::nullopt;
  }
};
template <class Draft, class N>
auto submitted_by_enter(N node) {
  return SKIFF_NAMED(submitted_by_enter, skiff::compose::keys_for<Draft, scene::phase::bubble>([](const Draft& now) { return now; }, enter_submits<Draft>{}, std::move(node)));
}

// Text typed into an optional: nothing when the field is empty.
[[nodiscard]] inline std::optional<std::string> typed_or_nothing(const std::string& text) {
  if (text.empty())
    return std::nullopt;
  return text;
}


// Esc leaves a panel: back to what is under it, or a step back within it
// first, as Back answers.
template <class Back = sends<request::pop_panel>, class N>
auto closes_on_escape(N node) {
  return SKIFF_NAMED(closes_on_escape, skiff::compose::onKey(scene::keys::kEscape, Back{}(), std::move(node)));
}



}  // namespace mux::ui
