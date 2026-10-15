// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:chat_list -- A chat in the list.
export module mux.ui:chat_list;

import std;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.compose;
import skiff.nodes.flow;
import skiff.nodes.text;
import skiff.widgets.pill;
import mux.core;
import mux.config;
import mux.ui.base;
import mux.ui.messages;

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
// row_views(state), made for a chat by make_row_view,
// found by ADL; none by default.
namespace row_view_defaults {
constexpr proto::sticker_view_list<> row_views(const auto&) {
  return {};
}
constexpr std::nullopt_t make_row_view(const auto&, const conversation&) {
  return std::nullopt;
}
}  // namespace row_view_defaults
template <class State>
constexpr auto row_views_for(const State& state) {
  using row_view_defaults::row_views;
  return row_views(state);
}

// The value compared by the list before keeping a row. The rendered tree
// belongs to this snapshot; a changed snapshot makes a new tree at its key.
struct conversation_view {
  std::string name;
  std::optional<message> last;
  std::int64_t unread = 0;
  bool chosen = false, muted = false;
  std::string draft;
  std::optional<invite_info> invite;
  std::optional<skia::SkColor> strip;
  std::vector<proto::part::badge> badges;
  std::optional<std::string> forum_topic;
  friend bool operator==(const conversation_view&, const conversation_view&) = default;
};
inline constexpr float kConversationRowHeight = 62.0f;
[[nodiscard]] inline float conversation_height(const conversation& one) {
  return one.forum_topic && !one.timeline.empty() ? kConversationRowHeight + 20.0f : kConversationRowHeight;
}
[[nodiscard]] inline conversation_view conversation_view_of(
    const ui_shared& shared, const conversation& one, bool chosen, bool muted,
    std::string draft = {}, const room_event_filter& events = {}, std::optional<skia::SkColor> strip = std::nullopt) {
  const message* last = newest(one, events);
  return {display_name(one), last ? std::optional<message>(*last) : std::nullopt,
          one.unread_here(events), chosen, muted, std::move(draft), one.invite, strip,
          proto::row_badges(protocol_state_of(shared, one.id.account), one), one.forum_topic};
}

// What the two preview lines say. Resolve the message, invite and draft
// precedence as data before making any nodes; there are no child setters
// for a holder to call as the conversation changes.
struct chat_preview {
  std::string time;
  std::optional<std::string> topic;
  std::string sender;
  bool draft = false;
  std::string text;
  std::vector<nodes::Text::Link> mentions;
};
[[nodiscard]] inline chat_preview preview_of(
    const conversation& one, const conversation_view& view, const room_event_filter& events) {
  chat_preview out;
  if (const message* latest = newest(one, events)) {
    const message last = with_actor(one, *latest);
    out.time = clock_of(last.at);
    auto text = [&] {
      if (last.body.html) {
        auto read = read_html(*last.body.html);
        return with_mentions(std::move(read.text), std::move(read.spans), one, nullptr);
      }
      return with_mentions(last.body.plain, link_spans_in(last.body.plain), one, nullptr);
    }();
    std::ranges::replace(text.text, '\n', ' ');
    std::erase_if(text.links, [](const nodes::Text::Link& link) { return !link.pill; });
    if (one.forum_topic) out.topic = *one.forum_topic;
    if (one.forum_topic || last.outgoing || is_group(one))
      out.sender = (last.outgoing ? std::string("You") : sender_name(one, last.sender)) + ":";
    if (auto media = media_line(last)) out.text = std::move(*media);
    else {
      out.text = std::move(text.text);
      out.mentions = std::move(text.links);
    }
  }
  if (view.invite) {
    out.sender = "Invite:";
    out.text = std::format("from {}", view.invite->from_name.empty() ? view.invite->from : view.invite->from_name);
    out.mentions.clear();
  }
  if (!view.draft.empty() && !view.chosen) {
    out.sender = "Draft:";
    out.draft = true;
    out.text = view.draft;
    std::ranges::replace(out.text, '\n', ' ');
    out.mentions.clear();
  }
  return out;
}

// Protocol-owned row content keeps its own type, including in a Release
// build. The generic holder is a skiff primitive, not an app node subclass.
template <class List> struct row_view_nodes;
template <class... Views> struct row_view_nodes<proto::sticker_view_list<Views...>> {
  using type = type_list<Views...>;
};
template <class> struct protocol_row_nodes;
template <class... Tags> struct protocol_row_nodes<protocol_list<Tags...>> {
  using type = typename joined<type_list<>,
      typename row_view_nodes<decltype(row_views_for(::mux::state_of<Tags>{}))>::type...>::type;
};
using protocol_row_view = typename variant_of_types<
    typename joined<type_list<nodes::Text>, typename protocol_row_nodes<protocols>::type>::type>::type;
using protocol_row_holder = skiff::compose::Holding<protocol_row_view>;
[[nodiscard]] inline std::optional<protocol_row_holder> row_holder_of(std::nullopt_t) {
  return std::nullopt;
}
template <class V>
[[nodiscard]] inline std::optional<protocol_row_holder> row_holder_of(std::optional<V> made) {
  if (!made) return std::nullopt;
  return protocol_row_holder(skiff::compose::vbox(0.0f, {.autoSize = scene::axes::kBoth}),
                             protocol_row_view(std::move(*made)));
}
[[nodiscard]] inline std::optional<protocol_row_holder> protocol_row_of(
    const ui_shared& shared, const conversation& one) {
  return spl::visit([&](const auto& state) -> std::optional<protocol_row_holder> {
    using row_view_defaults::make_row_view;
    return row_holder_of(make_row_view(state, one));
  }, protocol_state_of(shared, one.id.account));
}

