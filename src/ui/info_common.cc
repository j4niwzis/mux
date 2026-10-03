// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:info_common -- What a chat's and a person's info are made of: tiles, member rows, id lines, a person's facts.
export module mux.ui:info_common;

import std;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
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
struct action_tile : pressable<nodes::Stack> {
  Act act;
  struct parts_t {
    icon_mark mark;
    nodes::Text label;
  } parts;

  // Declared: the icon at the top, the name at the bottom.
  action_tile(const palette& colours, std::string text, icon_t icon, Act what = {})
      : act(std::move(what)), parts{.mark = icon_mark(colours, icon), .label = nodes::Text(std::move(text), 12.0f, colours.text)} {
    auto& [mark, label] = parts;
    fState.apply({.height = 58.0f, .padding = {6.0f, 0.0f, 8.0f, 0.0f}, .cornerRadius = 8.0f, .background = colours.tile, .hoverBackground = colours.chosen, .focusBackground = colours.chosen});
    fStack.justify = nodes::justify::space_between{};
    mark.setColour(colours.text);
    mark.apply({.height = 24.0f});
    label.apply({.alignSelf = scene::align::kMiddle});
  }

  [[nodiscard]] bool focusChangesAppearance() const { return true; }
  [[nodiscard]] scene::Semantics semantics() const {
    scene::Semantics out;
    out.fRole = scene::semantic_role::button{};
    out.fLabel = parts.label.text();
    out.fActions = {scene::semantic_action::focus{}, scene::semantic_action::activate{}};
    return out;
  }
};

// Someone in a group, in its info: avatar, name, how they are, and their
// role in a pill. Pressed, they are shown on a page of their own.
template <class Open>
struct member_row : nodes::Stack {
  // Who it shows and how they are: while the same, the row is kept.
  member who;
  std::string how_shown;
  Open open;
  std::string id;
  std::optional<std::string> role;
  // Their name, and how they are under it.
  struct texts_column : two_lines {
    texts_column(const palette& colours, std::string shown, std::string how) : two_lines(colours, std::move(shown), std::move(how), 14.0f, 4.0f) {}
  };
  // Their role, in a pill beside their name.
  struct role_pill : widgets::Pill {
    explicit role_pill(std::string what)
        : widgets::Pill(std::move(what), {.plate = skia::colorSetARGB(255, 62, 52, 96),
                                          .text = skia::colorSetARGB(255, 190, 170, 250),
                                          .size = 12.0f,
                                          .height = 20.0f,
                                          .padX = 8.0f}) {
      fState.apply({.alignSelf = scene::align::kStart, .margin = {10.0f, 0.0f, 0.0f, 0.0f}});
    }
  };
  struct parts_t {
    avatar_mark face;
    texts_column texts;
    role_pill pill;
  } parts;

  // Declared: the avatar, the name over how they are, the role at the end.
  member_row(const palette& colours, const member& one, std::string how, Open what)
      : who(one), how_shown(how), open(std::move(what)), id(one.id), role(one.role),
        parts{.face = avatar_mark(one.id, one.name.empty() ? one.id : one.name, 40.0f),
              .texts = texts_column(colours, one.name.empty() ? one.id : one.name, std::move(how)),
              .pill = role_pill(one.role.value_or(""))} {
    this->setHorizontal();
    this->setGap(12.0f);
    fState.apply({.fillX = true, .height = 54.0f, .padding = {0.0f, 16.0f, 0.0f, 16.0f}, .hoverBackground = colours.chosen});
    fState.setRecorded(true);  // played back as the list repaints around it
    parts.pill.setVisible(one.role.has_value());
  }
  [[nodiscard]] bool acceptsInput() const { return true; }
  [[nodiscard]] bool hoverChangesAppearance() const { return true; }
  [[nodiscard]] bool onClick(float, float) {
    open(*this);
    return true;
  }
};

// A round avatar on its own: the chat's, big, over its name.
struct big_avatar : avatar_mark {
  big_avatar() : avatar_mark(std::string(), std::string(), 96.0f) {}
};
// An icon on its own, not to be pressed.
struct icon_view : nodes::Icon {
  icon_view(const palette& colours, icon_t mark) : nodes::Icon(shape_of(mark), colours.dim) { fState.apply({.width = 28.0f, .height = 36.0f}); }
};
// A band between sections: just darker than the panel.
inline nodes::Box<> section_band(const palette& colours) {
  nodes::Box<> out{colours.section};
  out.apply({.fillX = true, .height = 6.0f, .margin = {6.0f, 0.0f, 6.0f, 0.0f}});
  return out;
}

// A chat's info, beside it, as Telegram Desktop shows it: a big avatar, the
// name and who is in it, three square buttons, its ID, and its members.
// Declared: a column of these, nothing placed by hand.
// An ID, whole -- wrapped, never cut -- and copied when pressed: a chat's
// or a person's.
struct id_line : nodes::Stack {
  struct parts_t {
    nodes::Text id;
    nodes::Text label;
  } parts;
  std::string copied;
  bool a_link = false;  // what is copied is a link to it, not the ID
  std::string named;    // what it is: ID, Address
  id_line(const palette& colours, std::string text, std::string link, std::string label = "ID")
      : parts{.id = nodes::Text(text, 14.0f, colours.accent), .label = nodes::Text(label, 12.0f, colours.dim)},
        copied(link.empty() ? text : link), a_link(!link.empty()), named(std::move(label)) {
    this->setGap(2.0f);
    fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {8.0f, 20.0f, 8.0f, 20.0f}, .hoverBackground = colours.chosen, .focusBackground = colours.chosen});
    fState.setCursor(scene::cursor::hand{});
    parts.id.setWrapped(true);
    parts.id.apply({.fillX = true});
    // Selectable, as any text shown: a drag takes part of it, the right
    // button its Copy; a click still copies it whole.
    parts.id.setSelectable(true);
  }
  [[nodiscard]] bool acceptsInput() const { return true; }
  [[nodiscard]] bool hoverChangesAppearance() const { return true; }
  [[nodiscard]] bool onClick(float, float) {
    skiff::scene::setClipboardText(copied);
    parts.label.setText(a_link ? named + " · link copied, with its servers" : named + " · copied");
    return true;
  }
};

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
};
[[nodiscard]] inline person_facts person_of(const ui_shared& shared, const conversation* in, const model& now, const account_id& account,
                                            const std::string& id) {
  person_facts out{id, presence_of(shared, now, account, id)};
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
