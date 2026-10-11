// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:info_common -- What a chat's and a person's info are made of: tiles, member rows, id lines, a person's facts.
export module mux.ui:info_common;

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
import :base;
import :icons;
import :avatars;
import :controls;
import :themes;
import :names;
import :forms;
import :composer;
import :message;
import :html;
import :timeline;  // a message's menu, for the reactions list's bubbles

export namespace mux::ui {

// One of the square buttons of a chat's info: its icon over its name.
template <class Act>
auto action_tile(const palette& colours, std::string text, icon_t icon, Act what = {}) {
  return skiff::compose::onPress(std::move(what), skiff::compose::column(
      skiff::compose::justified(skiff::compose::vbox(0.0f, {.height = 58.0f,
          .padding = {6.0f, 0.0f, 8.0f, 0.0f}, .cornerRadius = 8.0f, .background = colours.tile,
          .hoverBackground = colours.chosen, .focusBackground = colours.chosen}), nodes::justify::space_between{}),
      skiff::compose::styled({.width = 28.0f, .height = 24.0f, .alignSelf = scene::align::kMiddle},
          nodes::Icon(shape_of(icon), colours.text)),
      skiff::compose::styled({.alignSelf = scene::align::kMiddle}, nodes::Text(text, 12.0f, colours.text))), text);
}
template <class Act>
using action_tile_t = decltype(action_tile(std::declval<const palette&>(), "", icon::none{}, std::declval<Act>()));

inline auto member_row(const palette& colours, const member& one, std::string how) {
  const std::string name = one.name.empty() ? one.id : one.name;
  return skiff::compose::recorded(skiff::compose::onClick(request::open_member_info{one.id}, skiff::compose::row(
      skiff::compose::hbox(12.0f, {.fillX = true, .height = 54.0f, .padding = {0.0f, 16.0f, 0.0f, 16.0f},
          .hoverBackground = colours.chosen}),
      avatar_mark(one.id, name, 40.0f), two_lines(colours, name, std::move(how), 14.0f, 4.0f),
      skiff::compose::visible(one.role.has_value(), skiff::compose::styled(
          {.alignSelf = scene::align::kStart, .margin = {10.0f, 0.0f, 0.0f, 0.0f}},
          widgets::Pill(one.role.value_or(""), {.plate = skia::colorSetARGB(255, 62, 52, 96),
              .text = skia::colorSetARGB(255, 190, 170, 250), .size = 12.0f, .height = 20.0f, .padX = 8.0f})))), name));
}
using member_row_t = decltype(member_row(std::declval<const palette&>(), std::declval<const member&>(), ""));

// A round avatar on its own: the chat's, big, over its name.
struct big_avatar : avatar_mark {
  big_avatar() : avatar_mark(std::string(), std::string(), 96.0f) {}
};
// An icon on its own, not to be pressed.
inline auto icon_view(const palette& colours, icon_t mark) {
  return skiff::compose::styled({.width = 28.0f, .height = 36.0f}, nodes::Icon(shape_of(mark), colours.dim));
}
using icon_view_t = decltype(icon_view(std::declval<const palette&>(), icon::none{}));
// A band between sections: just darker than the panel.
inline auto section_band(const palette& colours) {
  return skiff::compose::styled({.fillX = true, .height = 6.0f, .margin = {6.0f, 0.0f, 6.0f, 0.0f}},
                                nodes::Box<>(colours.section));
}

// A chat's info, beside it, as Telegram Desktop shows it: a big avatar, the
// name and who is in it, three square buttons, its ID, and its members.
// Declared: a column of these, nothing placed by hand.
// An ID, whole -- wrapped, never cut -- and copied when pressed: a chat's
// or a person's.
struct id_line_t : skiff::compose::Stacked {
  using Answer = request::copy_text;
  std::string copied_, confirmation_, label_;
  struct parts_t {
    nodes::Text value;
    nodes::Text feedback;
  } parts;
  id_line_t(const palette& colours, std::string text, std::string link, std::string label = "ID")
      : Stacked(skiff::compose::vbox(2.0f, {.fillX = true, .autoSize = scene::axes::kY,
              .padding = {8.0f, 20.0f, 8.0f, 20.0f}, .hoverBackground = colours.chosen,
              .focusBackground = colours.chosen})),
        copied_(link.empty() ? text : link), confirmation_(link.empty() ? label + " · copied" : label + " · link copied, with its servers"),
        label_("Copy " + label),
        parts{.value = wrapped(skiff::compose::styled({.fillX = true}, nodes::Text(std::move(text), 14.0f, colours.accent, false, true))),
              .feedback = wrapped(skiff::compose::styled({.fillX = true}, nodes::Text(std::move(label), 13.0f, colours.accent)))} {
    fState.setCursor(scene::cursor::hand{});
  }
  [[nodiscard]] bool acceptsInput() const { return true; }
  [[nodiscard]] bool hoverChangesAppearance() const { return true; }
  [[nodiscard]] bool focusChangesAppearance() const { return true; }
  [[nodiscard]] bool onClick(float, float) {
    scene::pressLater(fState);
    return true;
  }
  request::copy_text onPress() {
    // Update the clicked row before delivering the clipboard request. A
    // cached info panel need not be refreshed to show its confirmation.
    parts.feedback.setText(confirmation_);
    return {copied_};
  }
  [[nodiscard]] scene::Semantics semantics() const {
    scene::Semantics out;
    out.fRole = scene::semantic_role::button{};
    out.fLabel = label_;
    out.fActions = {scene::semantic_action::focus{}, scene::semantic_action::activate{}};
    return out;
  }
};
inline auto id_line(const palette& colours, std::string text, std::string link, std::string label = "ID") {
  return id_line_t(colours, std::move(text), std::move(link), std::move(label));
}

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

}  // namespace mux::ui
