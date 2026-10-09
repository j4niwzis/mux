// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:info_reactions -- A message's reactions as events, and the marks not yet seen, listed.
export module mux.ui:info_reactions;

import std;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.compose;
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
import :info_common;
import :info_cards;

export namespace mux::ui {
// A reaction as the event it is: who, with what, when.
struct reaction_entry {
  std::string event;
  std::string who;
  std::string name;
  std::string key;
  std::chrono::sys_time<std::chrono::milliseconds> at{};
  bool mine = false;
  std::string to;  // the event reacted to
  std::optional<std::string> shortcode;
};

// A message's reactions as events, as Matrix has them: a box in the middle,
// a row for each -- the person's avatar and name over what they reacted
// with and when. A press on one answers it: the reaction is an event, and a
// message can reply to it.
// A list dialog's rows, under its top bar: the box as high as it is, the
// list scrolling in what is left of it, its rows one under another.
[[nodiscard]] inline skiff::compose::Look list_dialog(float height) {
  return skiff::compose::vbox(
      0.0f,
      {.fillX = true, .height = height, .padding = {0.0f, 0.0f, 12.0f, 0.0f}});
}
template <class Rows> [[nodiscard]] auto dialog_list(Rows rows) {
  return skiff::compose::styled(
      {.fillX = true, .grow = scene::axes::kY},
      nodes::ScrollContainer<Rows>(skiff::compose::styled(
          {.fillX = true, .autoSize = scene::axes::kY}, std::move(rows))));
}

// A chat as the chats hold it now: none, where it is gone.
[[nodiscard]] inline const conversation& chat_or_none(const model* now, const conversation_id& in) {
  static const conversation none{};
  const conversation* found = now == nullptr ? nullptr : now->find(in);
  return found == nullptr ? none : *found;
}
// A message's reactions open: in which chat, the reactions, the chats.
struct reactions_facts {
  conversation_id in;
  std::vector<reaction_entry> entries;
  const model* now = nullptr;
  std::string event;
  bool back = false;
};
// Panel rows use the actual reaction event as their menu target. Inherit its
// nested reactions when known; the parent event is only the reply reference.
[[nodiscard]] inline message reaction_message_of(const conversation& in, const reaction_entry& entry) {
  message out;
  if (const message* held = held_message(in, entry.event)) out = *held;
  out.in = in.id;
  out.id = entry.event;
  out.sender = entry.who;
  out.at = entry.at;
  out.body = reaction_body_of(entry.key, in.emotes, entry.shortcode);
  out.outgoing = entry.mine;
  out.service = false;
  out.reaction = true;
  out.reaction_key = entry.key;
  out.replies_to = entry.to.empty() ? std::nullopt : std::optional(entry.to);
  return out;
}
using reaction_row_answer = std::variant<menu_facts, request::open_url,
    std::tuple<std::optional<request::reply_to>, request::close_reactions>>;

template <class Actions>
auto reaction_event_row(const ui_needs<Actions>& n, const conversation& in, const reaction_entry& entry,
                        bool first, bool last, const model* known) {
  namespace c = skiff::compose;
  auto bubble = message_bubble<Actions>(spl::remapped<typename message_bubble<Actions>::needs>(n),
                                        in, reaction_message_of(in, entry), first, last, known);
  auto row = c::onClick(std::tuple{entry.event.empty() ? std::optional<request::reply_to>{}
        : std::optional(request::reply_to{entry.event, std::format("{} reacted {}", entry.name, entry.key)}), request::close_reactions{}},
      c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY,
          .padding = {0.0f, 12.0f, 0.0f, 12.0f}, .hoverBackground = n.colours->chosen}), std::move(bubble)), "Reply to reaction");
  return c::onPointer<reaction_row_answer>([in_id = in.id, known](auto& row, const scene::pointer::down& press,
                                                                scene::PointerReply& reply) -> std::optional<reaction_row_answer> {
    if (press.button != 3) return std::nullopt;
    const auto& bubble = std::get<0>(row.fParts);
    const auto* chat = known ? known->find(in_id) : nullptr;
    auto facts = facts_of_bubble(bubble, chat, press.x, press.y);
    // Legacy cached reactions without an event ID cannot be targeted.
    if (facts.id.empty()) { facts.can.react = false; facts.deletable = false; }
    reply.handle();
    return reaction_row_answer{std::move(facts)};
  }, std::move(row));
}
template <class Actions>
auto reactions_panel(const ui_needs<Actions>& n, const reactions_facts& facts) {
  namespace c = skiff::compose;
  const auto& chat = chat_or_none(facts.now, facts.in);
  const auto& entries = facts.entries;
  auto rows = std::ranges::to<std::vector>(std::views::transform(std::views::iota(std::size_t{0}, entries.size()),
      [&](std::size_t i) { return reaction_event_row(n, chat, entries[i],
          i == 0 || entries[i - 1].who != entries[i].who,
          i + 1 == entries.size() || entries[i + 1].who != entries[i].who, facts.now); }));
  return c::column(list_dialog(420.0f),
      page_header<sends<request::back_reactions>, sends<request::close_reactions>>(*n.colours, "Reactions", {}, {}, facts.back, true),
      dialog_list(c::many(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), std::move(rows))));
}
template <class Actions>
using reactions_box = decltype(reactions_panel(std::declval<const ui_needs<Actions>&>(), std::declval<const reactions_facts&>()));
template <class Actions>
auto make_content(std::type_identity<reactions_box<Actions>>, const ui_needs<Actions>& n, const reactions_facts& facts) {
  return reactions_panel(n, facts);
}
template <class Content>
inline dialog_look content_look(std::type_identity<Content>, std::type_identity<reactions_facts>) {
  return {.sheet = sheet::chat{}, .size = dialog_size::fixed{392.0f, 420.0f}};
}

