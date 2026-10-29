// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui.proto.xmpp -- What XMPP shows of its own in the client's UI: its
// account form, found by ADL where the panels are made.
module;
#include "skiff/named.h"
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
  const std::string read = spl::visit(
      spl::overloaded{[&](field_shown::read) { return one.label.empty() || one.value.empty() ? joined(one.value, "\n") + one.label
                                                                                                 : one.label + ": " + joined(one.value, "\n"); },
                         [](const auto&) { return std::string(); }},
      one.shown);
  const std::vector<std::string> said{read, one.choices.empty() ? std::string() : "One of: " + joined(one.choices, ", "),
                                      joined(one.links, "\n")};
  return joined(said | std::views::filter([](const std::string& line) { return !line.empty(); }) | std::ranges::to<std::vector<std::string>>(),
                "\n");
}

// Text a reader may select and copy.
[[nodiscard]] inline nodes::Text selectable_text(nodes::Text text) {
  text.setSelectable(true);
  return text;
}
// What is typed into one of the fields the server asked for: told to the
// program, each edit, which keeps the answers.
struct answer_typed {
  static constexpr bool kEveryEdit = true;
  using Answer = request::registration_typed;
  std::string var;
  Answer operator()(std::string_view text) const { return {var, std::string(text)}; }
};
// Its box: what the server said it holds first, masked for a secret.
[[nodiscard]] inline widgets::internal::TextArea<answer_typed> answer_box(const palette& colours, const registration_field& one) {
  widgets::internal::TextArea<answer_typed> box(colours.widgets, one.desc, answer_typed{one.var});
  box.setSingleLine(true);
  box.setText(one.value.empty() ? std::string() : one.value.front());
  box.setMasked(spl::visit(spl::overloaded{[](field_shown::masked) { return true; }, [](const auto&) { return false; }}, one.shown));
  return box;
}
// Whether a field has a box: not one only read, nor a hidden one (sent back
// as it came, nothing of it shown).
[[nodiscard]] inline bool typed_into(const registration_field& one) {
  return spl::visit(spl::overloaded{[](field_shown::typed) { return true; }, [](field_shown::masked) { return true; },
                                    [](const auto&) { return false; }},
                    one.shown);
}
[[nodiscard]] inline bool shown_at_all(const registration_field& one) {
  return spl::visit(spl::overloaded{[](field_shown::hidden) { return false; }, [](const auto&) { return true; }}, one.shown);
}
// A field of what the server asks to register: its picture where it has
// one, what is said of it, and its box with its label.
inline auto asked_row(const palette& colours, const registration_field& one) {
  namespace c = skiff::compose;
  const std::string said = note_of(one);
  return c::visible(shown_at_all(one), c::column(
      c::vbox(6.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      c::visible(!one.picture.empty(), c::styled({.width = 240.0f, .height = 90.0f, .cornerRadius = 6.0f, .background = colours.tile},
                                                 nodes::Image<sent_picture>(picture_of(one), nodes::fit::contain{}))),
      c::visible(!said.empty(), c::styled({.fillX = true}, selectable_text(wrapped(nodes::Text(said, 13.0f, colours.dim))))),
      c::visible(typed_into(one), c::column(c::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY}),
          nodes::Text(one.label + (one.required ? " (required)" : ""), 13.0f, colours.dim),
          c::styled({.fillX = true, .height = 36.0f}, answer_box(colours, one))))));
}
using asked_row_t = decltype(asked_row(std::declval<const palette&>(), std::declval<const registration_field&>()));
// What the server asked, under the form: what it says, and its fields --
// made again where it asks again.
inline auto asked_rows(const palette& colours, const registration_asked& asked) {
  namespace c = skiff::compose;
  const std::string said = asked.instructions + (asked.page ? "\nOr make the account on the server's page: " + *asked.page : std::string());
  return SKIFF_NAMED(asked_rows, c::column(c::vbox(10.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      c::visible(!said.empty(), c::styled({.fillX = true}, selectable_text(wrapped(nodes::Text(said, 13.0f, colours.text))))),
      c::many(c::vbox(10.0f, {.fillX = true, .autoSize = scene::axes::kY}),
              asked.fields | std::views::transform([&](const registration_field& one) { return asked_row(colours, one); }) | std::ranges::to<std::vector>())));
}
using asked_rows_t = decltype(asked_rows(std::declval<const palette&>(), std::declval<const registration_asked&>()));
// The registration the program keeps beside the form, where it has one.
[[nodiscard]] inline std::optional<registration_asked> asked_now(const std::optional<account_form_facts>& now) {
  if (!now)
    return std::nullopt;
  return spl::visit(spl::overloaded{[](const registration_form& kept) { return std::optional(kept.asked); },
                                    [](const auto&) { return std::optional<registration_asked>(); }},
                    now->part);
}
inline auto asked_part(const palette& colours) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(asked_part, c::projected<std::optional<account_form_facts>>(asked_now,
      c::mount<asked_rows_t, registration_asked>([at = &colours](const registration_asked& asked) { return std::tuple{asked_rows(*at, asked)}; },
                                                 {.fillX = true, .autoSize = scene::axes::kY})));
}

