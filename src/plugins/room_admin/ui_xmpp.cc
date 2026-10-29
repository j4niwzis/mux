// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.room_admin.ui:xmpp -- An XMPP group chat's settings' pages: the
// room, its configuration form as the server sends it, its roles and
// permissions. Built where XMPP is.
module;
#include "skiff/named.h"
export module mux.plugin.room_admin.ui:xmpp;

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
import mux.plugin.room_settings.types;
import mux.plugin.room_settings.ui;
import mux.plugin.room_admin.types;

export namespace mux::proto::xmpp::manage_detail {
using namespace ::mux::ui;
using room_settings_facts = ::mux::plugins::room_settings::protocol_settings<::mux::protocol::xmpp>;
inline const muc_room& settings_of(const room_settings_facts& facts) {
  static const muc_room empty;
  return spl::visit(spl::overloaded{[](const muc_room& room) -> const muc_room& { return room; },
                                    [](const auto&) -> const muc_room& { return empty; }},
                    facts.theirs);
}
inline std::vector<std::string> lines_of(std::string_view text) {
  if (text.empty()) return {};
  return text | std::views::split('\n') | std::views::transform([](auto line) {
        auto value = line | std::ranges::to<std::string>();
        if (!value.empty() && value.back() == '\r') value.pop_back();
        return value;
      }) | std::ranges::to<std::vector<std::string>>();
}
// A field's label beside its editor: its words, required marked, its
// description read out as its hint.
auto config_label(const auto& colours, const muc_field& f) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(config_label, c::named(f.label, f.description,
                  c::styled({.width = 120.0f, .alignSelf = scene::align::kMiddle},
                            wrapped(nodes::Text(f.label + (f.required ? " *" : ""), 13.0f, colours.text)))));
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
    return lines->second | std::views::transform(&jid_line::address) | std::views::filter(std::not_fn(&std::string::empty)) | std::ranges::to<std::vector>();
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
  return f.values | std::views::join_with(std::string_view("\n")) | std::ranges::to<std::string>();
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
auto form_checkbox(const auto& colours, std::string var, std::optional<std::string> option, Act act) {
  namespace c = skiff::compose;
  using box_t = widgets::internal::Toggle<Act, widgets::internal::CheckboxLook>;
  return SKIFF_NAMED(form_checkbox, c::applied<muc_form>(
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
      c::styled({.alignSelf = scene::align::kMiddle}, box_t(colours.widgets, std::move(act)))));
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
auto jid_list(const auto& colours, const muc_field& f) {
  namespace c = skiff::compose;
  using row_t = std::invoke_result_t<jid_row_of, const std::tuple<std::uint32_t, bool, std::string>&>;
  using rows_t = nodes::MemoRows<std::uint32_t, bool, row_t, jid_row_of>;
  const std::string var = f.var;
  return SKIFF_NAMED(jid_list, c::column(c::vbox(6.0f, {.fillX = true, .autoSize = scene::axes::kY}),
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
                 widgets::Button<sends<jids_removed>>(colours.widgets, "Remove", {{var}})))));
}
// What a field is edited with, by its kind; its trouble under it.
template <class P> using field_editor_t = spl::variant<nodes::Text, decltype(form_checkbox(std::declval<P>(), "", std::nullopt, field_checked{})),
                                    choice_menu<field_chosen>,
                                    decltype(skiff::compose::many(skiff::compose::vbox(),
                                        std::vector<decltype(skiff::compose::row(skiff::compose::hbox(), form_checkbox(std::declval<P>(), "", std::optional<std::string>(), option_flipped{}), nodes::Text("", 13.0f, 0u)))>{})),
                                    decltype(jid_list(std::declval<P>(), std::declval<const muc_field&>())),
                                    widgets::internal::TextBox<field_typed>, widgets::internal::TextArea<field_typed>>;
