// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:chat_list -- A chat in the list.
export module mux.ui:chat_list;

import std;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes.flow;
import skiff.nodes.text;
import skiff.widgets.pill;
import mux.core;
import mux.config;
import :base;
import :controls;
import :themes;
import :names;
import :html;
import :message;

export namespace mux::ui {

// One chat in the list, as Telegram Desktop draws it: a round avatar, the
// name, the time of the last message, a line of it, and how many are unread.
// What a message carries, as Telegram's chat list says it: a picture, a
// video or a GIF by its mark and its caption -- or, with none, what it is;
// a voice message as one; a sound or a file by its name. Nothing for a
// message that carries nothing.
[[nodiscard]] inline std::optional<std::string> media_line(const message& said) {
  if (!said.attachment)
    return std::nullopt;
  const attachment& carried = *said.attachment;
  // Its caption: its body, where that is not its file's name.
  const std::string caption = said.body.plain != carried.name ? said.body.plain : std::string();
  const auto with = [&](std::string_view mark, std::string_view kind) {
    return std::format("{} {}", mark, caption.empty() ? kind : std::string_view(caption));
  };
  if (carried.video)
    return with("\U0001F3A5", "Video");
  if (is_picture(carried.kind))
    return moves(carried.kind) ? with("\U0001F39E", "GIF") : with("\U0001F5BC", "Photo");
  if (audio_type(carried.mimetype, carried.name)) {
    const bool voice = carried.mimetype.starts_with("audio/ogg") || carried.name.ends_with(".ogg") ||
                       carried.name.ends_with(".opus") || carried.name.ends_with(".oga");
    return voice ? with("\U0001F3A4", "Voice message") : std::format("\U0001F3B5 {}", carried.name);
  }
  return std::format("\U0001F4CE {}", carried.name.empty() ? std::string("File") : carried.name);
}

// A node of a chat's protocol's own in its row of the list, after what was
// said last (a Telegram channel's views, an IRC channel's modes): listed by
// row_views(state, type_tag<Actions>), made for a chat by make_row_view,
// found by ADL; none by default.
namespace row_view_defaults {
template <class Actions>
constexpr proto::sticker_view_list<> row_views(const auto&, type_tag<Actions>) {
  return {};
}
template <class Actions>
constexpr std::nullopt_t make_row_view(const auto&, const conversation&, type_tag<Actions>) {
  return std::nullopt;
}
}  // namespace row_view_defaults
template <class State, class Actions>
constexpr auto row_views_for(const State& state, type_tag<Actions> tag) {
  using row_view_defaults::row_views;
  return row_views(state, tag);
}

template <class Actions>
struct conversation_row : nodes::Stack {
  template <class List>
  struct view_nodes;
  template <class... Vs>
  struct view_nodes<proto::sticker_view_list<Vs...>> {
    using type = type_list<Vs...>;
  };
  template <class>
  struct protocol_row_nodes;
  template <class... Tags>
  struct protocol_row_nodes<protocol_list<Tags...>> {
    using type = typename joined<
        type_list<>, typename view_nodes<decltype(row_views_for(::mux::state_of<Tags>{}, type_tag<Actions>{}))>::type...>::type;
  };
  using row_view_t = typename variant_of_types<
      typename joined<type_list<nodes::Text>, typename protocol_row_nodes<protocols>::type>::type>::type;
  struct row_view_holder : nodes::Stack {
    struct parts_t {
      row_view_t shown;
    } parts;
    template <class View>
    explicit row_view_holder(View made) : parts{.shown = row_view_t(std::move(made))} {
      fState.apply({.autoSize = scene::axes::kBoth});
    }
  };
  Actions* actions = nullptr;
  conversation_id id;
  bool chosen = false;
  bool muted = false;
  // The name and the time over the last message and the unread count.
  struct lines_column : nodes::Stack {
    struct top_line : name_time_line {
      top_line(const palette& colours, std::string shown, bool chosen)
          : name_time_line(std::move(shown), "", chosen ? colours.selected_text : colours.text,
                           chosen ? colours.selected_text : colours.dim, 13.0f) {}
    };
    struct bottom_line : nodes::Stack {
      // The chats' unread count, in a pill.
      struct badge : widgets::Pill {
        badge(const palette& colours, std::int64_t n, bool is_chosen, bool is_muted)
            : widgets::Pill(std::to_string(n),
                            {.plate = is_chosen ? colours.selected_text : is_muted ? colours.dim : colours.accent,
                             .text = is_chosen ? colours.selected : colours.on_accent,
                             .size = 12.0f,
                             .height = 21.0f,
                             .padX = 7.0f,
                             .bold = true}) {}
      };
      // tdesktop's line: who said it -- "You:", a member's name, "Draft:"
      // -- in its own colour (dialogsTextFgService), then what was said,
      // its mentions as the bubble draws them: pills with their avatars.
      // What the chat's protocol marks it with (proto::row_badges), each a
      // pill in its tone, as the count is.
      struct mark : widgets::Pill {
        mark(const palette& colours, const proto::part::badge& one)
            : widgets::Pill(one.text, {.plate = tone_colour(colours, one.tone), .text = colours.on_accent, .size = 11.0f, .height = 19.0f,
                                       .padX = 6.0f, .bold = true}) {}
      };
      struct parts_t {
        nodes::Text sender;
        nodes::BasicText<message_pictures> preview;
        std::vector<mark> marks;
        // Its protocol's own node (make_row_view).
        std::optional<row_view_holder> theirs;
        badge unread;
      } parts;
      bottom_line(const palette& colours, std::int64_t count, bool chosen, bool muted)
          : parts{.sender = nodes::Text("", 13.0f, chosen ? colours.selected_text : colours.accent),
                  .preview = nodes::BasicText<message_pictures>("", 13.0f, chosen ? colours.selected_text : colours.dim),
                  .unread = badge(colours, count, chosen, muted)} {
        this->setHorizontal();
        this->setGap(8.0f);
        fState.apply({.fillX = true, .autoSize = scene::axes::kY});
        this->setGap(4.0f);
        parts.sender.apply({.alignSelf = scene::align::kMiddle});
        parts.unread.apply({.margin = {0.0f, 0.0f, 0.0f, 4.0f}});
        parts.sender.setVisible(false);
        // A member's long name gives way too, cut with an ellipsis: not cut, it
        // pushed the unread count past the row's end once the preview had
        // given all it could.
        parts.sender.setElided(true);
        parts.preview.setElided(true);
        parts.preview.apply({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
        parts.unread.setVisible(count > 0);
      }
    };
    struct parts_t {
      top_line top;
      // A forum's: the topic -- the room -- its newest is in, on a line of
      // its own between the name and what was said, as tdesktop's.
      nodes::Text topic;
      bottom_line bottom;
    } parts;
    lines_column(const palette& colours, std::string shown, std::int64_t count, bool chosen, bool muted)
        : parts{.top = top_line(colours, std::move(shown), chosen),
                .topic = nodes::Text("", 13.0f, chosen ? colours.selected_text : colours.text),
                .bottom = bottom_line(colours, count, chosen, muted)} {
      parts.topic.setElided(true);
      parts.topic.apply({.fillX = true});
      parts.topic.setVisible(false);
      this->setGap(6.0f);
      fState.apply({.autoSize = scene::axes::kY, .grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
    }
  };
  struct parts_t {
    avatar_mark face;
    lines_column lines;
    // Listed in another account's list than its own: a rounded strip on its
    // left in its colour -- out of the flow, in the row's padding.
    nodes::Box<> strip{0u};
  } parts;

  static constexpr float kHeight = 62.0f;
  // The height its row will have, without making it: a forum's, with a
  // message, has the topic on a line of its own.
  [[nodiscard]] static float height_of(const conversation& one) {
    return one.forum_topic && !one.timeline.empty() ? kHeight + 20.0f : kHeight;
  }
  void place_view(std::nullopt_t) {}
  template <class View>
  void place_view(std::optional<View> made) {
    if (made)
      parts.lines.parts.bottom.parts.theirs.emplace(std::move(*made));
  }

  // Declared: the avatar, then the name and time over the last message and
  // how many are unread.
  // What a row shows of its chat: while that is the same, the row is kept.
  struct view {
    std::string name;
    std::optional<message> last;
    std::int64_t unread = 0;
    bool chosen = false, muted = false;
    std::string draft;
    std::optional<invite_info> invite;
    std::optional<skia::SkColor> strip;
    std::vector<proto::part::badge> badges;
    friend bool operator==(const view&, const view&) = default;
  };
  // What it says of the chat -- its newest and its count -- as the chat
  // shows it: the room events it hides left out of both.
  [[nodiscard]] static view view_of(const ui_shared& shared, const conversation& one, bool is_chosen, bool is_muted, std::string draft = {},
                                    const room_event_filter& events = {}, std::optional<skia::SkColor> strip = std::nullopt) {
    const message* last = newest(one, events);
    return {display_name(one), last ? std::optional<message>(*last) : std::nullopt, one.unread_here(events), is_chosen,
            is_muted, std::move(draft), one.invite, strip, proto::row_badges(protocol_state_of(shared, one.id.account), one)};
  }
  view shown;

  conversation_row(const ui_needs<Actions>& n, const conversation& one, bool is_chosen, bool is_muted, std::string draft = {},
                   const room_event_filter& events = {}, std::optional<skia::SkColor> strip = std::nullopt)
      : actions(n.actions), id(one.id), chosen(is_chosen), muted(is_muted), shown(view_of(*n.shared, one, is_chosen, is_muted, draft, events, strip)),
        parts{.face = avatar_mark(one.id.id, display_name(one), 46.0f),
              .lines = lines_column(*n.colours, display_name(one), one.unread_here(events), is_chosen, is_muted)} {
    const palette& colours = *n.colours;
    // Drawn once, played back as the list repaints around it.
    fState.setRecorded(true);
    std::ranges::for_each(shown.badges, [&](const proto::part::badge& one) {
      parts.lines.parts.bottom.parts.marks.emplace_back(colours, one).apply({.alignSelf = scene::align::kMiddle});
    });
    spl::visit(
        [&](const auto& now) {
          using row_view_defaults::make_row_view;
          this->place_view(make_row_view(now, one, type_tag<Actions>{}));
        },
        protocol_state_of(*n.shared, one.id.account));
    auto& time = parts.lines.parts.top.parts.time;
    auto& preview = parts.lines.parts.bottom.parts.preview;
    auto& sender = parts.lines.parts.bottom.parts.sender;
    const auto said_by = [&](std::string who, skia::SkColor colour) {
      sender.setText(std::move(who) + ":");
      if (!is_chosen)
        sender.setColour(colour);
      sender.setVisible(true);
    };
    this->setHorizontal();
    this->setGap(12.0f);
    fState.apply({.fillX = true, .height = kHeight, .padding = {0.0f, 12.0f, 0.0f, 10.0f}, .hoverBackground = colours.chosen, .selectedBackground = colours.selected, .focusBackground = colours.chosen, .selected = chosen});
    parts.strip.setVisible(strip.has_value());
    if (strip)
      parts.strip.setColour(*strip);
    parts.strip.apply({.place = scene::anchor::kCentreLeft, .x = -8.0f, .width = 4.0f, .height = 38.0f, .cornerRadius = 2.0f});
    if (const message* newest_one = newest(one, events)) {
      const message last = with_actor(one, *newest_one);
      time.setText(clock_of(last.at));
      // What it says, as drawn: an HTML one's text, not its tags, and
      // its mentions by name, as pills.
      mentioned shown;
      if (last.body.html) {
        auto read = read_html(*last.body.html);
        shown = with_mentions(std::move(read.text), std::move(read.spans), one, nullptr);
      } else {
        shown = with_mentions(last.body.plain, link_spans_in(last.body.plain), one, nullptr);
      }
      std::ranges::replace(shown.text, '\n', ' ');
      std::erase_if(shown.links, [](const nodes::Text::Link& link) { return !link.pill; });
      // A forum's: the topic it is in on a line of its own -- the row taller
      // for it -- then who, before what was said.
      if (one.forum_topic) {
        auto& topic = parts.lines.parts.topic;
        topic.setText(*one.forum_topic);
        topic.setVisible(true);
        fState.apply({.height = kHeight + 20.0f});
        said_by(last.outgoing ? std::string("You") : sender_name(one, last.sender), colours.accent);
      } else if (last.outgoing)
        said_by("You", colours.accent);
      else if (is_group(one))
        said_by(sender_name(one, last.sender), colours.accent);
      // Media: said as Telegram says it, not by the file's name its body is.
      if (auto carried = media_line(last)) {
        preview.setText(std::move(*carried));
        preview.setLinks({}, colours.accent);
      } else {
        preview.setText(std::move(shown.text));
        preview.setLinks(std::move(shown.links), colours.accent);
      }
    }
    // An invite: who asked, in the accent, where a message would be.
    if (one.invite) {
      said_by("Invite", colours.accent);
      preview.setText(std::format("from {}", one.invite->from_name.empty() ? one.invite->from : one.invite->from_name));
      preview.setLinks({}, colours.accent);
    }
    // A draft left in it: said instead, as tdesktop says it, in red.
    if (!shown.draft.empty() && !is_chosen) {
      std::string text = shown.draft;
      std::ranges::replace(text, '\n', ' ');
      said_by("Draft", colours.error);
      preview.setText(std::move(text));
      preview.setLinks({}, colours.accent);
    }
  }

  [[nodiscard]] bool acceptsInput() const { return true; }
  [[nodiscard]] bool hoverChangesAppearance() const { return true; }
  [[nodiscard]] bool onClick(float, float) {
    actions->choose(id);
    return true;
  }
  [[nodiscard]] scene::Semantics semantics() const {
    scene::Semantics out;
    out.fRole = scene::semantic_role::list_item{};
    out.fLabel = parts.lines.parts.top.parts.name.text();
    out.fSelected = chosen;
    out.fActions = {scene::semantic_action::focus{}, scene::semantic_action::activate{}};
    return out;
  }
};

}  // namespace mux::ui
