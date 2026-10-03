// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:info_reactions -- A message's reactions as events, and the marks not yet seen, listed.
export module mux.ui:info_reactions;

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
  std::string to;  // the message reacted to
};

// A message's reactions as events, as Matrix has them: a box in the middle,
// a row for each -- the person's avatar and name over what they reacted
// with and when. A press on one answers it: the reaction is an event, and a
// message can reply to it.
template <class Actions>
struct reactions_box : nodes::Stack {
  // On the chat's colour: its bubbles, as in the chat.
  [[nodiscard]] static dialog_look look_of_dialog() { return {.sheet = sheet::chat{}, .size = dialog_size::fixed{392.0f, 420.0f}}; }
  using close_act = ask<Actions, &Actions::close_reactions>;
  using close_button = icon_button<close_act>;
  using top_bar = page_header<no_back, close_act>;
  // A reaction as the chat would show it: a bubble from who reacted,
  // saying what they reacted with, in runs as the chat's bubbles are.
  // Pressed, it is answered.
  struct row : nodes::Stack {
    Actions* actions;
    reaction_entry entry;
    struct parts_t {
      message_bubble<Actions> bubble;
    } parts;
    row(const ui_needs<Actions>& n, const conversation& in, reaction_entry one, bool first, bool last, const model* now)
        : actions(n.actions), entry(one),
          parts{.bubble = message_bubble<Actions>(splice::remapped<typename message_bubble<Actions>::needs>(n), in, message_of(in, one), first, last, now)} {
      fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 12.0f, 0.0f, 12.0f},
                    .hoverBackground = chosen_colour});
      fState.setCursor(scene::cursor::hand{});
    }
    // What it said, as a message: its key; a picture's, as a custom emoji
    // in HTML, as a message would carry it.
    [[nodiscard]] static message message_of(const conversation& in, const reaction_entry& one) {
      message out{.in = in.id, .id = one.event, .sender = one.who, .at = one.at, .body = {one.key, std::nullopt},
                  .outgoing = one.mine};
      if (proto::is_media(one.key))
        out.body = {":emoji:", std::format("<img data-mx-emoticon src=\"{}\" alt=\":emoji:\" height=\"32\">", one.key)};
      return out;
    }
    [[nodiscard]] bool acceptsInput() const { return true; }
    [[nodiscard]] bool hoverChangesAppearance() const { return true; }
    // Its menu, as a message's in the chat: Copy -- what is selected, the
    // link under the pointer, the preview's -- and, one's own, the reaction
    // changed to another from the menu's reactions, or taken back.
    using scene::Node::onPointer;
    void onPointer(scene::phase::bubble, const scene::pointer::down& press, scene::PointerReply& reply) {
      if (press.button != 3)
        return;
      const message_bubble<Actions>& one = parts.bubble;
      menu_facts facts;
      facts.id = entry.event;
      facts.own = entry.mine;
      facts.text = one.plain;
      facts.selection = one.parts.body.parts.text.hasSelection();
      facts.copied = facts.selection ? one.parts.body.parts.text.selected() : one.plain;
      facts.deletable = entry.mine && !entry.event.empty();
      if (const auto& asked = skiff::nodes::textMenusAsked(); !asked.empty() && asked.back().link)
        facts.pressed_link = *asked.back().link;
      else if (const auto& preview = one.parts.body.parts.preview; preview && preview->fState.fBounds.contains(press.x, press.y))
        facts.pressed_link = preview->url;
      if (!entry.event.empty() && !entry.to.empty())
        facts.reaction = menu_facts::reaction_facts{entry.to, entry.key};
      facts.x = press.x;
      facts.y = press.y;
      actions->message_menu(std::move(facts));
      reply.handle();
    }
    [[nodiscard]] bool onClick(float x, float y) {
      // A link's preview or card in it: followed, as in the chat.
      const message_bubble<Actions>& one = parts.bubble;
      if (const auto& preview = one.parts.body.parts.preview; preview && preview->bounds().contains(x, y)) {
        actions->open_url(preview->url);
        return true;
      }
      for (const link_card& card : one.parts.body.parts.cards)
        if (card.bounds().contains(x, y)) {
          actions->open_url(card.url);
          return true;
        }
      // Answered, where it is an event of its own to answer.
      if (!entry.event.empty())
        actions->reply_to(entry.event, std::format("{} reacted {}", entry.name, entry.key));
      actions->close_reactions();
      return true;
    }
  };
  using rows_t = nodes::Flow<std::vector<row>>;
  struct parts_t {
    top_bar top;
    nodes::ScrollContainer<rows_t> list{rows_t({.spacingY = 0.0f, .wrap = false}, {})};
  } parts;

  reactions_box(const ui_needs<Actions>& n, const conversation& in, const std::vector<reaction_entry>& entries, const model* now)
      : parts{.top = top_bar("Reactions", {}, {n.actions}, false, true)} {
    fState.apply({.fillX = true, .height = 420.0f, .padding = {0.0f, 0.0f, 12.0f, 0.0f}});
    parts.list.apply({.fillX = true, .grow = scene::axes::kY});
    auto& flow = std::get<0>(parts.list.fChildren);
    flow.apply({.fillX = true, .autoSize = scene::axes::kY});
    auto& rows = std::get<0>(flow.fChildren);
    rows.reserve(entries.size());
    for (std::size_t i = 0; i < entries.size(); ++i)
      rows.emplace_back(n, in, entries[i], i == 0 || entries[i - 1].who != entries[i].who,
                        i + 1 == entries.size() || entries[i + 1].who != entries[i].who, now);
  }
};