// A message's edit history, as AyuGram Desktop's: each version of it the
// chat's own bubble, on the chat's colour, oldest first, each at the time
// it was written -- the message as it is now last, the list scrolled to it.
// A message's earlier versions open: in which chat, the message, the chats.
struct history_facts {
  conversation_id in;
  message said;
  const model* known = nullptr;
};
// Version IDs are the events that wrote their bodies. Legacy histories
// without edit IDs remain readable, but do not guess a reaction target.
[[nodiscard]] inline std::vector<message> edit_versions_of(const message& now, const conversation* chat = nullptr) {
  const auto as_message = [&](const mux::body& body, auto at, std::string id) {
    message out = now;
    out.id = std::move(id);
    out.body = body;
    out.at = at;
    out.versions.clear();
    out.latest_edit_event.clear();
    out.edited = false;
    out.reactions.clear();
    out.reaction_events.clear();
    if (chat)
      if (const auto* event = held_message(*chat, out.id)) {
        out.reactions = event->reactions;
        out.reaction_events = event->reaction_events;
        out.redacted = event->redacted;
      }
    return out;
  };
  auto versions = std::ranges::to<std::vector>(std::views::transform(std::views::iota(std::size_t{0}, now.versions.size()),
      [&](std::size_t i) { return as_message(now.versions[i].body, i == 0 ? now.at : now.versions[i - 1].until,
          now.versions[i].event.empty() && i == 0 ? now.id : now.versions[i].event); }));
  versions.push_back(as_message(now.body, now.versions.empty() ? now.at : now.versions.back().until,
      now.latest_edit_event.empty() && now.versions.empty() ? now.id : now.latest_edit_event));
  return versions;
}
template <class Actions>
auto edit_history_row(const ui_needs<Actions>& n, const conversation& in, const message& event, const model* known) {
  namespace c = skiff::compose;
  return c::onPointer<menu_facts>([in_id = in.id, known](auto& row, const scene::pointer::down& press,
                                                      scene::PointerReply& reply) -> std::optional<menu_facts> {
    if (press.button != 3) return std::nullopt;
    auto facts = facts_of_bubble(std::get<0>(row.fParts), known ? known->find(in_id) : nullptr, press.x, press.y);
    facts.editable = false;
    facts.history = false;
    reply.handle();
    return facts;
  }, c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {4.0f, 12.0f, 4.0f, 12.0f}}),
      message_bubble<Actions>(spl::remapped<typename message_bubble<Actions>::needs>(n), in, event, true, true, known)));
}
template <class Actions>
auto edit_history_panel(const ui_needs<Actions>& n, const history_facts& facts) {
  namespace c = skiff::compose;
  const auto& chat = chat_or_none(facts.known, facts.in);
  const auto versions = edit_versions_of(facts.said, &chat);
  auto rows = std::ranges::to<std::vector>(std::views::transform(versions, [&](const message& event) {
    return edit_history_row(n, chat, event, facts.known);
  }));
  auto list = dialog_list(c::many(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), std::move(rows)));
  list.scrollToEnd(false);
  return c::column(list_dialog(560.0f),
      page_header<no_back, sends<request::close_edit_history>>(*n.colours, "Edit History", {}, {}, false, true), std::move(list));
}
template <class Actions>
using edit_history_box = decltype(edit_history_panel(std::declval<const ui_needs<Actions>&>(), std::declval<const history_facts&>()));
template <class Actions>
auto make_content(std::type_identity<edit_history_box<Actions>>, const ui_needs<Actions>& n, const history_facts& facts) {
  return edit_history_panel(n, facts);
}
template <class Content>
inline dialog_look content_look(std::type_identity<Content>, std::type_identity<history_facts>) {
  return {.sheet = sheet::chat{}, .size = dialog_size::fixed{460.0f, 560.0f}};
}