inline auto field_editor(const auto& colours, const muc_field& f) -> field_editor_t<decltype(colours)> {
  namespace c = skiff::compose;
  const auto text = [&](bool masked) {
    auto box = widgets::internal::TextBox<field_typed>(colours.widgets, "", field_typed{f.var, false});
    box.setText(joined_values(f));
    box.setMasked(masked);
    box.apply({.fillX = true, .height = 36.0f});
    return field_editor_t<decltype(colours)>(std::in_place_index<5>, std::move(box));
  };
  const auto fixed = [&](std::string said) { return field_editor_t<decltype(colours)>(std::in_place_index<0>, wrapped(nodes::Text(std::move(said), 13.0f, colours.text))); };
  return spl::visit(
      spl::overloaded{
          [&](muc_kind::boolean) {
            return field_editor_t<decltype(colours)>(std::in_place_index<1>,
                                  form_checkbox(colours, f.var, std::nullopt,
                                                field_checked{f.var, !f.values.empty() && muc_boolean_of(f.values.front()).value_or(false)}));
          },
          [&](muc_kind::list_single) {
            const auto selected = f.values.empty() ? f.options.size()
                                                   : static_cast<std::size_t>(std::ranges::find(f.options, f.values.front(),
                                                                                                &std::pair<std::string, std::string>::second) -
                                                                              f.options.begin());
            return field_editor_t<decltype(colours)>(std::in_place_index<2>,
                                  make_choice_menu(colours, "", f.options | std::views::keys | std::ranges::to<std::vector<std::string>>(), selected,
                                                   field_chosen{f.var, f.options | std::views::values | std::ranges::to<std::vector<std::string>>()}));
          },
          [&](muc_kind::list_multi) {
            return field_editor_t<decltype(colours)>(std::in_place_index<3>,
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
            return field_editor_t<decltype(colours)>(std::in_place_index<6>, std::move(area));
          },
          [&](muc_kind::jid_multi) { return field_editor_t<decltype(colours)>(std::in_place_index<4>, jid_list(colours, f)); },
          [&](muc_kind::fixed) { return fixed(joined_values(f)); }, [&](muc_kind::hidden) { return fixed({}); },
          [&](muc_kind::unsupported) { return fixed("Unsupported configuration field."); }},
      f.kind);
}
// A field's row: its label beside its editor, what is wrong with it under
// that; not there where it is hidden, nor where it does not apply.
auto config_row(const auto& colours, const muc_room& room, const muc_field& f) {
  namespace c = skiff::compose;
  const bool labelled = spl::visit(spl::overloaded{[](muc_kind::fixed) { return false; }, [](const auto&) { return true; }}, f.kind);
  const bool hidden = spl::visit(spl::overloaded{[](muc_kind::hidden) { return true; }, [](const auto&) { return false; }}, f.kind);
  return SKIFF_NAMED(config_row, c::shown_for<muc_form>([room, f, hidden](const muc_form& form) { return !hidden && applies(form, room, f); },
      c::row(c::hbox(12.0f, {.fillX = true, .autoSize = scene::axes::kY, .maxWidth = 435.0f, .alignSelf = scene::align::kStart}),
             c::visible(labelled, config_label(colours, f)),
             c::column(c::vbox(4.0f, {.autoSize = scene::axes::kY, .grow = scene::axes::kX}),
                       field_editor(colours, f),
                       c::shown_for<muc_form>([room, f](const muc_form& form) { return trouble_of(form, room, f).has_value(); },
                           c::text_for<muc_form>([room, f](const muc_form& form) { return trouble_of(form, room, f).value_or(""); },
                                                 wrapped(nodes::Text("", 12.0f, colours.error))))))));
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
auto configuration_page(const auto& colours, const muc_room& room) {
  namespace c = skiff::compose;
  const auto changed_and_valid = [room](const muc_form& form) {
    return std::ranges::none_of(room.fields, [&](const muc_field& f) { return trouble_of(form, room, f).has_value(); }) &&
           std::ranges::any_of(room.fields, [&](const muc_field& f) { return modified(form, room, f); });
  };
  const auto saving = [](const facts_t& facts) { return room_of(facts).saving; };
  return SKIFF_NAMED(configuration_page, c::local<muc_form>(muc_form_events{},
      c::column(c::vbox(12.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {12.0f, 12.0f, 18.0f, 12.0f}}),
          c::visible(!room.title.empty(), wrapped(nodes::Text(room.title, 16.0f, colours.text, true))),
          wrapped(nodes::Text(room.configuring ? room.instructions | std::views::join_with(std::string_view("\n")) | std::ranges::to<std::string>()
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
                            return request::configure_muc{room.fields | std::views::transform([&](const muc_field& f) { return answer_of(form, room, f); }) | std::ranges::to<std::vector>()};
                          },
                              widgets::Button<scene::NoAction>(colours.widgets, "Save", {})))))),
          c::shown_for<facts_t>([](const facts_t& facts) { return room_of(facts).saving || room_of(facts).failure.has_value(); },
              c::text_for<facts_t>([](const facts_t& facts) {
                return room_of(facts).saving ? std::string("Saving\u2026") : room_of(facts).failure.value_or("");
              },
                  wrapped(nodes::Text("", 13.0f, colours.dim)))),
          c::many(c::vbox(12.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                  room.fields | std::views::transform([&](const muc_field& f) { return config_row(colours, room, f); }) | std::ranges::to<std::vector>())),
      form_of(room)));
}
template <class P> using configuration_page_t = decltype(configuration_page(std::declval<P>(), std::declval<const muc_room&>()));

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
auto room_page(const auto& colours, const room_basis& basis) {
  namespace c = skiff::compose;
  const muc_room& room = basis.room;
  const scene::Spec button{.height = 32.0f, .alignSelf = scene::align::kStart};
  return SKIFF_NAMED(room_page, c::local<room_form>(
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
      room_form{basis.topic, room.nick, {}}));
}
template <class P> using room_page_t = decltype(room_page(std::declval<P>(), std::declval<const room_basis&>()));