// These are in-flow pills. The floating count_badge control has different
// geometry; all pill drawing and layout remains the widget's.
inline auto chat_unread(const palette& colours, const conversation_view& view) {
  return skiff::compose::visible(view.unread > 0,
      skiff::compose::styled({.margin = {0.0f, 0.0f, 0.0f, 4.0f}},
          widgets::Pill(std::to_string(view.unread),
              {.plate = view.chosen ? colours.selected_text : view.muted ? colours.dim : colours.accent,
               .text = view.chosen ? colours.selected : colours.on_accent,
               .size = 12.0f, .height = 21.0f, .padX = 7.0f, .bold = true})));
}
inline auto chat_protocol_mark(const palette& colours, const proto::part::badge& mark) {
  return skiff::compose::styled({.alignSelf = scene::align::kMiddle},
      widgets::Pill(mark.text, {.plate = tone_colour(colours, mark.tone), .text = colours.on_accent,
                                .size = 11.0f, .height = 19.0f, .padX = 6.0f, .bold = true}));
}
// Configure the rich-text leaf when it is made, from the resolved preview.
// It retains only avatar mention pills; other links do not open from a row.
inline auto chat_preview_text(const palette& colours, const conversation_view& view, const chat_preview& preview) {
  auto text = nodes::BasicText<message_pictures>(preview.text, 13.0f, view.chosen ? colours.selected_text : colours.dim);
  text.setLinks(preview.mentions, colours.accent);
  return skiff::compose::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle}, elided(std::move(text)));
}
inline auto chat_preview_line(const ui_needs& needs, const conversation& one,
                              const conversation_view& view, const chat_preview& preview) {
  namespace c = skiff::compose;
  const palette& colours = *needs.colours;
  return c::row(c::hbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      c::visible(!preview.sender.empty(), c::styled({.alignSelf = scene::align::kMiddle},
          elided(nodes::Text(preview.sender, 13.0f,
              view.chosen ? colours.selected_text : preview.draft ? colours.error : colours.accent)))),
      chat_preview_text(colours, view, preview),
      std::ranges::to<std::vector>(std::views::transform(view.badges, [&](const auto& mark) { return chat_protocol_mark(colours, mark); })),
      protocol_row_of(*needs.shared, one), chat_unread(colours, view));
}

// A chat row is a function of the list's model snapshot. Choosing it sends
// a request; its list-item label and selection are part of the same tree.
inline auto conversation_row(const ui_needs& needs, const conversation& one,
                             const conversation_view& view, const room_event_filter& events) {
  namespace c = skiff::compose;
  const palette& colours = *needs.colours;
  const chat_preview preview = preview_of(one, view, events);
  return c::recorded(c::with_cursor(scene::cursor::arrow{},
      c::said(scene::SemanticRole{scene::semantic_role::list_item{}}, view.chosen,
          c::onClick(request::choose{one.id},
              c::row(c::hbox(12.0f, {.fillX = true,
                  .height = conversation_height(one),
                  .padding = {0.0f, 12.0f, 0.0f, 10.0f}, .hoverBackground = colours.chosen,
                  .selectedBackground = colours.selected, .focusBackground = colours.chosen, .selected = view.chosen}),
                  avatar_mark(avatar_of::chat{}, one.id.id, view.name, 46.0f),
                  c::column(c::vbox(6.0f, {.autoSize = scene::axes::kY, .grow = scene::axes::kX,
                                           .alignSelf = scene::align::kMiddle}),
                      name_time_line(view.name, preview.time, view.chosen ? colours.selected_text : colours.text,
                                     view.chosen ? colours.selected_text : colours.dim, 13.0f),
                      c::visible(preview.topic.has_value(), c::styled({.fillX = true},
                          elided(nodes::Text(preview.topic.value_or(""), 13.0f,
                                            view.chosen ? colours.selected_text : colours.text)))),
                      chat_preview_line(needs, one, view, preview)),
                  c::visible(view.strip.has_value(), c::styled(
                      {.place = scene::anchor::kCentreLeft, .x = -8.0f, .width = 4.0f, .height = 38.0f, .cornerRadius = 2.0f},
                      nodes::Box<>(view.strip.value_or(0u))))), view.name))));
}
using conversation_row_t = decltype(conversation_row(std::declval<const ui_needs&>(),
    std::declval<const conversation&>(), std::declval<const conversation_view&>(), std::declval<const room_event_filter&>()));

struct listed_chat {
  conversation_id id;
  conversation_view view;
  room_event_filter events;
  std::optional<skia::SkColor> strip;
  float height = kConversationRowHeight;
};
// Forums supply a synthetic conversation; other rows use the model's own.
struct chat_listing {
  std::vector<listed_chat> chats;
  std::map<conversation_id, conversation> forums;
  [[nodiscard]] const conversation* find(const model& now, const conversation_id& id) const {
    if (const auto made = forums.find(id); made != forums.end()) return &made->second;
    return now.find(id);
  }
};
using chat_item = std::tuple<conversation_id, conversation_view, const conversation*, const listed_chat*, const ui_needs*>;
inline constexpr auto make_chat_row = [](const chat_item& item) {
  const auto& [id, view, one, listed, needs] = item;
  return conversation_row(*needs, *one, view, listed->events);
};
using chat_rows = nodes::MemoRows<conversation_id, conversation_view, conversation_row_t, std::remove_cv_t<decltype(make_chat_row)>>;
inline auto make_chat_rows() {
  return skiff::compose::styled({.fillX = true, .autoSize = scene::axes::kY}, chat_rows(make_chat_row));
}

}  // namespace mux::ui