// What a mark list shows of each: the message, and for a reaction to it who
// reacted and with what.
struct mark_entry {
  message said;
  std::string event;  // the mark's own event
  std::optional<std::string> who;
  std::string key;
};

// A list of marks open: of which kind, in which chat, what it lists, and
// the chats it shows them from.
struct marks_facts {
  mark_kind_t kind;
  conversation_id in;
  std::vector<mark_entry> entries;
  const model* now = nullptr;
};

// The mentions of the user or the reactions to theirs not yet seen, as a
// list of the chat's bubbles: each its message -- a reaction's with who
// reacted and with what on a badge at its bottom right. Pressed, gone to.
template <class Actions> struct marks_box : skiff::compose::Stacked {
  // On the chat's colour: its bubbles, as in the chat.
  [[nodiscard]] static dialog_look look_of_dialog() { return {.sheet = sheet::chat{}, .size = dialog_size::fixed{460.0f, 520.0f}}; }
  using close_act = sends<::mux::ui::request::close_marks>;
  using close_button = icon_button<close_act>;
  using top_bar = page_header_t<no_back, close_act>;
  struct badge : skiff::compose::Stacked {
    struct parts_t {
      avatar_mark face;
      nodes::Text key;
    } parts;
    badge(const palette &colours, const std::string &who,
          const std::string &name, const std::string &key)
        : Stacked(
              skiff::compose::hbox(4.0f, {.place = scene::anchor::kBottomRight,
                                          .x = -14.0f,
                                          .y = -2.0f,
                                          .autoSize = scene::axes::kBoth,
                                          .padding = {2.0f, 6.0f, 2.0f, 3.0f},
                                          .cornerRadius = 12.0f,
                                          .background = colours.sidebar})),
          parts{.face = avatar_mark(who, name, 20.0f),
                .key = skiff::compose::styled(
                    {.alignSelf = scene::align::kMiddle},
                    nodes::Text(proto::is_media(key) ? std::string(":emoji:")
                                                     : key,
                                15.0f, colours.text))} {}
  };
  struct row : skiff::compose::Stacked {
    // A press: to the mark, the list closed.
    using Answer = std::tuple<::mux::ui::request::go_to_mark, ::mux::ui::request::close_marks>;
    mark_kind_t kind;
    std::string event;
    struct parts_t {
      message_bubble<Actions> bubble;
      std::optional<badge> reacted;
    } parts;
    row(const ui_needs<Actions> &n, mark_kind_t which, const conversation &in,
        const mark_entry &one, const model *now)
        : Stacked(skiff::compose::vbox(0.0f,
                                       {.fillX = true,
                                        .autoSize = scene::axes::kY,
                                        .padding = {4.0f, 12.0f, 8.0f, 12.0f},
                                        .hoverBackground = n.colours->chosen})),
          kind(which), event(one.event),
          parts{.bubble = message_bubble<Actions>(
                    spl::remapped<typename message_bubble<Actions>::needs>(n),
                    in, one.said, true, true, now)} {
      fState.setCursor(scene::cursor::hand{});
      if (one.who)
        parts.reacted.emplace(*n.colours, *one.who, sender_name(in, *one.who), one.key);
    }
    [[nodiscard]] bool acceptsInput() const { return true; }
    [[nodiscard]] bool hoverChangesAppearance() const { return true; }
    std::optional<Answer> onClick(float, float) { return Answer{::mux::ui::request::go_to_mark{kind, event}, ::mux::ui::request::close_marks{}}; }
  };
  using rows_t = nodes::Flow<std::vector<row>>;
  struct parts_t {
    top_bar top;
    nodes::ScrollContainer<rows_t> list{
        dialog_list(rows_t({.spacingY = 0.0f, .wrap = false}, {}))};
  } parts;
  // Made from what the list is of; its rows from the chat as the chats
  // hold it now -- none where it is gone.
  marks_box(const ui_needs<Actions> &n, const marks_facts &facts)
      : Stacked(list_dialog(520.0f)),
        parts{.top = page_header<no_back, close_act>(
                  *n.colours,
                  spl::visit(spl::overloaded{[](mark_kind::mention) {
                                               return std::string("Mentions");
                                             },
                                             [](mark_kind::reaction) {
                                               return std::string("Reactions");
                                             }},
                             facts.kind),
                  {}, {}, false, true)} {
    auto &rows = std::get<0>(std::get<0>(parts.list.fChildren).fChildren);
    const conversation* in = facts.now == nullptr ? nullptr : facts.now->find(facts.in);
    if (in == nullptr)
      return;
    rows.reserve(facts.entries.size());
    std::ranges::for_each(facts.entries, [&](const mark_entry& one) { rows.emplace_back(n, facts.kind, *in, one, facts.now); });
  }
};

}  // namespace mux::ui