// Affiliation belongs to a bare account; role belongs to a joined occupant.
// Merge the two lists without inventing duplicate accounts, while keeping
// each nickname's role when one account joins from several clients.
inline std::vector<std::string> permission_rows(const muc_room& room) {
  struct entry { std::string jid, affiliation; std::map<std::string, std::string> occupants; };
  std::map<std::pair<bool, std::string>, entry> people;
  for (const auto& person : room.people) {
    const auto key = std::pair{person.jid.empty(), person.jid.empty() ? person.nick : person.jid};
    auto& kept = people[key];
    kept.jid = person.jid;
    kept.affiliation = person.affiliation;
    kept.occupants.insert_or_assign(person.nick, person.role);
  }
  for (const auto& person : room.affiliations) {
    auto& kept = people[{false, person.jid}];
    kept.jid = person.jid;
    kept.affiliation = person.affiliation;
  }
  return people | std::views::transform([](const auto& item) {
    const auto& person = item.second;
    auto text = person.jid.empty() ? item.first.second : person.jid;
    text += " · Affiliation: " + person.affiliation;
    const auto occupants = person.occupants | std::views::transform([](const auto& occupant) {
      return occupant.first + " (Role: " + occupant.second + ")";
    });
    if (!person.occupants.empty())
      text += " · " + (occupants | std::views::join_with(std::string_view(", ")) | std::ranges::to<std::string>());
    return text;
  }) | std::ranges::to<std::vector<std::string>>();
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
auto roles_page(const auto& colours, const roles_basis& basis) {
  namespace c = skiff::compose;
  const muc_room& room = basis.room;
  const bool owner = room.affiliation == "owner";
  return SKIFF_NAMED(roles_page, c::local<roles_form>(
      c::column(c::vbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 28.0f, 24.0f, 12.0f}}),
          tab_heading(colours, "Roles & Permissions"),
          wrapped(nodes::Text("Affiliations use a real bare JID: none, member, admin, owner, outcast (ban). Roles use an occupant nickname: visitor, "
                              "participant, moderator, none (kick). The server checks your privileges.",
                              13.0f, colours.dim)),
          c::many(c::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                  permission_rows(room) | std::views::transform([&](std::string text) { return wrapped(nodes::Text(std::move(text), 13.0f, colours.text)); }) | std::ranges::to<std::vector>()),
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
                            widgets::Button<scene::NoAction>(colours.widgets, "Destroy room", {}))))))));
}
template <class P> using roles_page_t = decltype(roles_page(std::declval<P>(), std::declval<const roles_basis&>()));
// A page made from what the room is, again where that changes.
template <class Page, class Basis, class Of, class Make>
auto made_from(const auto& colours, Of of, Make make) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(made_from, c::projected<facts_t>([of](const facts_t& facts) { return facts ? std::optional<Basis>(of(*facts)) : std::nullopt; },
      c::mount<Page, Basis>([at = &colours, make](const Basis& basis) { return std::tuple{make(*at, basis)}; },
                            {.fillX = true, .autoSize = scene::axes::kY})));
}
}
export namespace mux::proto::xmpp::manage {
inline ui::icon_t tab_icon(room) { return ui::icon::people{}; }
inline ui::icon_t tab_icon(configuration) { return ui::icon::gear{}; }
inline ui::icon_t tab_icon(roles) { return ui::icon::people{}; }
// Each tab's page, made again from the room as the server says it.
auto page_of(room, const auto& colours, const ::mux::plugins::room_settings::protocol_settings<::mux::protocol::xmpp>&) {
  using namespace manage_detail;
  return made_from<room_page_t<decltype(colours)>, room_basis>(colours, [](const ::mux::plugins::room_settings::protocol_settings<::mux::protocol::xmpp>& facts) { return room_basis{settled(settings_of(facts)), facts.topic}; },
                                            [](const auto& at, const room_basis& basis) { return room_page(at, basis); });
}
auto page_of(configuration, const auto& colours, const ::mux::plugins::room_settings::protocol_settings<::mux::protocol::xmpp>&) {
  using namespace manage_detail;
  return made_from<configuration_page_t<decltype(colours)>, muc_room>(colours, [](const ::mux::plugins::room_settings::protocol_settings<::mux::protocol::xmpp>& facts) { return settled(settings_of(facts)); },
                                                   [](const auto& at, const muc_room& room) { return configuration_page(at, room); });
}
auto page_of(roles, const auto& colours, const ::mux::plugins::room_settings::protocol_settings<::mux::protocol::xmpp>&) {
  using namespace manage_detail;
  return made_from<roles_page_t<decltype(colours)>, roles_basis>(colours, [](const ::mux::plugins::room_settings::protocol_settings<::mux::protocol::xmpp>& facts) { return roles_basis{settled(settings_of(facts)), facts.id}; },
                                              [](const auto& at, const roles_basis& basis) { return roles_page(at, basis); });
}
template <class Tab>
auto make_manage_page(Tab tab, const ui::palette& colours, const ::mux::plugins::room_settings::protocol_settings<::mux::protocol::xmpp>& facts)
  requires requires { page_of(tab, colours, facts); }
{
  return page_of(tab, colours, facts);
}
}


export namespace mux::plugins::room_admin {
// The dialog for an XMPP group chat: the client's own look.
inline std::optional<room_settings::room_manage_style> admin_presented(xmpp_admin, const ::mux::plugins::room_settings::protocol_settings<::mux::protocol::xmpp>&, const ui::palette&) {
  return std::nullopt;
}
}  // namespace mux::plugins::room_admin
