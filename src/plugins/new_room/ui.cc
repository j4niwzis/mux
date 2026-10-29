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
// What a protocol's form has, while compiling.
template <class Protocol>
inline constexpr proto::creation_traits traits_of = proto::creation_traits_of<state_of<Protocol>>();
// The space it is made in, where it is one's form.
template <class Protocol>
std::optional<place> place_of(const room_form_of<Protocol>&) { return std::nullopt; }
template <class Protocol>
std::optional<place> place_of(const space_room_form_of<Protocol>& facts) { return facts.in; }
template <class Facts>
struct room_creation_events {
  Facts facts;
  auto on(submit_room, const room_draft& draft) const {
    constexpr auto traits = traits_of<typename Facts::protocol>;
    std::optional<request::create_room> asked;
    const auto [open, members] = spl::visit(spl::overloaded{
        [](room_access::invited) { return std::pair{false, false}; },
        [](room_access::everyone) { return std::pair{true, false}; },
        [](room_access::space) { return std::pair{false, true}; }}, draft.access);
    const auto in = place_of(facts);
    if (!draft.name.empty() && (!traits.address_required || !draft.address.empty()))
      asked = request::create_room{draft.name, draft.topic, open, draft.address, draft.federate,
          traits.encryption && draft.encrypted && !open,
          in ? std::optional(in->space) : std::nullopt, members,
          in && in->make_space};
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
// What differs by the form's type, each made only where it is had: the
// space's members as a choice, in a space's form; the address, always where
// the protocol needs one, else as Anyone is chosen; encryption and
// federation, where the protocol has them.
template <class Protocol>
auto space_choice(const palette&, const room_form_of<Protocol>&) { return nothing_shown(); }
template <class Protocol>
auto space_choice(const palette& colours, const space_room_form_of<Protocol>&) {
  return room_access_choice(colours, "Space members", room_access::space{});
}
template <class Protocol>
auto address_part(const palette& colours, const std::string& hint) {
  using access = skiff::model::Field<&room_draft::access>;
  return skiff::compose::shown_for<access>([](const auto& access) { return access == room_access_t(room_access::everyone{}); },
                                           model_field<&room_draft::address>(colours, "Address", hint));
}
template <class Protocol>
  requires(traits_of<Protocol>.address_required)
auto address_part(const palette& colours, const std::string& hint) {
  return model_field<&room_draft::address>(colours, "Address", hint);
}
template <class Protocol>
auto encryption_part(const palette&) { return nothing_shown(); }
template <class Protocol>
  requires(traits_of<Protocol>.encryption)
auto encryption_part(const palette& colours) {
  using access = skiff::model::Field<&room_draft::access>;
  return skiff::compose::shown_for<access>([](const auto& access) { return access != room_access_t(room_access::everyone{}); },
                                           room_form_toggle<&room_draft::encrypted>(colours, "Enable end-to-end encryption"));
}
template <class Protocol>
auto federation_part(const palette&) { return nothing_shown(); }
template <class Protocol>
  requires(traits_of<Protocol>.federation)
auto federation_part(const palette& colours) {
  namespace c = skiff::compose;
  return c::column(c::vbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                   room_form_toggle<&room_draft::advanced>(colours, "Advanced"),
                   c::shown_for<skiff::model::Field<&room_draft::advanced>>([](bool shown) { return shown; },
                       room_form_toggle<&room_draft::federate>(colours, "Allow people from other servers")));
}
template <class Facts>
auto create_room_view(const palette& colours, const Facts& facts) {
  namespace c = skiff::compose;
  using Protocol = typename Facts::protocol;
  const auto in = place_of(facts);
  const auto form = proto::room_creation_form_of(state_before(facts.by.speaks), facts.own_server);
  return SKIFF_NAMED(create_room_view, c::local<room_draft>(room_creation_events<Facts>{facts}, c::column(
      c::vbox(8.0f, {.fill = true, .padding = {0.0f, 22.0f, 18.0f, 22.0f}}),
      page_header<no_back, sends<request::close_shown<Facts>>>(colours,
          in ? std::string(in->make_space ? "Create a space in " : "Create a room in ") + in->name : "Create a room", {}, {}, false, true),
      c::styled({.fillX = true, .grow = scene::axes::kY}, nodes::ScrollContainer(c::column(
          c::vbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}),
          model_field<&room_draft::name>(colours, "Name", ""),
          model_field<&room_draft::topic>(colours, "Topic (optional)", ""),
          nodes::Text("Who can join", 13.0f, colours.dim),
          room_access_choice(colours, "Invite only", room_access::invited{}),
          room_access_choice(colours, "Anyone", room_access::everyone{}),
          space_choice(colours, facts),
          address_part<Protocol>(colours, form.address_hint),
          encryption_part<Protocol>(colours),
          federation_part<Protocol>(colours)))),
      c::row(c::justified(c::hbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}), nodes::justify::end{}),
          c::styled({.width = 90.0f, .height = 34.0f}, widgets::SendButton<request::close_shown<Facts>>(colours.widgets, "Cancel", {})),
          c::styled({.width = 128.0f, .height = 34.0f},
              skiff::compose::primary(widgets::SendButton<submit_room>(colours.widgets, in && in->make_space ? "Create space" : "Create room", {}))))),
      room_draft{.access = in ? room_access_t(room_access::space{}) : room_access_t(room_access::invited{}),
                 .encrypted = traits_of<Protocol>.encryption}));
}
template <class Protocol>
dialog_look content_look(std::type_identity<room_form_of<Protocol>>) { return {.size = dialog_size::fixed{480.0f, 560.0f}}; }
template <class Protocol>
dialog_look content_look(std::type_identity<space_room_form_of<Protocol>>) { return {.size = dialog_size::fixed{480.0f, 560.0f}}; }
template <class Protocol>
auto make_content(const ui_needs& needs, const room_form_of<Protocol>& facts) { return create_room_view(*needs.colours, facts); }
template <class Protocol>
auto make_content(const ui_needs& needs, const space_room_form_of<Protocol>& facts) { return create_room_view(*needs.colours, facts); }

// Its dialogs, among the window's (mux.ui:window): one for each protocol's
// form, from the drawer and in a space.
template <class>
struct forms_of;
template <class... Protocols>
struct forms_of<protocol_list<Protocols...>> {
  using type = composition::page_list<room_form_of<Protocols>..., space_room_form_of<Protocols>...>;
  using requests = proto::request_list<request::close_shown<room_form_of<Protocols>>..., request::close_shown<space_room_form_of<Protocols>>...>;
  template <class Showing>
  static void close(Showing& showing) {
    // show() is the window's (mux.ui), found by ADL on the model.
    (show(showing, std::optional<room_form_of<Protocols>>{}), ...);
    (show(showing, std::optional<space_room_form_of<Protocols>>{}), ...);
  }
};
using forms = forms_of<protocols>;
constexpr typename forms::type facts_types(contract::window_dialogs, type_tag<feature>) { return {}; }
template <class Facts>
constexpr auto extend(contract::window_dialogs, type_tag<feature>, type_tag<Facts>) {
  return type_tag<decltype(create_room_view(std::declval<const palette&>(), std::declval<const Facts&>()))>{};
}

}  // namespace mux::plugins::new_room
