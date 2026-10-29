// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.new_room.ui -- Element's Create a room (its CreateRoomDialog): a
// name, a topic, who can join -- by invitation, or anyone, with the address
// it is found by -- and, among the advanced, whether those of other servers
// may ever join. And its row in the drawer.
module;
#include "skiff/named.h"
export module mux.plugin.new_room.ui;

import std;
import splice;
import skiff.paint;
import skiff.scene;
import skiff.bind;
import skiff.compose;
import skiff.model;
import skiff.widgets.model;
import skiff.nodes.box;
import skiff.nodes.flow;
import skiff.nodes.icon;
import skiff.nodes.scroll;
import skiff.nodes.text;
import skiff.widgets.button;
import skiff.widgets.sliderbar;
import skiff.widgets.textbox;
import skiff.widgets.textarea;
import mux.composition;
import mux.core;
import mux.protocols;
import mux.ui.base;
export import mux.plugin.new_room.types;

export namespace mux::plugins::new_room {
using namespace ::mux::ui;

// Its row in the drawer (mux.ui:drawer).
inline auto extend(contract::drawer_rows, type_tag<feature>, const palette& colours) {
  return row_item(colours, "New room", sends<request::open_new_room>{}, icon::people{});
}

namespace room_access {
struct invited { friend bool operator==(invited, invited) = default; };
struct everyone { friend bool operator==(everyone, everyone) = default; };
struct space { friend bool operator==(space, space) = default; };
}
using room_access_t = spl::variant<room_access::invited, room_access::everyone, room_access::space>;
struct room_draft {
  std::string name, topic, address;
  room_access_t access = room_access::invited{};
  bool encrypted = true, federate = true, advanced = false;
};
struct submit_room {};
struct room_creation_events {
  shown facts;
  proto::creation_form form;
  auto on(submit_room, const room_draft& draft) const {
    std::optional<request::create_room> asked;
    const auto [open, members] = spl::visit(spl::overloaded{
        [](room_access::invited) { return std::pair{false, false}; },
        [](room_access::everyone) { return std::pair{true, false}; },
        [](room_access::space) { return std::pair{false, true}; }}, draft.access);
    if (!draft.name.empty() && (!form.address_required || !draft.address.empty()))
      asked = request::create_room{draft.name, draft.topic, open, draft.address, draft.federate,
          form.encryption && draft.encrypted && !open,
          facts.in ? std::optional(facts.in->space) : std::nullopt, members,
          facts.in && facts.in->make_space};
    return skiff::model::Up{asked};
  }
};
template <auto Member>
auto room_form_toggle(const palette& colours, std::string label) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(room_form_toggle, c::row(c::hbox(12.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      c::styled({.grow = scene::axes::kX}, wrapped(nodes::Text(label, 13.0f, colours.text))),
      c::bound<skiff::model::Field<Member>>(widgets::ToggleField<bool>(colours.widgets))));
}
// Radio rows send changes to their bound field; their mark is projected from it.
inline auto room_access_choice(const palette& colours, std::string label, room_access_t choice) {
  namespace c = skiff::compose;
  using access = skiff::model::Field<&room_draft::access>;
  return SKIFF_NAMED(room_access_choice, c::spec_for<access>([choice](const auto& selected) { return scene::Spec{.selected = selected == choice}; },
      c::onClick(skiff::bind::own(skiff::model::setTo(choice)),
          c::row(c::hbox(16.0f, {.fillX = true, .height = 46.0f,
                                .padding = {0.0f, 20.0f, 0.0f, 20.0f},
                                .hoverBackground = colours.chosen}),
              c::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle},
                  nodes::Text(label, 15.0f, colours.text)),
              c::styled({.width = 20.0f, .alignSelf = scene::align::kMiddle},
                  c::text_for<access>([choice](const auto& selected) {
                    return selected == choice ? std::string("●") : std::string("○");
                  }, nodes::Text("", 20.0f, colours.accent)))), label)));
}
inline auto create_room_view(const palette& colours, const shown& facts) {
  namespace c = skiff::compose;
  using access = skiff::model::Field<&room_draft::access>;
  const auto form = proto::room_creation_form_of(facts.by ? state_before(facts.by->speaks) : state_before(protocol_t{}), facts.own_server);
  return SKIFF_NAMED(create_room_view, c::local<room_draft>(room_creation_events{facts, form}, c::column(
      c::vbox(8.0f, {.fill = true, .padding = {0.0f, 22.0f, 18.0f, 22.0f}}),
      page_header<no_back, sends<request::close_shown<shown>>>(colours,
          facts.in ? std::string(facts.in->make_space ? "Create a space in " : "Create a room in ") + facts.in->name : "Create a room", {}, {}, false, true),
      c::styled({.fillX = true, .grow = scene::axes::kY}, nodes::ScrollContainer(c::column(
          c::vbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}),
          model_field<&room_draft::name>(colours, "Name", ""),
          model_field<&room_draft::topic>(colours, "Topic (optional)", ""),
          nodes::Text("Who can join", 13.0f, colours.dim),
          room_access_choice(colours, "Invite only", room_access::invited{}),
          room_access_choice(colours, "Anyone", room_access::everyone{}),
          c::visible(facts.in.has_value(), room_access_choice(colours, "Space members", room_access::space{})),
          c::shown_for<access>([required = form.address_required](const auto& access) {
            return required || access == room_access_t(room_access::everyone{});
          }, model_field<&room_draft::address>(colours, "Address", form.address_hint)),
          c::visible(form.encryption, c::shown_for<access>([](const auto& access) {
            return access != room_access_t(room_access::everyone{});
          }, room_form_toggle<&room_draft::encrypted>(colours, "Enable end-to-end encryption"))),
          c::visible(form.federation, room_form_toggle<&room_draft::advanced>(colours, "Advanced")),
          c::visible(form.federation, c::shown_for<skiff::model::Field<&room_draft::advanced>>([](bool shown) { return shown; },
              room_form_toggle<&room_draft::federate>(colours, "Allow people from other servers")))))),
      c::row(c::justified(c::hbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}), nodes::justify::end{}),
          c::styled({.width = 90.0f, .height = 34.0f}, widgets::SendButton<request::close_shown<shown>>(colours.widgets, "Cancel", {})),
          c::styled({.width = 128.0f, .height = 34.0f},
              skiff::compose::primary(widgets::SendButton<submit_room>(colours.widgets, facts.in && facts.in->make_space ? "Create space" : "Create room", {}))))),
      room_draft{.access = facts.in ? room_access_t(room_access::space{}) : room_access_t(room_access::invited{}),
                 .encrypted = form.encryption}));
}
using box = decltype(create_room_view(std::declval<const palette&>(), std::declval<const shown&>()));
inline dialog_look content_look(std::type_identity<shown>) { return {.size = dialog_size::fixed{480.0f, 560.0f}}; }
inline auto make_content(const ui_needs& needs, const shown& facts) { return create_room_view(*needs.colours, facts); }

// Its dialog, among the window's (mux.ui:window).
constexpr type_tag<box> extend(contract::window_dialogs, type_tag<feature>) { return {}; }

}  // namespace mux::plugins::new_room