// What "Advanced" folds out: the resource, where to connect, and PLAIN
// without TLS -- the last said in the error's colour, as it is one.
inline auto advanced_fields(const palette& colours) {
  namespace c = skiff::compose;
  using d = account_draft;
  return SKIFF_NAMED(advanced_fields, c::column(c::vbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 0.0f, 12.0f, 0.0f}}),
                   model_field<&d::resource>(colours, "Device name (resource)", "mux"),
                   model_field<&d::host>(colours, "Host", "from the domain's SRV records"),
                   model_field<&d::port>(colours, "Port", "5222"),
                   check_field<&d::plain_without_tls>(colours, "Allow PLAIN without TLS. Only for a test server on this machine: never over a network.")));
}

}  // namespace mux::proto::xmpp::form_detail

export namespace mux::proto::xmpp {

// What the add-account pane says of the protocol.
[[nodiscard]] constexpr std::string_view form_note(const account_draft&) {
  return "An address like user@example.com, on a server such as Prosody or ejabberd.";
}
// An XMPP account's settings, over the draft typed into them: its JID and
// password; a new account -- made on the server first (XEP-0077), with the
// answers to what the server asked, as the program keeps them; and
// "Advanced" folding out the rest, folded out where any of it is not as it
// comes. Folding is not clearing: what is folded away is kept.
inline auto account_form_of(const account_draft& seed, const ui::palette& colours, bool editing) {
  namespace c = skiff::compose;
  using namespace ::mux::ui;
  using d = account_draft;
  return SKIFF_NAMED(account_form_of, c::local<d>(
      submitted_by_enter<d>(c::column(
          c::vbox(12.0f, {.fillX = true, .autoSize = scene::axes::kY}),
          model_field<&d::address>(colours, "Address (JID)", "user@example.com"),
          model_field<&d::password>(colours, "Password", "Password", true),
          c::visible(!editing || seed.creating, choice_field<&d::creating, bool>(colours, "Account",
                                                                                 {{"Sign in to an account", false}, {"Create a new account", true}})),
          c::shown_for<d>([](const d& now) { return now.creating; }, form_detail::asked_part(colours)),
          check_field<&d::advanced>(colours, "Advanced"),
          c::projected<d>([](const d& now) { return now.advanced; }, widgets::Collapsible(form_detail::advanced_fields(colours))),
          account_form_end<d>(colours, editing))),
      seed));
}
// The answers the program keeps of what the server asked, in the draft
// sent: each asked field's, typed or as it came -- none for one only read.
inline void with_form_part(account_draft& draft, const registration_form& kept) {
  draft.answers = kept.answers;
}

}  // namespace mux::proto::xmpp

export namespace mux::proto::xmpp::settings {
struct profile_policy {
  static constexpr bool banners = false;
  static auto presences() { return std::vector<std::pair<std::string, availability_t>>{{"Online", availability::online{}}, {"Free to chat", availability::chat{}}, {"Away", availability::away{}}, {"Extended away", availability::extended_away{}}, {"Do not disturb", availability::do_not_disturb{}}, {"Offline", availability::offline{}}}; }
};
constexpr std::string_view page_title(profile) { return "Profile"; }
inline ui::icon_t page_icon(profile) { return ui::icon::info{}; }
inline auto page_view(profile, const ui::palette& colours, const ui::account_page_facts& first) {
  return ui::profile_page<profile_policy>(colours, first);
}
inline ui::request::refresh_profile asked_as_it_opens(profile, const account_id& by) { return {by}; }
}
