// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui.cards -- What a chat's and a person's info are made of: tiles, member rows, id lines, a person's facts, a card's fit -- a module of its own, under the window, so a plugin's card (the room card's) is made of the same.
module;
#include "skiff/named.h"
export module mux.ui.cards;

import std;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.compose;
import skiff.model;
import skiff.nodes.box;
import skiff.nodes.flow;
import skiff.nodes.icon;
import skiff.nodes.image;
import skiff.nodes.scroll;
import skiff.nodes.text;
import skiff.widgets.pill;
import skiff.widgets.button;
import skiff.widgets.sliderbar;
import skiff.widgets.textbox;
import skiff.widgets.textarea;
import mux.core;
import mux.config;
import mux.logic.links;
import mux.protocols;
import mux.ui.base;
import mux.ui.messages;

export namespace mux::ui {

// One of the square buttons of a chat's info: its icon over its name.
template <class Act>
auto action_tile(const palette& colours, std::string text, icon_t icon, Act what = {}) {
  return SKIFF_NAMED(action_tile, skiff::compose::onPress(std::move(what), skiff::compose::column(
      skiff::compose::justified(skiff::compose::vbox(0.0f, {.height = 58.0f,
          .padding = {6.0f, 0.0f, 8.0f, 0.0f}, .cornerRadius = 8.0f, .background = colours.tile,
          .hoverBackground = colours.chosen, .focusBackground = colours.chosen}), nodes::justify::space_between{}),
      skiff::compose::styled({.width = 28.0f, .height = 24.0f, .alignSelf = scene::align::kMiddle},
          nodes::Icon(shape_of(icon), colours.text)),
      skiff::compose::styled({.alignSelf = scene::align::kMiddle}, nodes::Text(text, 12.0f, colours.text))), text));
}
template <class Act>
using action_tile_t = decltype(action_tile(std::declval<const palette&>(), "", icon::none{}, std::declval<Act>()));


// An icon on its own, not to be pressed.
inline auto icon_view(const palette& colours, icon_t mark) {
  return SKIFF_NAMED(icon_view, skiff::compose::styled({.width = 28.0f, .height = 36.0f}, nodes::Icon(shape_of(mark), colours.dim)));
}
using icon_view_t = decltype(icon_view(std::declval<const palette&>(), icon::none{}));
// A band between sections: just darker than the panel.
inline auto section_band(const palette& colours) {
  return SKIFF_NAMED(section_band, skiff::compose::styled({.fillX = true, .height = 6.0f, .margin = {6.0f, 0.0f, 6.0f, 0.0f}},
                                nodes::Box<>(colours.section)));
}

// A chat's info, beside it, as Telegram Desktop shows it: a big avatar, the
// name and who is in it, three square buttons, its ID, and its members.
// Declared: a column of these, nothing placed by hand.
// An ID, whole -- wrapped, never cut -- and copied when pressed: a chat's
// or a person's.
struct id_feedback {
  bool copied = false;
};
struct copy_id_line {};
struct id_copy_events {
  std::string copied;
  auto on(copy_id_line, const id_feedback&) const {
    return std::tuple{skiff::model::over<id_feedback>(skiff::model::setTo(id_feedback{true})),
                      skiff::model::Up{request::copy_text{copied}}};
  }
};
inline auto id_line(const palette& colours, std::string text, std::string link, std::string label = "ID") {
  namespace c = skiff::compose;
  const bool a_link = !link.empty();
  const std::string copied = a_link ? link : text;
  return SKIFF_NAMED(id_line, c::local<id_feedback>(id_copy_events{copied},
      c::onClick(copy_id_line{}, c::column(
          c::vbox(2.0f, {.fillX = true, .autoSize = scene::axes::kY,
              .padding = {8.0f, 20.0f, 8.0f, 20.0f}, .hoverBackground = colours.chosen,
              .focusBackground = colours.chosen}),
          wrapped(c::styled({.fillX = true}, nodes::Text(std::move(text), 14.0f, colours.accent, false, true))),
          c::text_for<id_feedback>([label, a_link](const id_feedback& now) {
              return !now.copied ? label : a_link ? label + " · link copied, with its servers" : label + " · copied";
            }, wrapped(c::styled({.fillX = true}, nodes::Text(label, 13.0f, colours.accent))))), "Copy " + label)));
}
using id_line_t = decltype(id_line(std::declval<const palette&>(), "", ""));

// A person's name and how they are, as a chat knows them: a member's, with
// their role; the other side of a direct chat; anyone else by their address.
struct person_facts {
  std::string name;
  std::string status;
  // What the user may do to them in the chat being read, as its power
  // levels allow: only above them, and only where the room lets them.
  bool may_kick = false;
  bool may_ban = false;
  // What is known of their encryption identity, where the account said.
  std::optional<trust_t> trust;
  // Their sessions, each verified or not, where their keys were listed.
  std::vector<change::device_view> devices;
  profile_details profile;
  friend bool operator==(const person_facts&, const person_facts&) = default;
};
[[nodiscard]] inline person_facts person_of(const ui_shared& shared, const conversation* in, const model& now, const account_id& account,
                                            const std::string& id) {
  person_facts out{id, presence_of(shared, now, account, id)};
  if (const auto* kept = now.accounts().find(account))
    if (const auto found = kept->profiles.find(id); found != kept->profiles.end()) {
      if (found->second.name && !found->second.name->empty()) out.name = *found->second.name;
      out.profile = found->second.details;
    }
  out.trust = now.trust_of(account, id);
  if (const auto* listed = now.devices_of(account, id))
    out.devices = *listed;
  // What their protocol says of them beside how they are (Matrix: their identity).
  out.status = std::ranges::fold_left(proto::person_badges(protocol_state_of(shared, account), nullptr, now, account, id),
                                      std::move(out.status), [](std::string so_far, const proto::part::badge& badge) {
                                        return so_far.empty() ? badge.text : std::format("{} · {}", so_far, badge.text);
                                      });
  if (in == nullptr)
    return out;
  if (const auto found = std::ranges::find(in->members, id, &member::id); found != in->members.end()) {
    // What may be done to them, as the chat's protocol says (Matrix: its power levels).
    const proto::part::person_rights may = proto::person_rights(protocol_state_of(shared, account), *in, id);
    out.may_kick = may.kick;
    out.may_ban = may.ban;
    if (!found->name.empty())
      out.name = found->name;
    if (found->role)
      out.status = out.status.empty() ? *found->role : std::format("{} · {}", out.status, *found->role);
  } else if (!is_group(*in) && id == contact_of(shared, *in)) {
    out.name = display_name(*in);
  }
  return out;
}

// Measure a fitting card at its available width without losing the scroll
// anchor's previous position. Both user and room cards use this viewport.
inline float card_part_height(auto& node, const skia::SkRect& parent) {
  const auto previous = skiff::compose::state_of(node).fLastConstraint;
  scene::layout(node, skia::SkRect::MakeXYWH(previous.fLeft, previous.fTop, parent.width(), 0.0f));
  return node.bounds().height() + skiff::compose::state_of(node).fMargin.totalY();
}
// Keep a short card compact; for a long one, size the scroll viewport to the
// room the dialog gives the card rather than the content's unbounded height.
// What a card holds beside its head and its details: the person's card
// nothing; the room's its join button, and its decline where it is shown.
template <class Top, class Scroll>
float card_rest_height(std::tuple<Top, Scroll>&, const skia::SkRect&) {
  return 0.0f;
}
template <class Top, class Scroll, class Join, class Decline>
float card_rest_height(std::tuple<Top, Scroll, Join, Decline>& parts, const skia::SkRect& parent) {
  auto& decline = std::get<3>(parts);
  return card_part_height(std::get<2>(parts), parent) + (decline.visible() ? card_part_height(decline, parent) : 0.0f);
}
// A card: its head, its details scrolling, what it offers under them -- in
// their places, which card_fit names.
template <class... Parts> skiff::compose::Box<Parts...> card_box(skiff::compose::Look look, Parts... parts) {
  look.fHorizontal = false;
  return skiff::compose::Box<Parts...>(std::move(look), std::move(parts)...);
}
// One whose parts hold pieces (a room card's details, their faces): a
// builder of it, built as its parts are.
template <class... Parts>
  requires(skiff::compose::a_builder<Parts> || ...)
auto card_box(skiff::compose::Look look, Parts... parts) {
  return skiff::compose::lifted([](skiff::compose::Look l, auto... p) { return card_box(std::move(l), std::move(p)...); }, std::move(look), std::move(parts)...);
}
struct card_fit {
  struct State {};
  template <class N> static void prepare(N& card, State&) {
    const skia::SkRect& parent = card.fState.fLastConstraint;
    auto& scroll = std::get<1>(card.fParts);
    const float details = card_part_height(std::get<0>(scroll.fChildren), parent);
    const float others = card.fState.fPadding.totalY() + card_part_height(std::get<0>(card.fParts), parent) +
                         card_rest_height(card.fParts, parent);
    const float room = std::max(0.0f, parent.height() - others);
    scroll.fState.fHeight = parent.height() > 0.0f ? std::min(details, room) : details;
  }
  template <class N> static void arrange(N&, State&) {}
  template <class N> static bool settling(const N&, const State&) { return false; }
  template <class N> static void settle(N&, State&) {}
};

}  // namespace mux::ui
