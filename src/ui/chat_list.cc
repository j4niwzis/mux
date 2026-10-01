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

template <class Actions>
struct conversation_row : nodes::Stack {
  Actions* actions = nullptr;
  conversation_id id;
  bool chosen = false;
  bool muted = false;
  // The name and the time over the last message and the unread count.
  struct lines_column : nodes::Stack {
    struct top_line : nodes::Stack {
      struct parts_t {
        nodes::Text name;
        nodes::Text time;
      } parts;
      top_line(std::string shown, bool chosen)
          : parts{.name = nodes::Text(std::move(shown), 13.0f, chosen ? selected_text_colour : text_colour, true),
                  .time = nodes::Text("", 13.0f, chosen ? selected_text_colour : dim_colour)} {
        this->setHorizontal();
        this->setGap(8.0f);
        fState.apply({.fillX = true, .autoSize = scene::axes::kY});
        parts.name.setElided(true);
        parts.name.apply({.grow = scene::axes::kX});
      }
    };
    struct bottom_line : nodes::Stack {
      // The chats' unread count, in a pill.
      struct badge : widgets::Pill {
        badge(std::int64_t n, bool is_chosen, bool is_muted)
            : widgets::Pill(std::to_string(n),
                            {.plate = is_chosen ? selected_text_colour : is_muted ? dim_colour : accent_colour,
                             .text = is_chosen ? selected_colour : on_accent_colour,
                             .size = 12.0f,
                             .height = 21.0f,
                             .padX = 7.0f,
                             .bold = true}) {}
      };
      // tdesktop's line: who said it -- "You:", a member's name, "Draft:"
      // -- in its own colour (dialogsTextFgService), then what was said,
      // its mentions as the bubble draws them: pills with their avatars.
      struct parts_t {
        nodes::Text sender;
        nodes::BasicText<message_pictures> preview;
        badge unread;
      } parts;
      bottom_line(std::int64_t count, bool chosen, bool muted)
          : parts{.sender = nodes::Text("", 13.0f, chosen ? selected_text_colour : accent_colour),
                  .preview = nodes::BasicText<message_pictures>("", 13.0f, chosen ? selected_text_colour : dim_colour),
                  .unread = badge(count, chosen, muted)} {
        this->setHorizontal();
        this->setGap(8.0f);
        fState.apply({.fillX = true, .autoSize = scene::axes::kY});
        this->setGap(4.0f);
        parts.sender.apply({.alignSelf = scene::align::kMiddle});
        parts.unread.apply({.margin = {0.0f, 0.0f, 0.0f, 4.0f}});
        parts.sender.setVisible(false);
        parts.preview.setElided(true);
        parts.preview.apply({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
        parts.unread.setVisible(count > 0);
      }
    };
    struct parts_t {
      top_line top;
      bottom_line bottom;
    } parts;
    lines_column(std::string shown, std::int64_t count, bool chosen, bool muted)
        : parts{.top = top_line(std::move(shown), chosen), .bottom = bottom_line(count, chosen, muted)} {
      this->setGap(6.0f);
      fState.apply({.autoSize = scene::axes::kY, .grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
    }
  };
  struct parts_t {
    avatar_mark face;
    lines_column lines;
  } parts;

  static constexpr float kHeight = 62.0f;

  // Declared: the avatar, then the name and time over the last message and
  // how many are unread.
  // What a row shows of its chat: while that is the same, the row is kept.
  struct view {
    std::string name;
    std::optional<message> last;
    std::int64_t unread = 0;
    bool chosen = false, muted = false;
    std::string draft;
    friend bool operator==(const view&, const view&) = default;
  };
  // What it says of the chat -- its newest and its count -- as the chat
  // shows it: the room events it hides left out of both.
  [[nodiscard]] static view view_of(const conversation& one, bool is_chosen, bool is_muted, std::string draft = {},
                                    const room_event_filter& events = {}) {
    const message* last = newest(one, events);
    return {display_name(one), last ? std::optional<message>(*last) : std::nullopt, one.unread_here(events), is_chosen,
            is_muted, std::move(draft)};
  }
  view shown;

  conversation_row(Actions* a, const conversation& one, bool is_chosen, bool is_muted, std::string draft = {},
                   const room_event_filter& events = {})
      : actions(a), id(one.id), chosen(is_chosen), muted(is_muted), shown(view_of(one, is_chosen, is_muted, draft, events)),
        parts{.face = avatar_mark(one.id.id, display_name(one), 46.0f),
              .lines = lines_column(display_name(one), one.unread_here(events), is_chosen, is_muted)} {
    // Drawn once, played back as the list repaints around it.
    fState.setRecorded(true);
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
    fState.apply({.fillX = true, .height = kHeight, .padding = {0.0f, 12.0f, 0.0f, 10.0f}, .hoverBackground = chosen_colour, .selectedBackground = selected_colour, .focusBackground = chosen_colour, .selected = chosen});
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
      // A forum's: the topic it is in, then who.
      if (one.forum_topic)
        said_by(*one.forum_topic + " \u00b7 " + (last.outgoing ? std::string("You") : sender_name(one, last.sender)), accent_colour);
      else if (last.outgoing)
        said_by("You", accent_colour);
      else if (is_group(one))
        said_by(sender_name(one, last.sender), accent_colour);
      // Media: said as Telegram says it, not by the file's name its body is.
      if (auto carried = media_line(last)) {
        preview.setText(std::move(*carried));
        preview.setLinks({}, accent_colour);
      } else {
        preview.setText(std::move(shown.text));
        preview.setLinks(std::move(shown.links), accent_colour);
      }
    }
    // A draft left in it: said instead, as tdesktop says it, in red.
    if (!shown.draft.empty() && !is_chosen) {
      std::string text = shown.draft;
      std::ranges::replace(text, '\n', ' ');
      said_by("Draft", error_colour);
      preview.setText(std::move(text));
      preview.setLinks({}, accent_colour);
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