// What a mark list shows of each: the message, and for a reaction to it who
// reacted and with what.
struct mark_entry {
  message said;
  std::string event;  // the mark's own event
  std::optional<std::string> who;
  std::string key;
};

// The mentions of the user or the reactions to theirs not yet seen, as a
// list of the chat's bubbles: each its message -- a reaction's with who
// reacted and with what on a badge at its bottom right. Pressed, gone to.
template <class Actions>
struct marks_box : nodes::Stack {
  // On the chat's colour: its bubbles, as in the chat.
  [[nodiscard]] static dialog_look look_of_dialog() { return {.sheet = sheet::chat{}, .size = dialog_size::fixed{460.0f, 520.0f}}; }
  using close_act = ask<Actions, &Actions::close_marks>;
  using close_button = icon_button<close_act>;
  using top_bar = page_header<no_back, close_act>;
  struct badge : nodes::Stack {
    struct parts_t {
      avatar_mark face;
      nodes::Text key;
    } parts;
    badge(const std::string& who, const std::string& name, const std::string& key)
        : parts{.face = avatar_mark(who, name, 20.0f),
                .key = nodes::Text(proto::is_media(key) ? std::string(":emoji:") : key, 15.0f, text_colour)} {
      this->setHorizontal();
      this->setGap(4.0f);
      fState.apply({.place = scene::anchor::kBottomRight, .x = -14.0f, .y = -2.0f, .autoSize = scene::axes::kBoth,
                    .padding = {2.0f, 6.0f, 2.0f, 3.0f}, .cornerRadius = 12.0f, .background = sidebar_colour});
      parts.key.apply({.alignSelf = scene::align::kMiddle});
    }
  };
  struct row : nodes::Stack {
    Actions* actions = nullptr;
    mark_kind_t kind;
    std::string event;
    struct parts_t {
      message_bubble<Actions> bubble;
      std::optional<badge> reacted;
    } parts;
    row(const ui_needs<Actions>& n, mark_kind_t which, const conversation& in, const mark_entry& one, const model* now)
        : actions(n.actions), kind(which), event(one.event),
          parts{.bubble = message_bubble<Actions>(splice::remapped<typename message_bubble<Actions>::needs>(n), in, one.said, true, true, now)} {
      fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {4.0f, 12.0f, 8.0f, 12.0f},
                    .hoverBackground = chosen_colour});
      fState.setCursor(scene::cursor::hand{});
      if (one.who)
        parts.reacted.emplace(*one.who, sender_name(in, *one.who), one.key);
    }
    [[nodiscard]] bool acceptsInput() const { return true; }
    [[nodiscard]] bool hoverChangesAppearance() const { return true; }
    [[nodiscard]] bool onClick(float, float) {
      actions->go_to_mark(kind, event);
      actions->close_marks();
      return true;
    }
  };
  using rows_t = nodes::Flow<std::vector<row>>;
  struct parts_t {
    top_bar top;
    nodes::ScrollContainer<rows_t> list{rows_t({.spacingY = 0.0f, .wrap = false}, {})};
  } parts;
  marks_box(const ui_needs<Actions>& n, mark_kind_t kind, const conversation& in, const std::vector<mark_entry>& entries, const model* now)
      : parts{.top = top_bar(splice::visit(splice::overloaded{[](mark_kind::mention) { return std::string("Mentions"); },
                                                   [](mark_kind::reaction) { return std::string("Reactions"); }},
                                kind),
                             {}, {n.actions}, false, true)} {
    fState.apply({.fillX = true, .height = 520.0f, .padding = {0.0f, 0.0f, 12.0f, 0.0f}});
    parts.list.apply({.fillX = true, .grow = scene::axes::kY});
    auto& flow = std::get<0>(parts.list.fChildren);
    flow.apply({.fillX = true, .autoSize = scene::axes::kY});
    auto& rows = std::get<0>(flow.fChildren);
    rows.reserve(entries.size());
    for (const mark_entry& one : entries)
      rows.emplace_back(n, kind, in, one, now);
  }
};

}  // namespace mux::ui
