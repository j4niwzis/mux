// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:message -- A message: its bubble, its pictures, files, reactions and mentions.
export module mux.ui.messages:message;

import std;
import chevron.escape;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.compose;
import skiff.nodes.box;
import skiff.nodes.flow;
import skiff.nodes.icon;
import skiff.nodes.image;
import skiff.nodes.text;
import skiff.widgets.loader;
import skiff.widgets.pill;
import mux.platform.audio;
import mux.platform.video;
import mux.core;
import mux.config;
import mux.logic.links;
import mux.protocols;
import mux.plugins.types;
import mux.ui.base;
import mux.plugins.ui;
import mux.ui.proto;

export import :message_pieces;
export import :message_media;
export import :message_text;

export namespace mux::ui {

[[nodiscard]] inline skia::SkColor quote_tint(skia::SkColor colour, float alpha) {
  return (colour & 0x00FFFFFFu) | (static_cast<skia::SkColor>(std::lround(alpha * 255.0f)) << 24);
}
[[nodiscard]] inline auto quote_row(const palette& colours, skia::SkColor colour,
                                    std::string who, std::string said,
                                    std::optional<std::string> picture = std::nullopt,
                                    bool quoted = false, bool sticker = false) {
  namespace c = skiff::compose;
  std::optional<nodes::Text> tag;
  if (quoted)
    tag.emplace(c::styled({.alignSelf = scene::align::kStart,
                           .margin = {1.0f, 0.0f, 0.0f, 0.0f}},
                          nodes::Text("quoted", 11.0f, colours.dim)));
  std::optional<nodes::Image<from_thumbnails>> thumb;
  if (picture)
    thumb.emplace(c::styled({.width = 32.0f, .height = 32.0f, .alignSelf = scene::align::kMiddle,
                              .margin = {2.0f, 0.0f, 2.0f, 0.0f}, .cornerRadius = 3.0f,
                              .background = colours.tile},
                             nodes::Image<from_thumbnails>({*picture, sticker})));
  auto text = nodes::Text(std::move(said), 13.0f, colours.text);
  if (quoted)
    text = wrapped(std::move(text));
  else
    text = elided(std::move(text));
  return c::row(c::hbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY,
      .margin = {2.0f, 0.0f, 4.0f, 0.0f},
      .padding = {2.0f, 6.0f, 2.0f, picture ? 7.0f : 11.0f}, .cornerRadius = 5.0f,
      .background = quote_tint(colour, 0.12f), .masking = true}),
    c::styled({.place = scene::anchor::kTopLeft, .x = picture ? -7.0f : -11.0f,
                .y = -2.0f, .fillY = true, .width = 3.0f}, nodes::Box<>(quote_tint(colour, 0.9f))),
    std::move(thumb),
    c::column(c::vbox(0.0f, {.autoSize = scene::axes::kY, .grow = scene::axes::kX,
                             .alignSelf = scene::align::kMiddle}),
      c::row(c::hbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}),
        c::styled({.grow = scene::axes::kX}, elided(nodes::Text(std::move(who), 13.0f, colour, true))),
        std::move(tag)),
      c::styled({.fillX = true}, std::move(text))));
}
using quote_row_t = decltype(quote_row(std::declval<const palette&>(), skia::SkColor{}, std::string{}, std::string{}));

// A protocol's own sticker nodes, and its making of one for a message:
// none by default -- the picture.
namespace sticker_defaults {
constexpr proto::sticker_view_list<> sticker_views(const auto&) {
  return {};
}
constexpr std::nullopt_t make_sticker(const auto&, const message&) {
  return std::nullopt;
}
}  // namespace sticker_defaults
template <class State>
constexpr auto sticker_views_for(const State& state) {
  using sticker_defaults::sticker_views;
  return sticker_views(state);
}
// And the nodes a protocol shows of a message of its own, under its text
// (Telegram's inline buttons, a poll): listed by message_views(state,
// type_tag), made for a message by make_message_view -- by its
// part (message::theirs), say. None by default.
namespace view_defaults {
constexpr proto::sticker_view_list<> message_views(const auto&) {
  return {};
}
constexpr std::nullopt_t make_message_view(const auto&, const message&) {
  return std::nullopt;
}
}  // namespace view_defaults
template <class State>
constexpr auto message_views_for(const State& state) {
  using view_defaults::message_views;
  return message_views(state);
}

// The sender's name over their run, in their colour, and after it their
// role in the room, dim, as Telegram shows "admin": the name cut where it
// passes `widest`, the role's room kept.
inline auto sender_line(const palette& colours, std::string who, skia::SkColor colour, std::string role, float widest) {
  auto name = elided(nodes::Text(std::move(who), 13.0f, colour, true));
  name.setMaxWidth(widest);
  const bool has_role = !role.empty();
  return skiff::compose::row(skiff::compose::hbox(10.0f, {.autoSize = scene::axes::kBoth}), std::move(name),
                             skiff::compose::visible(has_role, skiff::compose::styled({.alignSelf = scene::align::kEnd},
                                                                                      nodes::Text(std::move(role), 12.0f, colours.dim))));
}
// tdesktop's bar over the first unread: its words in the middle of a band
// the width of the chat, over the row.
inline constexpr float kUnreadBarHeight = 26.0f;
inline auto unread_bar(const palette& colours) {
  return skiff::compose::column(
      skiff::compose::justified(skiff::compose::vbox(0.0f, {.place = scene::anchor::kTopLeft, .y = -(kUnreadBarHeight + 4.0f), .fillX = true,
                                                            .height = kUnreadBarHeight, .background = colours.sidebar}),
                                nodes::justify::middle{}),
      skiff::compose::styled({.alignSelf = scene::align::kMiddle}, nodes::Text("Unread messages", 13.0f, colours.dim, true)));
}

// ---- a message's row ------------------------------------------------------------
// A message as the chat shows it: a function of the facts it is made from
// (bubble_facts), kept by the list while they are the same. Every part that
// is pressed answers its own press; what moves it -- a swipe, a flash, its
// coming in -- is a skiff behaviour, read from the model where the program
// says it.

// tdesktop's msgPadding, margins(11, 8, 11, 8); its time sits lower than
// the text by msgDateDelta, point(2, 5): 5 over the bubble's bottom.
inline constexpr float kBubblePadX = 11.0f;
inline constexpr float kBubblePadY = 8.0f;
inline constexpr float kTimeLower = kBubblePadY - 5.0f;
inline constexpr float kBubbleAvatar = 34.0f;
inline constexpr float kBubbleMaxWidth = 480.0f;
// What it answers, as tdesktop's reply (history_view_reply.cpp): the line
// counted up to maxSignatureSize.
inline constexpr float kReplyLineMax = 240.0f;
// The list's own room at its left and right, around every row.
inline constexpr float kListSide = 12.0f;
inline constexpr float kReadersHeight = 16.0f;
inline constexpr float kSwipeToReply = 70.0f;
inline constexpr float kBesideGap = 8.0f;

// ---- the protocols' own sticker and message nodes -------------------------------
template <class List>
struct view_nodes;
template <class... Vs>
struct view_nodes<proto::sticker_view_list<Vs...>> {
  using type = type_list<Vs...>;
};
template <class>
struct protocol_sticker_nodes;
template <class... Tags>
struct protocol_sticker_nodes<protocol_list<Tags...>> {
  using type = typename joined<type_list<>, typename view_nodes<decltype(sticker_views_for(::mux::state_of<Tags>{}))>::type...>::type;
};
// Never empty: a text, where no protocol draws its own.
using their_sticker_t =
    typename variant_of_types<typename joined<type_list<nodes::Text>, typename protocol_sticker_nodes<protocols>::type>::type>::type;
template <class>
struct protocol_message_nodes;
template <class... Tags>
struct protocol_message_nodes<protocol_list<Tags...>> {
  using type = typename joined<type_list<>, typename view_nodes<decltype(message_views_for(::mux::state_of<Tags>{}))>::type...>::type;
};
using their_view_t =
    typename variant_of_types<typename joined<type_list<nodes::Text>, typename protocol_message_nodes<protocols>::type>::type>::type;
// As nodes: the protocol's own in them, as tall and as wide as they are.
using view_holder = skiff::compose::Holding<their_view_t>;
using sticker_holder = skiff::compose::Holding<their_sticker_t>;

// What plugins put after the text, at the bubble's foot
// (contract::message_trailers) -- the reactions -- in their order, each in a
// slot of its own: empty where it has nothing to show of the message.
template <class List>
struct plugin_trailers;
template <class... Features>
struct plugin_trailers<plugin_list<Features...>> {
  template <class Feature>
  static auto made(const trailer_facts& facts) -> decltype(extend(contract::message_trailers{}, type_tag<Feature>{}, facts)) {
    static_assert(requires { extend(contract::message_trailers{}, type_tag<Feature>{}, facts); },
                  composition::broken<contract::message_trailers, Feature>());
    return extend(contract::message_trailers{}, type_tag<Feature>{}, facts);
  }
  using nodes_t = std::tuple<std::optional<composition::node_t<decltype(made<Features>(std::declval<const trailer_facts&>()))>>...>;
  static nodes_t built(const trailer_facts& facts) {
    nodes_t slots;
    [&]<std::size_t... At>(std::index_sequence<At...>) {
      (composition::put(std::get<At>(slots), made<Features>(facts)), ...);
    }(std::index_sequence_for<Features...>{});
    return slots;
  }
  // The last shown, where there is one: what the time goes beside, and how
  // far its last line goes, as its plugin says (trailing_width).
  struct end {
    skia::SkRect bounds;
    float width = 0.0f;
  };
  static std::optional<end> last(const nodes_t& slots) {
    std::optional<end> found;
    [&]<std::size_t... At>(std::index_sequence<At...>) {
      ((std::get<At>(slots) && std::get<At>(slots)->visible()
            ? void(found = end{std::get<At>(slots)->bounds(),
                               trailing_width(contract::message_trailers{}, type_tag<Features>{}, *std::get<At>(slots))})
            : void()),
       ...);
    }(std::index_sequence_for<Features...>{});
    return found;
  }
};
using trailers_t = plugin_trailers<composition::providers<contract::message_trailers, enabled_plugins>>;

// The facts a message's row is made from: while they are the same, the
// row is kept as it is -- with a selection in its text.
struct bubble_facts {
  message said;
  // Where it is in its sender's run: the first has the name, the last the
  // avatar.
  bool first = false, last = false;
  // Whether room events are shown, the chat shows link previews, its link's
  // preview has come, the message it answers was there to quote, it is the
  // chat's first unread.
  bool events_shown = true, previews_shown = true, preview_known = false, quote_known = true, unread_start = false;
  // What the message it answers says now: a row quoting it made again as
  // it changes -- edited, deleted.
  std::optional<decltype(message::body)> quote_said;
  // Who has read up to it, where the chat shows it.
  std::vector<std::string> readers;
  // The inherited blur its panes were made with.
  double frost = 10.0;
  // How many of the rooms it names have come -- a picture, or found to be
  // there -- since it was made: made again as one does.
  std::size_t rooms_come = 0;
  // The chat's looks it was made with, by their number: made again as
  // they change.
  std::uint64_t looks = 0;
  friend bool operator==(const bubble_facts&, const bubble_facts&) = default;
};

// What a row is handed down.
struct bubble_needs {
  const palette* colours = nullptr;
  looks_shown* looks = nullptr;
  ui_shared* shared = nullptr;
  // The program's model: rooms found, previews fetched.
  const model* now = nullptr;
};

// What the program marks on messages: those selected (and, while a range
// is being dragged over, those it would make so), and the one a jump went
// to -- flashed, newer each time, and the stretch of its text a quote of it
// pointed at, marked until the flash is over. The program's, in the shown
// model; rows read it.
struct message_marks {
  std::set<std::string> selected;
  std::optional<std::set<std::string>> previewed;
  std::optional<std::string> flashed;
  std::optional<std::string> fragment;
  std::uint64_t flash = 0;
  friend bool operator==(const message_marks&, const message_marks&) = default;
};

// The rooms a message's text names whose picture is still to come, and
// those not known to be there: its row made again when one comes.
struct named_rooms {
  std::vector<std::string> waiting, unknown;
  [[nodiscard]] std::size_t come(const model& now) const {
    return static_cast<std::size_t>(std::ranges::count_if(waiting, [](const std::string& key) { return avatar_images().has(key); }) +
                                    std::ranges::count_if(unknown, [&](const std::string& key) { return now.rooms_found.contains(key); }));
  }
};

// A message's text as it is shown: its HTML read, its mentions as pills --
// a reaction shown as a line, its emoji's.
[[nodiscard]] inline mentioned shown_text_of(const conversation& in, const message& said, const model* now) {
  mux::body readable = said.body;
  if (!said.reaction_key.empty() && said.service) {
    std::optional<std::string> label;
    if (said.replies_to)
      if (const auto* parent = held_message(in, *said.replies_to))
        if (const auto event = std::ranges::find(parent->reaction_events, said.id, &message::reaction_event::event);
            event != parent->reaction_events.end())
          label = event->shortcode;
    auto reaction = reaction_body_of(said.reaction_key, in.emotes, label);
    if (reaction.html) {
      readable.plain = sender_name(in, said.sender) + " reacted " + reaction.plain;
      readable.html = std::format(R"(<a href="https://matrix.to/#/{}">{}</a> reacted )", chevron::escaped(said.sender),
                                  chevron::escaped(sender_name(in, said.sender))) +
                      *reaction.html;
    }
  }
  if (readable.html) {
    auto read = read_html(*readable.html);
    return with_mentions(std::move(read.text), std::move(read.spans), in, now, std::move(read.styles), std::move(read.disclosures));
  }
  return with_mentions(readable.plain, link_spans_in(readable.plain), in, now);
}
[[nodiscard]] inline named_rooms rooms_named_in(const conversation& in, const message& said, const model* now) {
  mentioned shown = shown_text_of(in, with_actor(in, said), now);
  return {std::move(shown.waiting), std::move(shown.unknown)};
}

// The body's parts, by where they are in it.
namespace bubble_part {
inline constexpr std::size_t frost = 0, name = 1, forwarded = 2, quote = 3, picture = 4, their_sticker = 5, album = 6, file = 7,
                             text = 8, blocks = 9, preview = 10, trailers = 11, thread = 12, lines = 13, their_view = 14, time = 15,
                             inline_time = 16;
}
template <std::size_t Part> [[nodiscard]] auto& part_of(auto& body) { return std::get<Part>(body.fParts); }

// The bubble's own rules, as tdesktop lays one out: the time beside the
// text's last line wherever the two fit at the bubble's widest -- the bubble
// widened to them where it is narrower -- and on a line of its own only
// where they do not; a sticker's name, forward and quote beside it, to its
// right where it came in and to its left where it was sent, where the row
// has room for both (history_view_media_unwrapped.cpp's drawSurrounding).
struct bubble_rules {
  struct State {
    skia::SkColor plate = 0;
    bool sticker = false;
    bool beside = false, beside_left = false;
    float beside_room = 0.0f;
    // Its least width as the message asks it (a quote's), and as the time
    // beside the last line asks it: the bubble widened to hold both.
    float base_min = 0.0f, widened = 0.0f;
    // Where the time goes has been decided, from a layout; and what that
    // was decided from, so that it is measured again only where that moved.
    bool time_placed = false;
    skia::SkRect placed_beside = skia::SkRect::MakeEmpty();
    float placed_width = -1.0f, placed_most = 0.0f;
    std::string placed_time;
  };
  static bool beside_now(auto& body, const State& state) {
    auto& picture = part_of<bubble_part::picture>(body);
    if (!state.beside || !picture)
      return false;
    float widest = 0.0f;
    const auto widest_of = [&](auto& part) {
      if (part && part->visible())
        widest = std::max(widest, part->bounds().isEmpty() ? 120.0f : part->bounds().width());
    };
    widest_of(part_of<bubble_part::name>(body));
    widest_of(part_of<bubble_part::forwarded>(body));
    widest_of(part_of<bubble_part::quote>(body));
    const float sticker = std::max(picture->bounds().width(), picture->fState.fWidth);
    // countCurrentSize: beside where the sticker and the least of what goes
    // beside it fit -- msgReplyPadding.left() + msgMinWidth / 2.
    constexpr float kLeastBeside = 10.0f + 190.0f / 2.0f;
    return sticker + kBesideGap + std::min(widest, kLeastBeside) <= body.fState.fLastConstraint.width();
  }
  static void prepare(auto& body, State& state) {
    const bool side = beside_now(body, state);
    if (auto& picture = part_of<bubble_part::picture>(body); side && picture)
      state.beside_room = std::max(0.0f, body.fState.fLastConstraint.width() -
                                             std::max(picture->bounds().width(), picture->fState.fWidth) - kBesideGap);
    const auto flow = [&](auto& part) {
      if (!part)
        return;
      part->fState.setOutOfFlow(side);
      if (!side)
        part->fState.shiftTo(0.0f, 0.0f);
    };
    flow(part_of<bubble_part::name>(body));
    flow(part_of<bubble_part::forwarded>(body));
    flow(part_of<bubble_part::quote>(body));
    // Beside it, as wide as what is left at the most, as tdesktop narrows its
    // reply to the surrounding width; over it, as the bubble lets.
    if (auto& forwarded = part_of<bubble_part::forwarded>(body))
      forwarded->apply({.maxWidth = side ? state.beside_room : 0.0f});
    if (auto& quote = part_of<bubble_part::quote>(body))
      quote->apply({.maxWidth = side ? state.beside_room : 0.0f});
  }
  static void arrange(auto& body, State& state) {
    auto& text = part_of<bubble_part::text>(body);
    auto& inline_time = part_of<bubble_part::inline_time>(body);
    // The column can wrap again in its final box: its overlay put where its
    // last line ended, after that layout.
    const auto trailer = trailers_t::last(part_of<bubble_part::trailers>(body));
    if (inline_time.visible()) {
      const auto last = trailer ? trailer->bounds : text.bounds();
      if (!last.isEmpty()) {
        inline_time.apply({.y = last.fBottom - body.fState.contentBox().fBottom + kTimeLower});
        scene::layout(inline_time, body.fState.contentBox());
      }
    }
    if (state.time_placed && text.visible() && part_of<bubble_part::blocks>(body).empty() && !part_of<bubble_part::preview>(body) &&
        !state.beside) {
      const auto last = trailer ? trailer->bounds : text.bounds();
      if (last != state.placed_beside) {
        state.time_placed = false;
        body.markDamaged();
      }
    }
    if (!beside_now(body, state))
      return;
    const skia::SkRect sticker = part_of<bubble_part::picture>(body)->bounds();
    auto& forwarded = part_of<bubble_part::forwarded>(body);
    auto& quote = part_of<bubble_part::quote>(body);
    // The forward and the reply on one plate, as tdesktop's one service
    // rect: the forward's corners round at its top, the reply's at its
    // bottom, nothing between them.
    const bool both = forwarded && forwarded->visible() && quote && quote->visible();
    if (forwarded)
      forwarded->apply({.corners = both ? scene::Corners{8.0f, 8.0f, 0.0f, 0.0f} : scene::Corners{8.0f, 8.0f, 8.0f, 8.0f}});
    if (quote)
      quote->apply({.corners = both ? scene::Corners{0.0f, 0.0f, 8.0f, 8.0f} : scene::Corners{8.0f, 8.0f, 8.0f, 8.0f}});
    float y = 0.0f;
    const auto put = [&](auto& part, float after) {
      if (!part || !part->visible())
        return;
      const skia::SkRect at = part->bounds();
      const float x = state.beside_left ? sticker.fLeft - kBesideGap - at.width() : sticker.fRight + kBesideGap;
      part->fState.shiftTo(x - at.fLeft, sticker.fTop + y - at.fTop);
      y += at.height() + after;
    };
    put(part_of<bubble_part::name>(body), 4.0f);
    put(forwarded, both ? 0.0f : 4.0f);
    put(quote, 4.0f);
  }
  // Frames asked for only once it has been laid out, and until the time is
  // placed: a bubble made but not laid out yet -- out of view, of those made
  // around it -- asked for frames forever.
  static bool settling(const auto& body, const State& state) { return !state.time_placed && !body.fState.fBounds.isEmpty(); }
  static void settle(auto& body, State& state) {
    auto& text = part_of<bubble_part::text>(body);
    auto& time = part_of<bubble_part::time>(body);
    auto& inline_time = part_of<bubble_part::inline_time>(body);
    const auto trailer = trailers_t::last(part_of<bubble_part::trailers>(body));
    // A sticker's time is over it, and nowhere else: placed beside its
    // reactions too, it was shown twice.
    if (part_of<bubble_part::picture>(body) && state.sticker) {
      state.time_placed = true;
      if (inline_time.visible() || (state.beside_left && time.visible())) {
        if (state.beside_left)
          time.setVisible(false);
        inline_time.setVisible(false);
        body.invalidateLayout();
      }
      return;
    }
    // Nothing left of the text -- all of it the quote the header shows -- or
    // a text that ends in a quote, and nothing under it: the time on a line
    // of its own, as Telegram's.
    const std::string& words = text.text();
    const bool ends_quoted = !words.empty() && std::ranges::any_of(text.styles(), [&](const auto& one) {
      return one.quote && one.first < words.size() && words.size() <= one.last;
    });
    if (text.visible() && (words.empty() || ends_quoted) && !trailer) {
      state.time_placed = true;
      if (words.empty())
        text.setVisible(false);
      if (!time.visible() || inline_time.visible()) {
        time.setVisible(true);
        inline_time.setVisible(false);
        state.widened = 0.0f;
        body.fState.apply({.minWidth = state.base_min});
        body.invalidateLayout();
      }
      return;
    }
    if (!part_of<bubble_part::blocks>(body).empty() || part_of<bubble_part::preview>(body) || (!text.visible() && !trailer)) {
      state.time_placed = true;  // under it, as it is
      return;
    }
    skia::SkFont* font = skiff::paint::defaultFont();
    if (font == nullptr)
      return;
    // What the time goes beside: the last trailer where there is one -- the
    // reactions, as Telegram puts it on their line -- else the text's last
    // line.
    const skia::SkRect last = trailer ? trailer->bounds : text.bounds();
    if (last.isEmpty())
      return;
    state.time_placed = true;
    const float last_width = trailer ? trailer->width : text.lastLineWidth();
    // The widest the text can be here: the bubble's widest, or what the row
    // gives it where that is less -- a phone's window.
    const float given = body.fState.fLastConstraint.width() - 2.0f * kBubblePadX;
    const float most = given > 0.0f ? std::min(kBubbleMaxWidth, given) : kBubbleMaxWidth;
    if (last == state.placed_beside && last_width == state.placed_width && inline_time.text() == state.placed_time &&
        most == state.placed_most)
      return;
    state.placed_beside = last;
    state.placed_width = last_width;
    state.placed_time = inline_time.text();
    state.placed_most = most;
    const float needs = last_width + skiff::paint::Painter(nullptr, *font).measure(inline_time.text(), 11.0f) + 10.0f;
    const bool inside = needs <= most;
    const float widest = inside ? std::ceil(needs) + 2.0f * kBubblePadX : 0.0f;
    if (inside == time.visible() || widest != state.widened) {
      time.setVisible(!inside);
      inline_time.setVisible(inside);
      state.widened = widest;
      body.fState.apply({.minWidth = std::max(state.base_min, widest)});
      state.time_placed = false;
      body.invalidateLayout();
    }
  }
};

// The bubble's colour as its chat's look has it: solid, or at its opacity --
// over what is behind it, frosted where it is so.
[[nodiscard]] inline skia::SkColor plate_of(const palette& colours, const config::bubble_look& look, bool mine) {
  const skia::SkColor solid = mine ? colours.out_bubble : colours.bubble;
  return spl::visit(spl::overloaded{[&](config::bubbles::solid) { return solid; }, [&](const auto&) { return at_opacity(solid, look.opacity); }},
                    look.kind);
}
// Words of a message, as its text and its pieces show them: wrapped,
// selectable, its links and quotes in their colours.
inline auto message_words(const palette& colours, const text_piece& words, skia::SkColor colour, skia::SkColor quote) {
  auto text = wrapped(nodes::BasicText<message_pictures>(words.text, 13.0f, colour));
  text.setSelectable(true);
  text.setSelectionColour((colours.accent & 0x00FFFFFFu) | (110u << 24));  // the accent, see-through
  text.setLinks(words.links, colours.accent);
  text.setStyles(words.styles, quote);
  text.setShrinksToLines(true);
  return text;
}
// The stretch of a text a quote of it pointed at, marked while its message
// is flashed: over the text's own styles, kept as it was made.
struct marks_fragment {
  std::vector<nodes::Text::Styled> styles;
  skia::SkColor quote;
  void operator()(nodes::BasicText<message_pictures>& text, const std::optional<std::string>& fragment) const {
    auto now = styles;
    if (fragment && !fragment->empty())
      if (const auto at = std::string_view(text.text()).find(*fragment); at != std::string_view::npos)
        now.push_back({.first = at, .last = at + fragment->size(), .marked = true});
    text.setStyles(std::move(now), quote);
  }
};
// A press on a quoted stretch of a reply's text -- the part of the message
// it answers, as "> " quotes it: to that message, the part marked. Nothing
// where it is no quote, or a selection is being made.
struct quote_pressed {
  std::string message_id;
  std::optional<std::string> replies_to;
  std::optional<request::jump_to_message> operator()(auto& text, float x, float y) const {
    if (!replies_to || text.hasSelection())
      return std::nullopt;
    const auto quote = text.quoteAt(x, y);
    if (!quote)
      return std::nullopt;
    return request::jump_to_message{
        *replies_to, trimmed_fragment(std::string_view(text.text()).substr(quote->first, quote->second - quote->first)), message_id};
  }
};
// A forward's line pressed off its sender's pill: the original, where it
// links to it.
struct forward_link {
  using Answer = std::optional<request::open_url>;
  std::string link;
  Answer operator()() const { return link.empty() ? Answer() : Answer(request::open_url{link}); }
};
// The face beside a run's last bubble: their page, where it is shown.
struct open_face {
  using Answer = std::optional<request::open_member_info>;
  std::string sender;
  bool shown = false;
  Answer operator()() const { return shown ? Answer(request::open_member_info{sender}) : std::nullopt; }
};
// The arrow a swipe shows, filling as it reaches its mark.
struct swipe_shown {
  const palette* colours;
  void operator()(nodes::Icon& mark, float reached) const {
    mark.apply({.background = reached >= 1.0f ? colours->accent : colours->tile, .alpha = reached});
    mark.setColour(reached >= 1.0f ? colours->on_accent : colours->dim);
  }
};

// The bubble: a column of the name, the forward, the quote, the picture or
// file or album, the text and its pieces, the preview, the reactions, the
// thread's summary, the protocol's lines and node, the time.
inline auto message_body(const bubble_needs& n, const conversation& in, const bubble_facts& f, const model* now) {
  namespace c = skiff::compose;
  const palette& colours = *n.colours;
  const looks_shown& looks = *n.looks;
  const message said = with_actor(in, f.said);
  const bool mine = said.outgoing;
  const bool group = is_group(in);
  const bool as_lines = spl::visit(spl::overloaded{[](proto::part::style::lines) { return true; }, [](proto::part::style::bubbles) { return false; }},
                                   proto::message_style(protocol_state_of(*n.shared, in.id.account)));
  bubble_rules::State rules{.plate = plate_of(colours, looks.bubbles, mine)};
  scene::Spec spec{.autoSize = scene::axes::kBoth, .maxWidth = kBubbleMaxWidth + 2.0f * kBubblePadX,
                   .padding = {kBubblePadY, kBubblePadX, kBubblePadY, kBubblePadX}, .cornerRadius = 12.0f, .background = rules.plate};
  std::optional<scene::Border> border;
  spl::visit(spl::overloaded{[&](config::bubbles::glass) { border = scene::Border{skia::colorSetARGB(70, 255, 255, 255), 1.0f}; },
                             [](const auto&) {}},
             looks.bubbles.kind);
  std::optional<float> blur = frosts(looks.bubbles) ? std::optional<float>(blur_of(looks.bubbles, looks.window)) : std::nullopt;
  // Lines: full width, on the wallpaper, no plate.
  if (as_lines) {
    spec.fillX = true;
    spec.maxWidth = 0.0f;
    spec.cornerRadius = 0.0f;
    spec.background = skia::SkColor{0};
  }
  // Something done, not said: a line in the middle, on a plate of its own,
  // with no avatar and no name -- as tdesktop's service messages; frosted as
  // its own blur says.
  if (said.service) {
    spec.cornerRadius = 12.0f;
    spec.background = at_opacity(colours.tile, element_opacity_of(looks.bubbles, &config::element_opacity::service));
    blur = frosts(looks.bubbles) ? std::optional<float>(element_blur_of(looks.bubbles, &config::element_blur::service, looks.window))
                                 : std::nullopt;
  }

  // The sender's name over their run, in a group, and their role.
  std::optional<decltype(c::onClick(request::open_member_info{}, sender_line(colours, "", 0, "", 0.0f)))> name;
  if (((group && !mine) || as_lines) && f.first && !said.service)
    name.emplace(c::onClick(request::open_member_info{said.sender},
                            sender_line(colours, sender_name(in, said.sender), avatar_colour(said.sender),
                                        proto::sender_role(protocol_state_of(*n.shared, in.id.account), in, said.sender),
                                        kBubbleMaxWidth - 60.0f),
                            sender_name(in, said.sender)));
  // Forwarded: "Forwarded from" its sender, at its top, as Telegram's -- the
  // sender a person's pill, their page; the words, the original, where its
  // link is.
  std::optional<decltype(c::onPress(std::declval<forward_link>(), forward_line(colours, "", {}, 0, std::nullopt)))> forwarded;
  if (said.forwarded) {
    const std::string& who = said.forwarded->name.empty() ? said.forwarded->from : said.forwarded->name;
    std::vector<nodes::Text::Link> spans;
    const auto link = proto::person_link(state_before(protocol_of(said.forwarded->from)), said.forwarded->from);
    if (link && !who.empty())
      spans.push_back(nodes::Text::Link{0, who.size(), *link});
    mentioned shown = with_mentions(who, std::move(spans), in, now);
    forwarded.emplace(c::onPress(forward_link{said.forwarded->link},
                                 forward_line(colours, std::move(shown.text), std::move(shown.links), mine ? colours.sent_time : colours.accent,
                                              link ? std::optional<std::string>(said.forwarded->from) : std::nullopt),
                                 "Forwarded from " + who));
  }
  // Element's warnings, shortened to fit beside the time: not encrypted, in
  // an encrypted room (what came before it was encrypted is not marked);
  // encrypted by a device not verified by its owner; its authenticity not
  // guaranteed on this device.
  const bool plain_in_encrypted = in.encrypted && !said.encrypted && !said.service &&
                                  (said.came_plain || !in.encrypted_since || said.at >= *in.encrypted_since);
  const std::string warning = plain_in_encrypted                         ? std::string("not encrypted · ")
                              : said.encrypted && said.unverified        ? std::string("not verified by its owner · ")
                              : said.encrypted && said.unauthenticated  ? std::string("authenticity not guaranteed · ")
                                                                         : std::string();
  std::string when = warning + mark_of(said) + clock_of(said.at);
  when += spl::visit(spl::overloaded{[](const delivery::sending&) { return " · sending"; }, [](const delivery::failed&) { return " · not sent"; },
                                     [](const auto&) { return ""; }},
                     said.delivery);
  // A sticker its protocol draws itself (a Telegram TGS or WebM): its node,
  // not the picture.
  std::optional<sticker_holder> their_sticker;
  if (said.sticker)
    spl::visit(
        [&](const auto& state) {
          using sticker_defaults::make_sticker;
          auto made = make_sticker(state, said);
          if constexpr (!std::same_as<decltype(made), std::nullopt_t>)
            if (made)
              their_sticker.emplace(c::vbox(0.0f, {.autoSize = scene::axes::kBoth}), std::move(*made));
        },
        protocol_state_of(*n.shared, in.id.account));
  // What a picture of it opens, pressed: it in the viewer -- a video played
  // there, or, built without video, by the system's player.
  const std::string sender_shown = sender_name(in, said.sender);
  const std::string when_shown =
      std::format("{:%d.%m.%Y} at {}", std::chrono::year_month_day{std::chrono::floor<std::chrono::days>(said.at)}, clock_of(said.at));
  const auto opens = [&](const std::string& source, const std::optional<std::string>& video, const std::string& name_of) -> media_open {
    if (video && !mux::platform::video::kPlays)
      return request::open_file{*video, name_of};
    if (video)
      return request::open_video{source, *video, said.sender, sender_shown, when_shown};
    return request::open_picture{source, said.sender, sender_shown, when_shown};
  };
  std::optional<picture_view_t> picture;
  std::optional<file_view_t> file;
  bool text_shown = true, time_shown = true;
  scene::Spec time_spec{.alignSelf = scene::align::kEnd};
  if (said.attachment && !their_sticker) {
    const mux::attachment& carried = *said.attachment;
    const bool bare = said.body.plain.empty() && !said.body.html;
    rules.sticker = said.sticker && is_picture(carried.kind);
    // Its time over it: a sticker sent, at its bottom right corner
    // (tdesktop's UnwrappedMedia); a picture with no caption.
    const bool time_over = rules.sticker ? mine : bare;
    spl::visit(spl::overloaded{[&](attachment_kind::image) {
                                 picture.emplace(picture_view(
                                     colours, carried.source, carried.width, carried.height,
                                     {.sticker = rules.sticker,
                                      .video_ms = carried.video ? std::optional<std::int64_t>(carried.duration_ms) : std::nullopt,
                                      .thumbnail = !carried.video || carried.video != carried.source,
                                      .time = time_over ? when : std::string()},
                                     opens(carried.source, carried.video, carried.name)));
                               },
                               [&](attachment_kind::file) {
                                 const bool sound = audio_type(carried.mimetype, carried.name);
                                 file.emplace(file_view(colours, carried.source, carried.name, carried.size, sound,
                                                        sound ? file_open(request::play_audio{carried.source})
                                                              : file_open(request::open_file{carried.source, carried.name})));
                               }},
               carried.kind);
    if (rules.sticker && picture) {
      // A sticker: on nothing -- no bubble, no padding. One sent has its time
      // over it; one that came, beside it at the bottom, to its right, on a
      // plate of its own, shown while the pointer is over the message, as
      // tdesktop's needInfoDisplay (isUnderCursor).
      text_shown = false;
      if (mine)
        time_shown = false;
      else
        time_spec = {.place = scene::anchor::kBottomRight, .origin = scene::anchor::kBottomLeft, .x = 6.0f, .padding = {2.0f, 6.0f, 2.0f, 6.0f},
                     .cornerRadius = 8.0f, .background = rules.plate, .visible = false, .revealOnHover = true};
      blur.reset();
      border = scene::Border{skia::SkColor{0}, 0.0f};
      spec.padding = {0.0f, 0.0f, 0.0f, 0.0f};
      spec.background = skia::SkColor{0};
    } else if (bare) {
      // No caption: the text goes, and a picture has its time over it.
      text_shown = false;
      if (picture) {
        time_shown = false;
        spec.padding = {3.0f, 3.0f, 3.0f, 3.0f};
      }
    }
  }
  // A gallery: its pictures as an album, its body the caption under it, each
  // pressed opening it in the viewer.
  std::optional<album_view_t> album;
  if (!said.album.empty())
    album.emplace(album_view(colours, said.album,
                             said.album | std::views::transform([&](const attachment& one) { return opens(one.source, std::nullopt, one.name); }) |
                                 std::ranges::to<std::vector>()));
  // Pictures at the chat's look's opacity for them.
  if (const float images = static_cast<float>(element_opacity_of(looks.bubbles, &config::element_opacity::images)) / 100.0f; images < 1.0f) {
    if (picture)
      picture->apply({.alpha = images});
    if (album)
      album->apply({.alpha = images});
  }

  // Its text, as the message says it, its mentions as pills. A reply whose
  // text opens with its only quote, of the message it answers: the quote
  // shown in the reply's header, as the part answered, and not again in the
  // text -- only where that message says all of it.
  mentioned shown = shown_text_of(in, said, now);
  std::optional<std::string> header_quote;
  if (said.replies_to) {
    const message* answered = held_message(in, *said.replies_to);
    mentioned taken = shown;
    if (const auto quote = take_opening_quote(taken); quote && answered && squeezed(quote_line_of(*answered, in, now)).contains(squeezed(*quote))) {
      header_quote = quote;
      shown = std::move(taken);
    }
  }
  // The quote's colour: the accent on theirs; on one's own, the text's, as
  // tdesktop's outgoing blockquote -- not the accent on its accent.
  const skia::SkColor quote_colour = mine ? colours.text : colours.accent;
  // Words, code blocks and link cards in their own order.
  const std::vector<text_piece> pieces = pieces_of(shown.text, shown.links, shown.styles, shown.cards, shown.disclosures);
  auto words = c::styled({.maxWidth = kBubbleMaxWidth}, message_words(colours, pieces.front(), colours.text, quote_colour));
  words.setVisible(text_shown && !pieces.front().text.empty());
  std::vector<message_piece> blocks;
  for (std::size_t i = 1; i + 1 < pieces.size(); i += 2) {
    if (pieces[i].disclosure)
      blocks.emplace_back(std::in_place_type<disclosure_piece_t>, disclosure_piece(colours, shown, pieces[i + 1], now, quote_colour));
    else if (pieces[i].card)
      blocks.emplace_back(std::in_place_type<link_piece_t>, link_piece(colours, *pieces[i].card, pieces[i + 1], now, quote_colour));
    else
      blocks.emplace_back(std::in_place_type<code_piece_t>, code_piece(colours, pieces[i], pieces[i + 1], quote_colour, quote_colour, colours.text));
  }
  auto text = c::onClickAt(quote_pressed{said.id, said.replies_to},
                           c::applied<message_marks>([id = said.id](const message_marks& marks) {
                             return marks.flashed == id ? marks.fragment : std::nullopt;
                           },
                           marks_fragment{pieces.front().styles, quote_colour}, std::move(words)));
  // The first link's preview, where it has come.
  std::optional<page_preview_t> preview;
  if (const auto link = first_link_of(said); link && now && f.previews_shown)
    if (const auto found = now->previews.find(*link); found != now->previews.end())
      preview.emplace(page_preview(colours, found->second, *link));
  // What it answers, in the timeline, in a thread or fetched aside: pressed,
  // that message -- the part the header shows marked there.
  std::optional<decltype(c::onClick(request::jump_to_message{}, quote_row(colours, 0, "", "")))> quote;
  if (said.replies_to) {
    const message* found = held_message(in, *said.replies_to);
    const bool known = found != nullptr;
    // A picture's: its thumbnail, and its caption or "Photo"; a file's: its
    // name; else its text.
    std::optional<std::string> pictured;
    std::string line = header_quote ? *header_quote : known ? quote_line_of(*found, in, now) : std::string("not loaded");
    if (known && found->attachment) {
      if (is_picture(found->attachment->kind))
        pictured = found->attachment->source;
      if (line.empty())
        line = is_picture(found->attachment->kind) ? std::string("Photo") : found->attachment->name;
    }
    if (!header_quote)
      std::ranges::replace(line, '\n', ' ');
    const bool with_picture = pictured.has_value();
    const std::string from = known ? (found->outgoing ? std::string("You") : sender_name(in, found->sender)) : std::string("A message");
    // The bubble at least as wide as the quote asks -- its name, "quoted"
    // after it, and its line up to maxSignatureSize -- so that a short answer
    // to a long message does not stretch its bubble to the full width.
    if (skia::SkFont* font = skiff::paint::defaultFont()) {
      skiff::paint::Painter measure(nullptr, *font);
      const float who = measure.measure(from, 13.0f) + (header_quote ? 8.0f + std::ceil(measure.measure("quoted", 11.0f)) : 0.0f);
      const float words_wide = std::max(who, std::min(measure.measure(line, 13.0f), kReplyLineMax));
      const float around = (with_picture && !header_quote ? 7.0f + 32.0f + 4.0f : 11.0f) + 6.0f + 2.0f * kBubblePadX;
      rules.base_min = std::min(std::ceil(words_wide) + around, kBubbleMaxWidth + 2.0f * kBubblePadX);
    } else {
      rules.base_min = 160.0f;
    }
    spec.minWidth = rules.base_min;
    quote.emplace(c::onClick(request::jump_to_message{*said.replies_to, header_quote, said.id},
                             quote_row(colours, known ? avatar_colour(found->sender) : colours.accent, from, std::move(line),
                                       header_quote ? std::nullopt : std::move(pictured), header_quote.has_value(), known && found->sticker),
                             "Go to the message answered"));
  }
  // A sticker's: no bubble under it, so what is over it -- a forward's line,
  // the quote of what it answers -- each on a small plate of its own, as
  // Telegram's, beside it; no sender's name (tdesktop's drawSurrounding
  // draws a topic, a forward, via and the reply, never the name).
  if (rules.sticker && picture) {
    const auto plated = [&](scene::Node& part) {
      part.apply({.padding = {3.0f, 8.0f, 3.0f, 8.0f}, .cornerRadius = 8.0f, .background = rules.plate});
    };
    if (name)
      name->setVisible(false);
    if (forwarded)
      plated(*forwarded);
    if (quote)
      plated(*quote);
    rules.beside = true;
    rules.beside_left = mine;
    rules.base_min = 0.0f;
    spec.minWidth = 0.0f;
  }
  // A thread's root: how many answers, the latest, as Element shows it under
  // the message; pressed, the thread.
  std::optional<decltype(c::onClick(request::open_thread{}, nodes::Text("", 13.0f, 0)))> thread;
  if (said.threaded && said.threaded->count > 0) {
    const thread_summary& summary = *said.threaded;
    std::string line = std::format("\U0001F4AC {} {}", summary.count, summary.count == 1 ? "reply" : "replies");
    if (!summary.last_text.empty())
      line += std::format(" · {}: {}", sender_name(in, summary.last_sender), summary.last_text);
    std::ranges::replace(line, '\n', ' ');
    thread.emplace(c::onClick(request::open_thread{said.id},
                              c::styled({.fillX = true, .margin = {4.0f, 0.0f, 0.0f, 0.0f}}, elided(nodes::Text(std::move(line), 13.0f, colours.accent, true))),
                              "Open thread"));
  }
  // What its protocol says under it, and its node of its own.
  auto lines = proto::message_lines(protocol_state_of(*n.shared, in.id.account), in, said) |
               std::views::transform([&](const proto::part::line& one) {
                 return c::styled({.fillX = true, .margin = {4.0f, 0.0f, 0.0f, 0.0f}},
                                  wrapped(nodes::Text(one.text, 12.0f, tone_colour(colours, one.tone))));
               }) |
               std::ranges::to<std::vector>();
  std::optional<view_holder> their_view;
  spl::visit(
      [&](const auto& state) {
        using view_defaults::make_message_view;
        auto made = make_message_view(state, said);
        if constexpr (!std::same_as<decltype(made), std::nullopt_t>)
          if (made)
            their_view.emplace(std::move(*made));
      },
      protocol_state_of(*n.shared, in.id.account));
  // The last of a run: its bottom corner on the sender's side squared and a
  // tail grown from it, as Telegram draws one -- not for a line of what was
  // done, nor a picture with nothing under it.
  const bool bare_picture = said.attachment && said.body.plain.empty() && !said.body.html && picture;
  if (f.last && !said.service && !bare_picture && !as_lines) {
    spec.corners = mine ? scene::Corners{12.0f, 12.0f, 0.0f, 12.0f} : scene::Corners{12.0f, 12.0f, 12.0f, 0.0f};
    spec.tail = scene::Tail{.side = mine ? scene::TailSide{scene::tail_side::right{}} : scene::TailSide{scene::tail_side::left{}}};
  }
  if (border)
    spec.border = *border;
  // Frosted: what is behind it blurred under all of it, a pane filling it to
  // its edges past its padding, in its corners -- the plate the pane's tint.
  std::optional<frost_pane> frost;
  if (blur) {
    frost.emplace(frost_source{}, *blur);
    const scene::Margin pad = spec.padding.given.value_or(scene::Margin{});
    frost->apply({.place = scene::anchor::kTopLeft, .fill = true, .margin = {-pad.fTop, -pad.fRight, -pad.fBottom, -pad.fLeft},
                  .cornerRadius = spec.cornerRadius, .corners = spec.corners, .tail = spec.tail});
    frost->setTint(spec.background.value_or(0));
    spec.background = skia::SkColor{0};
  }
  // The time: on a line of its own under the rest, and beside the last line
  // where that leaves room for it (the rules decide which), out of the
  // flow at its end. Guessed before its first layout from the text wrapped
  // at the bubble's widest, so that a bubble made anew is drawn right from
  // its first frame -- the layout checks the guess.
  auto time = c::visible(time_shown, c::styled(time_spec, nodes::Text(when, 11.0f, mine ? colours.sent_time : colours.dim)));
  auto inline_time = c::visible(false, c::styled({.place = scene::anchor::kBottomRight, .y = kTimeLower},
                                                 nodes::Text(when, 11.0f, mine ? colours.sent_time : colours.dim)));
  if (skia::SkFont* font = skiff::paint::defaultFont(); font && text.visible() && !text.text().empty() && !rules.sticker) {
    const skiff::paint::Painter p(nullptr, *font);
    if (const auto wrapped_lines = p.wrap(text.text(), kBubbleMaxWidth, 13.0f, false); !wrapped_lines.empty()) {
      const float needs = p.measure(wrapped_lines.back(), 13.0f, false) + p.measure(when, 11.0f) + 10.0f;
      if (needs <= kBubbleMaxWidth) {
        time.setVisible(false);
        inline_time.setVisible(true);
        rules.widened = std::ceil(needs) + 2.0f * kBubblePadX;
        spec.minWidth = std::max(rules.base_min, rules.widened);
      }
    }
  }
  return c::arranged<bubble_rules>(
      std::move(rules),
      c::column(c::vbox(2.0f, spec), std::move(frost), std::move(name), std::move(forwarded), std::move(quote), std::move(picture),
                std::move(their_sticker), std::move(album), std::move(file), std::move(text), std::move(blocks), std::move(preview),
                trailers_t::built(trailer_facts{colours, looks, said, in, said.id}), std::move(thread), std::move(lines),
                std::move(their_view), std::move(time), std::move(inline_time)));
}
// Who has read a message: those whose receipt is for it or for one after
// it, by their names in the chat -- its sender and the user aside.
[[nodiscard]] inline std::vector<seen_reader> seen_by(const conversation& chat, const std::string& id, const std::string& sender) {
  const auto place = [&](const std::string& event) -> std::optional<std::size_t> {
    const auto at = std::ranges::find(chat.timeline, event, &message::id);
    return at == chat.timeline.end() ? std::nullopt : std::optional(static_cast<std::size_t>(at - chat.timeline.begin()));
  };
  const auto mine = place(id);
  if (!mine)
    return {};
  const auto when = chat.timeline[*mine].at;
  auto out = chat.read_by | std::views::filter([&](const auto& read) {
               const auto& [user, event] = read;
               if (user == sender || user == chat.id.account.address)
                 return false;
               // Read up to a message here at or after it; else, the receipt
               // pointing at what is not among these -- a reaction, a state
               // event -- read at or after it was sent.
               if (const auto theirs = place(event))
                 return *theirs >= *mine;
               const auto read_at = chat.receipt_times.find(user);
               return read_at != chat.receipt_times.end() && read_at->second >= when;
             }) |
             std::views::transform([&](const auto& read) {
               const auto read_at = chat.receipt_times.find(read.first);
               return seen_reader{read.first, sender_name(chat, read.first),
                                  read_at == chat.receipt_times.end() ? std::nullopt : std::optional(read_at->second)};
             }) |
             std::ranges::to<std::vector>();
  std::ranges::sort(out, {}, &seen_reader::name);
  return out;
}

// A message's menu, for a right press on its row wherever it is shown -- the
// timeline, a thread: what it offers, from the message, its chat as the
// model has it now, what is selected in its text and the link pressed.
struct row_menu {
  const bubble_needs* needs = nullptr;
  conversation_id in;
  message said;
  std::optional<menu_facts> operator()(auto& row, float x, float y) const {
    auto& body = std::get<1>(std::get<0>(std::get<0>(row.fParts).fParts).fParts);
    const auto& text = part_of<bubble_part::text>(body);
    const conversation* chat = needs->now ? needs->now->find(in) : nullptr;
    menu_facts facts;
    facts.id = said.id;
    if (chat && !facts.id.empty() && !held_message(*chat, facts.id))
      facts.event = said;
    facts.own = said.outgoing;
    facts.text = said.body.plain;
    facts.selection = text.hasSelection();
    if (facts.selection)
      facts.copied_fragment = text.selectedFragment();
    facts.copied = facts.copied_fragment ? facts.copied_fragment->text : said.body.plain;
    if (const auto& kept = said.attachment) {
      facts.media = kept->video.value_or(kept->source);
      facts.media_name = kept->name;
      facts.moving = moves(kept->kind);
      if (is_picture(kept->kind) || kept->video)
        facts.picture = kept->source;
      facts.captioned = is_picture(kept->kind) && !kept->video;
    }
    if (chat) {
      const message* held = held_message(*chat, said.id);
      const message& current = held ? *held : said;
      facts.pinned = std::ranges::contains(chat->pinned, said.id);
      const protocol_state_t now = protocol_state_of(*needs->shared, chat->id.account);
      // What its account does, offered only where anything may be done now.
      if (proto::available(now))
        facts.can = ops_of(*needs->shared, chat->id.account);
      facts.can.react = facts.can.react && proto::may_react(now, *chat, current);
      facts.pinnable = proto::can_pin(now, said.id);
      // Edited as its protocol's rule allows: any of one's own, or the last.
      facts.editable = !said.service && !said.reaction && proto::may_edit(now, *chat, said);
      facts.history = !said.versions.empty();
      // Delete as the protocol allows it -- Matrix: as the room's power levels do.
      facts.deletable = proto::may_delete(now, *chat, said.outgoing);
      facts.reaction_events = !current.reaction_events.empty();
      facts.reaction_count = std::ranges::fold_left(current.reactions | std::views::values | std::views::transform([](const auto& who) { return who.size(); }),
                                                    std::size_t{0}, std::plus{});
      // The message's link, where its protocol has one.
      if (auto link = proto::message_link(now, *chat, said.id))
        facts.link = std::move(*link);
      facts.seen = seen_by(*chat, said.id, said.sender);
    }
    // The link the press was on: one in the text -- its text asked a menu
    // of its own with it, which this one is in place of -- or the preview's,
    // where the pointer is on it.
    if (auto pressed = skiff::scene::pressedLink())
      facts.pressed_link = std::move(*pressed);
    else if (const auto& preview = part_of<bubble_part::preview>(body); preview && preview->hovered())
      facts.pressed_link = preview->fEvent.url;
    facts.emoji = copied_emoji_at(facts.pressed_link);
    // A sticker: what sending it again takes.
    if (said.sticker && said.attachment)
      facts.sticker = emote{.shortcode = said.attachment->name,
                            .url = said.attachment->source,
                            .body = said.attachment->name,
                            .w = said.attachment->width > 0 ? std::optional<std::int64_t>(said.attachment->width) : std::nullopt,
                            .h = said.attachment->height > 0 ? std::optional<std::int64_t>(said.attachment->height) : std::nullopt,
                            .mimetype = said.attachment->mimetype.empty() ? std::nullopt : std::optional<std::string>(said.attachment->mimetype)};
    // A reaction shown as a line: what it is on, and with what.
    if (!said.reaction_key.empty() && said.replies_to)
      facts.reaction = menu_facts::reaction_facts{*said.replies_to, said.reaction_key};
    facts.x = x;
    facts.y = y;
    return facts;
  }
};
// A reaction shown as a line: pressed anywhere, to what it is on.
struct reaction_line_press {
  std::optional<std::string> on;
  std::string id;
  std::optional<request::jump_to_message> operator()(auto&, float, float) const {
    return on ? std::optional(request::jump_to_message{*on, std::nullopt, id}) : std::nullopt;
  }
};
[[nodiscard]] inline bool is_reaction_line(const message& said) {
  return said.service && said.replies_to &&
         spl::visit(spl::overloaded{[](room_event::reactions) { return true; }, [](room_event::unreactions) { return true; },
                                    [](const auto&) { return false; }},
                    said.event_kind);
}


// A message's row: the avatar's room and the bubble -- at the right where it
// is one's own, in the middle where it is something done -- the arrow a
// swipe shows, the unread bar over it, who has read it under it. Swiped
// left, it is answered; washed while it is selected, flashed where a jump
// went to it; coming in moving where it has just come (arrives).
inline auto message_row(const bubble_needs& n, const conversation& in, const bubble_facts& f, const model* now, bool arrives) {
  namespace c = skiff::compose;
  const palette& colours = *n.colours;
  const looks_shown& looks = *n.looks;
  const message& said = f.said;
  const bool group = is_group(in);
  const bool as_lines = spl::visit(spl::overloaded{[](proto::part::style::lines) { return true; }, [](proto::part::style::bubbles) { return false; }},
                                   proto::message_style(protocol_state_of(*n.shared, in.id.account)));
  // As tdesktop: a sender's messages one under the other nearly touch; where
  // the sender changes, a gap. The avatar hangs at the row's bottom left, out
  // of its flow, the row keeping its width on the left.
  const bool with_face = group && !said.outgoing && !said.service;
  const bool face_shown = with_face && f.last;
  auto face = c::visible(with_face, c::styled({.place = scene::anchor::kBottomLeft, .x = -(kBubbleAvatar + 8.0f), .y = -1.0f,
                                               .alpha = face_shown ? static_cast<float>(element_opacity_of(looks.bubbles, &config::element_opacity::avatars)) / 100.0f
                                                                   : 0.0f},
                                              c::onPress(open_face{said.sender, face_shown},
                                                         avatar_mark(avatar_of::person{}, said.sender, sender_name(in, said.sender), kBubbleAvatar),
                                                         sender_name(in, said.sender))));
  const nodes::Justify justify = said.service                    ? nodes::Justify{nodes::justify::middle{}}
                                 : said.outgoing && !as_lines ? nodes::Justify{nodes::justify::end{}}
                                                              : nodes::Justify{nodes::justify::start{}};
  auto inner = c::onClickAt(reaction_line_press{is_reaction_line(said) ? said.replies_to : std::nullopt, said.id},
                            c::row(c::justified(c::hbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY,
                                                               .padding = {f.first ? 8.0f : 1.0f, 0.0f, 1.0f, with_face ? kBubbleAvatar + 8.0f : 0.0f}}),
                                                justify),
                                   std::move(face), message_body(n, in, f, now)));
  auto mark = c::styled({.place = scene::anchor::kCentreRight, .x = -6.0f, .width = 28.0f, .height = 28.0f, .cornerRadius = 14.0f,
                         .background = colours.tile, .alpha = 0.0f},
                        nodes::Icon(shape_of(icon::back{}), colours.dim));
  // Over the first unread message of a chat opened: tdesktop's bar, across
  // the whole list -- held out past the row's sides and the list's.
  std::optional<decltype(unread_bar(colours))> unread;
  if (f.unread_start) {
    unread.emplace(unread_bar(colours));
    unread->apply({.margin = {0.0f, -kListSide, 0.0f, -kListSide}});
  }
  std::optional<readers_row_t> readers;
  if (!f.readers.empty()) {
    readers.emplace(readers_row(colours, in, f.readers));
    readers->apply({.place = scene::anchor::kBottomRight, .x = -4.0f, .y = kReadersHeight + 1.0f});
  }
  // Drawn once and played back until something in it changes -- not one
  // with a picture or a file (a loader turns in it while it comes, a picture
  // may move), nor a frosted one (its backdrop is where it is on the screen).
  const bool frosted = frosts(looks.bubbles);
  auto row = c::onRightPressAt(row_menu{&n, in.id, said}, c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY,
                                      .padding = {f.unread_start ? kUnreadBarHeight + 6.0f : 0.0f, 0.0f,
                                                  f.readers.empty() ? 0.0f : kReadersHeight + 2.0f, 0.0f}}),
                       c::swiped(kSwipeToReply, 120.0f, sends<request::reply_to>{{said.id, said.body.plain}}, swipe_shown{&colours},
                                 std::move(mark), std::move(inner)),
                       std::move(unread), std::move(readers)));
  row.fState.setRecorded(!said.attachment && said.album.empty() && !frosted);
  // Not shown where the chat's settings say so: kept, and out of the flow.
  row.setVisible(!said.service || f.events_shown);
  return c::washed_for<message_marks>(colours.accent, 56.0f, 80.0f,
      [id = said.id](const message_marks& marks) {
        return skiff::compose::Wash{.held = marks.previewed ? marks.previewed->contains(id) : marks.selected.contains(id),
                                    .flash = marks.flashed == id ? marks.flash : 0};
      },
      c::appearing(arrives,
                   // Messages being selected: a press on one selects it or lets
                   // it go, rather than doing what a press there does.
                   c::seized_for<message_marks>([](const message_marks& marks) { return !marks.selected.empty(); },
                                                sends<request::toggle_selected>{{said.id}}, std::move(row))));
}
using message_row_t = decltype(message_row(std::declval<const bubble_needs&>(), std::declval<const conversation&>(),
                                           std::declval<const bubble_facts&>(), nullptr, false));

// ---- the old one ----------------------------------------------------------------
// The bubble as it was, a struct the timeline and the chat screens reach
// into; gone once they keep message_row's rows.
namespace legacy {
// A message as the chat shows it. A template on the program's Actions, made
// where every protocol's UI module is seen: its sticker part is the basic
// picture or one of a protocol's own sticker nodes (sticker_views).
// What a message's trailers are made from (contract::message_trailers):
// the message, its chat, and what it is drawn in.
struct message_bubble : skiff::compose::Stacked {
  // ---- the protocols' own sticker nodes -------------------------------------
  template <class List>
  struct sticker_nodes;
  template <class... Vs>
  struct sticker_nodes<proto::sticker_view_list<Vs...>> {
    using type = type_list<Vs...>;
  };
  template <class>
  struct protocol_sticker_nodes;
  template <class... Tags>
  struct protocol_sticker_nodes<protocol_list<Tags...>> {
    using type = typename joined<
        type_list<>, typename sticker_nodes<decltype(sticker_views_for(::mux::state_of<Tags>{}))>::type...>::type;
  };
  // Never empty: a text, where no protocol draws its own.
  using their_sticker_t = typename variant_of_types<
      typename joined<type_list<nodes::Text>, typename protocol_sticker_nodes<protocols>::type>::type>::type;
  template <class>
  struct protocol_message_nodes;
  template <class... Tags>
  struct protocol_message_nodes<protocol_list<Tags...>> {
    using type = typename joined<
        type_list<>, typename sticker_nodes<decltype(message_views_for(::mux::state_of<Tags>{}))>::type...>::type;
  };
  using their_view_t = typename variant_of_types<
      typename joined<type_list<nodes::Text>, typename protocol_message_nodes<protocols>::type>::type>::type;
  using view_holder = skiff::compose::Holding<their_view_t>;
  // As a node: the protocol's sticker in it.
  // As tall and as wide as it is.
  using sticker_holder = skiff::compose::Holding<their_sticker_t>;
  // The message as it was shown, and where in its sender's run: while
  // these are the same, the bubble is kept.
  message said;
  bool first = false, last = false;
  // The message: its id and text, for its menu.
  std::string message_id;
  std::string plain;
  bool outgoing = false;
  std::string sender;

  // tdesktop's msgPadding, margins(11, 8, 11, 8); its time sits lower than
  // the text by msgDateDelta, point(2, 5): 5 over the bubble's bottom.
  static constexpr float kPadX = 11.0f;
  static constexpr float kPadY = 8.0f;
  static constexpr float kTimeLower = kPadY - 5.0f;
  static constexpr float kAvatar = 34.0f;
  static constexpr float kMaxWidth = 480.0f;

  // What it answers, as tdesktop's reply (history_view_reply.cpp): a block
  // tinted with the sender's colour (at 0.12), rounded 5, with a bar of it
  // down its left (3, at 0.9); the sender's name in it, semibold, over a
  // line of the message in the text's colour; a quoted picture's thumbnail,
  // 32 and rounded, at its left (historyReplyPreview). Its padding,
  // historyReplyPadding: 2 above and below, 11 at the left, 6 at the right.
  static constexpr float kReplyLineMax = 240.0f;  // tdesktop's maxSignatureSize
  [[nodiscard]] static skia::SkColor with_alpha(skia::SkColor colour, float alpha) {
    return (colour & 0x00FFFFFFu) | (static_cast<skia::SkColor>(std::lround(alpha * 255.0f)) << 24);
  }
  // What plugins put after the text, at the bubble's foot
  // (contract::message_trailers) -- the reactions -- in their order, each in
  // a slot of its own: empty where it has nothing to show of the message.
  template <class List>
  struct plugin_trailers;
  template <class... Features>
  struct plugin_trailers<plugin_list<Features...>> {
    template <class Feature>
    static auto made(const trailer_facts& facts) -> decltype(extend(contract::message_trailers{}, type_tag<Feature>{}, facts)) {
      static_assert(requires { extend(contract::message_trailers{}, type_tag<Feature>{}, facts); },
                    composition::broken<contract::message_trailers, Feature>());
      return extend(contract::message_trailers{}, type_tag<Feature>{}, facts);
    }
    using nodes_t =
        std::tuple<std::optional<composition::node_t<decltype(made<Features>(std::declval<const trailer_facts&>()))>>...>;
    static void build(nodes_t& slots, const trailer_facts& facts) {
      [&]<std::size_t... At>(std::index_sequence<At...>) {
        (composition::put(std::get<At>(slots), made<Features>(facts)), ...);
      }(std::index_sequence_for<Features...>{});
    }
    // The last shown, where there is one: what the time goes beside, and how
    // far its last line goes, as its plugin says (trailing_width).
    struct end {
      skia::SkRect bounds;
      float width = 0.0f;
    };
    static std::optional<end> last(const nodes_t& slots) {
      std::optional<end> found;
      [&]<std::size_t... At>(std::index_sequence<At...>) {
        ((std::get<At>(slots) && std::get<At>(slots)->visible()
              ? void(found = end{std::get<At>(slots)->bounds(),
                                 trailing_width(contract::message_trailers{}, type_tag<Features>{}, *std::get<At>(slots))})
              : void()),
         ...);
      }(std::index_sequence_for<Features...>{});
      return found;
    }
  };
  using trailers_t = plugin_trailers<composition::providers<contract::message_trailers, enabled_plugins>>;
  // The bubble: as wide as what it says, up to its largest.
  struct body_column : skiff::compose::Stacked {
    bool outgoing = false;
    using name_row = decltype(sender_line(std::declval<const palette&>(), std::string{}, skia::SkColor{}, std::string{}, 0.0f));
    struct parts_t {
      // Frosted: what is behind, blurred, under all of it.
      std::optional<frost_pane> frost;
      std::optional<name_row> name;
      // Forwarded: from whom, in the accent, as Telegram's.
      std::optional<forward_line_t> forwarded;
      std::optional<quote_row_t> quote;
      std::optional<picture_view_t> picture;
      // A sticker its protocol draws itself, in the picture's place.
      std::optional<sticker_holder> their_sticker;
      std::optional<album_view_t> album;
      std::optional<file_view_t> file;
      nodes::BasicText<message_pictures> text;
      // Code blocks and link cards in text order, each with the words after it.
      std::vector<message_piece> blocks;
      std::optional<page_preview_t> preview;
      // What plugins put after the text: the reactions.
      typename trailers_t::nodes_t trailers;
      // A thread's root: its summary -- how many answers, the latest -- as
      // Element shows it under the message; pressed, the thread.
      std::optional<nodes::Text> thread;
      // What its protocol says under it (proto::message_lines).
      std::vector<nodes::Text> lines;
      // A node of its protocol's own under it (make_message_view).
      std::optional<view_holder> their_view;
      nodes::Text time;
      // The time inside the last line of the text, where that line leaves
      // room for it, as Telegram's: out of the column's flow, at its end.
      nodes::Text inline_time;
    } parts;
    skia::SkColor plate = 0;
    // Its picture a sticker's: no bubble under it, its time over it.
    bool sticker = false;
    // The last trailer shown, where there is one: what the time goes beside.
    [[nodiscard]] auto last_trailer() const { return trailers_t::last(parts.trailers); }
    // A protocol's sticker placed, where it made one.
    bool place_sticker(std::nullopt_t) { return false; }
    void place_view(std::nullopt_t) {}
    template <class View>
    void place_view(std::optional<View> made) {
      if (made)
        parts.their_view.emplace(std::move(*made));
    }
    template <class View>
    bool place_sticker(std::optional<View> made) {
      if (!made)
        return false;
      parts.their_sticker.emplace(skiff::compose::vbox(0.0f, {.autoSize = scene::axes::kBoth}), std::move(*made));
      return true;
    }
    // A sticker's name, forward and quote beside it, as tdesktop's unwrapped
    // media (history_view_media_unwrapped.cpp, drawSurrounding): to its right
    // where it came in, to its left where it was sent, from its top down;
    // over it, as before, only where the row has no room for both
    // (countCurrentSize's _additionalOnTop). They stood over every sticker.
    bool beside = false;
    bool beside_left = false;
    float beside_room = 0.0f;  // how wide what goes beside it may be
    static constexpr float kBesideGap = 8.0f;
    void layoutChildren() {
      // Beside, where the room it was given holds the sticker and the widest
      // of them (as last laid out; a quote's least width before it was).
      bool side = false;
      if (beside && parts.picture) {
        float widest = 0.0f;
        const auto widest_of = [&](auto& part) {
          if (part && part->visible())
            widest = std::max(widest, part->bounds().isEmpty() ? 120.0f : part->bounds().width());
        };
        widest_of(parts.name);
        widest_of(parts.forwarded);
        widest_of(parts.quote);
        const float sticker = std::max(parts.picture->bounds().width(), parts.picture->fState.fWidth);
        // countCurrentSize: beside where the sticker and the least of what
        // goes beside it fit -- msgReplyPadding.left() + msgMinWidth / 2 --
        // what goes beside it narrowed to what is left.
        constexpr float kLeastBeside = 10.0f + 190.0f / 2.0f;
        side = sticker + kBesideGap + std::min(widest, kLeastBeside) <= fState.fLastConstraint.width();
        beside_room = std::max(0.0f, fState.fLastConstraint.width() - sticker - kBesideGap);
      }
      const auto flow = [&](auto& part) {
        if (!part)
          return;
        part->fState.setOutOfFlow(side);
        if (!side)
          part->fState.shiftTo(0.0f, 0.0f);
      };
      flow(parts.name);
      flow(parts.forwarded);
      flow(parts.quote);
      // Beside it, as wide as what is left at the most, as tdesktop narrows
      // its reply to the surrounding width; over it, as the bubble lets.
      if (parts.forwarded)
        parts.forwarded->apply({.maxWidth = side ? beside_room : 0.0f});
      if (parts.quote)
        parts.quote->apply({.maxWidth = side ? beside_room : 0.0f});
      this->nodes::Stack::layoutChildren();
      // The column can wrap again in its final box. Position its overlay
      // here, after that layout, rather than keeping a pre-layout offset.
      const auto trailer = this->last_trailer();
      if (parts.inline_time.visible()) {
        const auto last = trailer ? trailer->bounds : parts.text.bounds();
        if (!last.isEmpty()) {
          parts.inline_time.apply({.y = last.fBottom - fState.contentBox().fBottom + kTimeLower});
          scene::layout(parts.inline_time, fState.contentBox());
        }
      }
      if (time_placed && parts.text.visible() && parts.blocks.empty() && !parts.preview && !beside) {
        const auto last = trailer ? trailer->bounds : parts.text.bounds();
        if (last != placed_beside) {
          time_placed = false;
          this->markDamaged();
        }
      }
      if (!side)
        return;
      const skia::SkRect sticker = parts.picture->bounds();
      // The forward and the reply on one plate, as tdesktop's one service
      // rect: the forward's corners round at its top, the reply's at its
      // bottom, nothing between them.
      const bool both = parts.forwarded && parts.forwarded->visible() && parts.quote && parts.quote->visible();
      if (parts.forwarded)
        parts.forwarded->apply({.corners = both ? scene::Corners{8.0f, 8.0f, 0.0f, 0.0f} : scene::Corners{8.0f, 8.0f, 8.0f, 8.0f}});
      if (parts.quote)
        parts.quote->apply({.corners = both ? scene::Corners{0.0f, 0.0f, 8.0f, 8.0f} : scene::Corners{8.0f, 8.0f, 8.0f, 8.0f}});
      float y = 0.0f;
      const auto put = [&](auto& part, float after) {
        if (!part || !part->visible())
          return;
        const skia::SkRect at = part->bounds();
        const float x = beside_left ? sticker.fLeft - kBesideGap - at.width() : sticker.fRight + kBesideGap;
        part->fState.shiftTo(x - at.fLeft, sticker.fTop + y - at.fTop);
        y += at.height() + after;
      };
      put(parts.name, 4.0f);
      put(parts.forwarded, both ? 0.0f : 4.0f);
      put(parts.quote, 4.0f);
    }
    // Its least width as the message asks it (a quote's), and as the time
    // beside the last line asks it: the bubble widened to hold both.
    float base_min = 0.0f;
    float widened = 0.0f;
    float placed_most = 0.0f;  // the widest the text could be, as last placed
    // Whether where the time goes has been decided, from a layout: until it
    // has, the bubble asks for frames, for a window at rest updates nothing
    // and the time stayed under the text.
    bool time_placed = false;
    // Frames asked for only once it has been laid out, and until the time is
    // placed: a bubble made but not laid out yet -- out of view, of the
    // eighty made around it -- asked for frames forever, and every frame
    // repainted the chat and the list beside it.
    [[nodiscard]] bool settling() const { return !time_placed && !fState.fBounds.isEmpty(); }
    // Ticked only until the time is placed: every bubble in view was ticked
    // at every frame for as long as it was shown. What moves it again --
    // its text, its reactions -- marks it.
    [[nodiscard]] bool wantsTick() const { return !time_placed; }
    [[nodiscard]] static skia::SkColor mixed(skia::SkColor from, skia::SkColor to, float amount) {
      const auto channel = [&](int shift) {
        const float a = static_cast<float>((from >> shift) & 0xFF), b = static_cast<float>((to >> shift) & 0xFF);
        return static_cast<skia::SkColor>(std::lround(a + (b - a) * amount)) << shift;
      };
      return (from & 0xFF000000u) | channel(16) | channel(8) | channel(0);
    }
    // The time goes beside the last line of the text wherever the two fit in
    // the bubble at its widest, as tdesktop's -- the bubble widened to them
    // where it is narrower; on a line of its own only where they do not.
    // Decided from the last layout; a change is laid out at the next.
    void update(double now_ms) {
      auto [picture, text, blocks, preview, time, inline_time] = std::tie(parts.picture, parts.text, parts.blocks, parts.preview, parts.time, parts.inline_time);
      const auto trailer = this->last_trailer();
      // A sticker's time is over it, and nowhere else: placed beside its
      // reactions too, it was shown twice.
      if (picture && sticker) {
        time_placed = true;
        // Over it where it was sent; beside it where it came, shown by
        // skiff while the message is under the pointer (revealOnHover).
        if (inline_time.visible() || (beside_left && time.visible())) {
          if (beside_left)
            time.setVisible(false);
          inline_time.setVisible(false);
          this->invalidateLayout();
        }
        return;
      }
      // Nothing left of the text -- all of it the quote the header shows --
      // or a text that ends in a quote, and nothing under it: the time on a
      // line of its own, as Telegram's -- not beside an empty last line, nor
      // on the quote's plate, over its words.
      const std::string& words = text.text();
      const bool ends_quoted = !words.empty() && std::ranges::any_of(text.styles(), [&](const auto& one) {
        return one.quote && one.first < words.size() && words.size() <= one.last;
      });
      if (text.visible() && (words.empty() || ends_quoted) && !trailer) {
        time_placed = true;
        if (words.empty())
          text.setVisible(false);
        if (!time.visible() || inline_time.visible()) {
          time.setVisible(true);
          inline_time.setVisible(false);
          widened = 0.0f;
          fState.apply({.minWidth = base_min});
          this->invalidateLayout();
        }
        return;
      }
      if (!blocks.empty() || preview || (!text.visible() && !trailer)) {
        time_placed = true;  // under it, as it is
        return;
      }
      skia::SkFont* font = skiff::paint::defaultFont();
      if (font == nullptr)
        return;
      if (!trailer && text.bounds().isEmpty()) {
        this->guess_time(*font);
        return;
      }
      // What the time goes beside: the last trailer where there is one -- the
      // reactions, as Telegram puts it on their line -- else the text's last
      // line.
      const skia::SkRect last = trailer ? trailer->bounds : text.bounds();
      if (last.isEmpty())
        return;
      time_placed = true;
      const float last_width = trailer ? trailer->width : text.lastLineWidth();
      // Measured again only where what it goes beside moved or changed: not
      // a font's measuring for every bubble in view, every frame.
      // The widest the text can be here: the bubble's widest, or what the
      // row gives it where that is less -- a phone's window. Against the
      // widest alone, the time was put beside a last line the bubble could
      // not widen for, over its last words.
      const float given = fState.fLastConstraint.width() - 2.0f * kPadX;
      const float most = given > 0.0f ? std::min(kMaxWidth, given) : kMaxWidth;
      if (last == placed_beside && last_width == placed_width && inline_time.text() == placed_time && most == placed_most)
        return;
      placed_beside = last;
      placed_width = last_width;
      placed_time = inline_time.text();
      placed_most = most;
      const float needs =
          last_width + skiff::paint::Painter(nullptr, *font).measure(inline_time.text(), 11.0f) + 10.0f;
      const bool inside = needs <= most;
      const float widest = inside ? std::ceil(needs) + 2.0f * kPadX : 0.0f;
      if (inside == time.visible() || widest != widened) {
        time.setVisible(!inside);
        inline_time.setVisible(inside);
        widened = widest;
        fState.apply({.minWidth = std::max(base_min, widest)});
        time_placed = false;
        this->invalidateLayout();
      }
    }
    skia::SkRect placed_beside = skia::SkRect::MakeEmpty();
    float placed_width = -1.0f;
    std::string placed_time;
    void grow_tail(bool mine) {
      const scene::Corners squared = mine ? scene::Corners{12.0f, 12.0f, 0.0f, 12.0f} : scene::Corners{12.0f, 12.0f, 12.0f, 0.0f};
      // The bubble's own fill and its tail one shape (skiff's Tail): a
      // see-through bubble is so once, with no seam -- a tail of its own
      // over the bubble's edge showed both through, darker where they met.
      fState.apply({.corners = squared,
                    .tail = scene::Tail{.side = mine ? scene::TailSide{scene::tail_side::right{}} : scene::TailSide{scene::tail_side::left{}}}});
      // The backdrop and its tint use the same shape, including its tail.
      this->sync_frost();
    }
    // Before its first layout, where the time goes is guessed from the text
    // wrapped at the bubble's widest -- where it does wrap, but in a chat
    // narrower than a bubble. So a bubble made anew -- sent, edited, reacted
    // to -- is drawn right from its first frame, not with its time under
    // its text for one and then beside it, a jump each time. The layout
    // checks the guess, above.
    bool guessed = false;
    void guess_time(skia::SkFont& font) {
      auto [text, time, inline_time] = std::tie(parts.text, parts.time, parts.inline_time);
      if (std::exchange(guessed, true) || text.text().empty())
        return;
      const skiff::paint::Painter p(nullptr, font);
      const auto lines = p.wrap(text.text(), kMaxWidth, 13.0f, false);
      if (lines.empty())
        return;
      const float needs = p.measure(lines.back(), 13.0f, false) + p.measure(inline_time.text(), 11.0f) + 10.0f;
      if (needs > kMaxWidth)
        return;
      time.setVisible(false);
      inline_time.setVisible(true);
      widened = std::ceil(needs) + 2.0f * kPadX;
      fState.apply({.minWidth = std::max(base_min, widened)});
    }
    // The bubble's colour as its chat's look has it: solid, or at its
    // opacity -- over what is behind it, frosted where it is so.
    [[nodiscard]] static skia::SkColor plate_of(const palette& colours, const config::bubble_look& look, bool mine) {
      const skia::SkColor solid = mine ? colours.out_bubble : colours.bubble;
      return spl::visit(spl::overloaded{[&](config::bubbles::solid) { return solid; },
                                              [&](const auto&) { return at_opacity(solid, look.opacity); }},
                           look.kind);
    }
    // Frosted as much as `blur` says: a pane behind all of it, filling it to
    // its edges, in its corners.
    void frosted(float blur) {
      if (parts.frost) {
        parts.frost->setBlur(blur);
        return;
      }
      parts.frost.emplace(frost_source{}, blur);
      parts.frost->apply({.place = scene::anchor::kTopLeft, .fill = true, .margin = {-kPadY, -kPadX, -kPadY, -kPadX},
                          .cornerRadius = 12.0f});
      // Its plate over the frost, not under it: the pane's tint, its own none.
      parts.frost->setTint(fState.fBackground);
      fState.apply({.background = skia::SkColor{0}});
      this->sync_frost();
    }
    // The pane in its shape: to its edges past its padding, in its corners --
    // each corner's own, where a tail squares one. Again as either changes:
    // a pane left at other padding stood out of the bubble, over the rows
    // around it and past what was repainted of it.
    void sync_frost() {
      if (!parts.frost)
        return;
      const scene::Margin& pad = fState.fPadding;
      parts.frost->apply({.margin = {-pad.fTop, -pad.fRight, -pad.fBottom, -pad.fLeft}, .cornerRadius = fState.fCornerRadius,
                          .corners = fState.fCorners, .tail = fState.fTail});
    }
    body_column(const palette &colours, const looks_shown &looks, bool mine,
                std::string said, std::string when)
        : Stacked(skiff::compose::vbox(2.0f, {})), outgoing(mine),
          parts{.text = skiff::compose::styled(
                    {.maxWidth = kMaxWidth},
                    wrapped(nodes::BasicText<message_pictures>(
                        std::move(said), 13.0f, colours.text))),
                .time = skiff::compose::styled(
                    {.alignSelf = scene::align::kEnd},
                    nodes::Text(when, 11.0f,
                                mine ? colours.sent_time : colours.dim)),
                .inline_time = skiff::compose::visible(
                    false,
                    skiff::compose::styled(
                        {.place = scene::anchor::kBottomRight, .y = kTimeLower},
                        nodes::Text(when, 11.0f,
                                    mine ? colours.sent_time : colours.dim)))},
          plate(plate_of(colours, looks.bubbles, mine)) {
      auto& text = parts.text;
      fState.apply({.autoSize = scene::axes::kBoth, .maxWidth = kMaxWidth + 2.0f * kPadX,
                    .padding = {kPadY, kPadX, kPadY, kPadX}, .cornerRadius = 12.0f, .background = plate});
      // Frosted: what is behind blurred under the tint; glass: a light edge.
      spl::visit(spl::overloaded{[&](config::bubbles::frosted) { this->frosted(blur_of(looks.bubbles, looks.window)); },
                                       [&](config::bubbles::glass) {
                                         fState.apply({.border = scene::Border{skia::colorSetARGB(70, 255, 255, 255), 1.0f}});
                                       },
                                       [](const auto&) {}},
                    looks.bubbles.kind);
      text.setShrinksToLines(true);
      // Wrapped at the bubble's width however wide the room it is first
      // measured in: not at the chat's, the bubble then capped narrower
      // than its lines.

      // Shown once the last line is found to leave room for it.
    }
  };

  using unread_bar_t = decltype(unread_bar(std::declval<const palette&>()));
  // The colours it is made in: handed down, kept for what it makes later.
  const palette* colours_ = nullptr;
  // The looks shown: the program's.
  const looks_shown* looks_ = nullptr;
  // The inherited blur used to make the panes: changing the window's
  // Frosted setting changes a row even when its message stays the same.
  double frost_shown = 10.0;
  // What the window's parts share: the accounts' protocol states.
  const ui_shared* shared_ = nullptr;
  struct parts_t {
    // The sender's avatar, beside the last of their run in a group; the
    // same room, empty, beside the rest.
    legacy::avatar_mark face;
    body_column body;
    // The arrow a swipe shows, filling as it reaches its mark.
    nodes::Icon swipe_mark;
    // Over the first unread message of a chat opened: tdesktop's bar.
    std::optional<unread_bar_t> unread_bar;
    // Under it, where the chat shows them: who has read up to it.
    std::optional<readers_row_t> readers;
  } parts;
  // Whether it has the bar: kept while the chat's first unread is it.
  // Who has read up to it, as shown: while the same, the bubble is kept.
  std::vector<std::string> readers_shown;
  static constexpr float kReaders = 16.0f;
  void show_readers(const conversation& in, std::vector<std::string> users) {
    readers_shown = std::move(users);
    if (readers_shown.empty())
      return;
    parts.readers.emplace(readers_row(*colours_, in, readers_shown));
    parts.readers->apply({.place = scene::anchor::kBottomRight, .x = -4.0f, .y = kReaders + 1.0f});
    fState.apply({.padding = {fState.fPadding.fTop, fState.fPadding.fRight, fState.fPadding.fBottom + kReaders + 2.0f,
                              fState.fPadding.fLeft}});
    this->invalidateLayout();
  }
  bool unread_start = false;
  // The list's own room at its left and right, around every row.
  static constexpr float kListSide = 12.0f;
  // tdesktop's "Unread messages" bar, across the whole row, over it.
  void mark_unread_start() {
    unread_start = true;
    parts.unread_bar.emplace(unread_bar(*colours_));
    // Across the whole list, not the row's content box: held out past the
    // row's own padding (a group's room for the avatar) and the list's sides,
    // which a relative width is measured inside of.
    parts.unread_bar->apply({.margin = {0.0f, -(fState.fPadding.fRight + kListSide), 0.0f,
                                        -(fState.fPadding.fLeft + kListSide)}});
    fState.apply({.padding = {fState.fPadding.fTop + kUnreadBarHeight + 6.0f, fState.fPadding.fRight,
                              fState.fPadding.fBottom, fState.fPadding.fLeft}});
    this->invalidateLayout();
  }

  // Declared: the avatar's room and the bubble, at the right where it is
  // one's own; the bubble a column of the name, the quote, the text, the
  // links, the reactions and the time.
  // What it is handed down: what plays a voice message in it.
  struct needs {
    platform::audio::speaker* sound = nullptr;
    const palette* colours = nullptr;
    looks_shown* looks = nullptr;
    ui_shared* shared = nullptr;
  };
  message_bubble(const needs& n, const conversation& in, const message& given, bool first_of_run, bool last_of_run,
                 const model* now = nullptr, bool show_events = true, bool show_preview = true)
      : message_bubble(n, in, with_actor(in, given), first_of_run, last_of_run, now, show_events, show_preview, made_t{}) {}
  // What the one above makes it of: the message with its pills.
  struct made_t {};
  message_bubble(const needs &n, const conversation &in, const message &said,
                 bool first_of_run, bool last_of_run, const model *now,
                 bool show_events, bool show_preview, made_t)
      : Stacked(skiff::compose::hbox(8.0f, {})), said(said),
        first(first_of_run), last(last_of_run), message_id(said.id),
        plain(said.body.plain), outgoing(said.outgoing), sender(said.sender),
        colours_(n.colours), looks_(n.looks), frost_shown(n.looks->window.frost), shared_(n.shared),
        parts{.face = skiff::compose::styled(
                  {.place = scene::anchor::kBottomLeft,
                   .x = -(kAvatar + 8.0f),
                   .y = -1.0f},
                  legacy::avatar_mark(avatar_of::person{}, said.sender, sender_name(in, said.sender),
                              kAvatar)),
              .body = body_column(*n.colours, *n.looks, said.outgoing,
                                  said.body.plain,
                                  mark_of(said) + clock_of(said.at)),
              .swipe_mark = skiff::compose::styled(
                  {.place = scene::anchor::kCentreRight,
                   .x = -6.0f,
                   .width = 28.0f,
                   .height = 28.0f,
                   .cornerRadius = 14.0f,
                   .background = colours_->tile,
                   .alpha = 0.0f},
                  nodes::Icon(shape_of(icon::back{}), n.colours->dim))} {
    // Drawn once and played back until something in it changes: a strip of
    // the list repainted went through every part of every message in it.
    // Not one with a picture or a file: a loader turns in it while it comes,
    // a picture may move -- recorded again at every frame, and where its
    // parts were last drawn left behind when history moved it.
    // Frosted, its backdrop is where it is on the screen: drawn each time,
    // not played back from where it was recorded.
    const bool frosted = spl::visit(spl::overloaded{[](config::bubbles::frosted) { return true; },
                                                          [](const auto&) { return false; }},
                                       looks_->bubbles.kind);
    fState.setRecorded(!said.attachment && said.album.empty() && !frosted);
    auto& [face, body, swipe_mark, unread_bar, readers] = parts;

    // As tdesktop: a sender's messages one under the other nearly touch;
    // where the sender changes, a gap.
    const bool group = is_group(in);
    // The avatar hangs at the row's bottom left, as tdesktop's: out of the
    // row's flow, the row keeping its width on the left. In the flow, a
    // 34-high avatar made a one-line bubble's row taller, and the last
    // bubble of a run stood apart from the rest.
    // Laid out as the chat's protocol says: bubbles, or lines.
    const bool as_lines = spl::visit(spl::overloaded{[](proto::part::style::lines) { return true; },
                                                           [](proto::part::style::bubbles) { return false; }},
                                        proto::message_style(protocol_state_of(*shared_, in.id.account)));
    const bool with_face = group && !outgoing && !said.service;
    fState.apply({.fillX = true, .autoSize = scene::axes::kY,
                  .padding = {first_of_run ? 8.0f : 1.0f, 0.0f, 1.0f, with_face ? kAvatar + 8.0f : 0.0f}});
    if (outgoing && !as_lines)
      fStack.justify = nodes::justify::end{};
    // Lines: full width, on the wallpaper, no plate.
    if (as_lines)
      body.apply({.fillX = true, .maxWidth = 0.0f, .cornerRadius = 0.0f, .background = skia::SkColor{0}});
    face.setVisible(with_face);
    // Placed in the content box: back over the padding kept for it.
    if (!(group && !outgoing && last_of_run))
      face.fState.setAlpha(0.0f);  // its room kept, so the run's bubbles line up
    else
      face.fState.setAlpha(static_cast<float>(element_opacity_of(looks_->bubbles, &config::element_opacity::avatars)) / 100.0f);
    if (((group && !outgoing) || as_lines) && first_of_run && !said.service) {
      // Their role, as the chat's protocol says it (Matrix: its power levels).
      body.parts.name.emplace(sender_line(*colours_, sender_name(in, said.sender), avatar_colour(said.sender),
                                          proto::sender_role(protocol_state_of(*shared_, in.id.account), in, said.sender),
                                          kMaxWidth - 60.0f));
    }
    // Forwarded: "Forwarded from" its sender, at its top, as Telegram's.
    // The sender a person's pill, as a mention is, and pressed, opens them.
    if (said.forwarded) {
      const std::string& who = said.forwarded->name.empty() ? said.forwarded->from : said.forwarded->name;
      std::vector<nodes::Text::Link> spans;
      // A person their protocol links to: their pill, and their picture.
      const auto link = proto::person_link(state_before(protocol_of(said.forwarded->from)), said.forwarded->from);
      if (link && !who.empty())
        spans.push_back(nodes::Text::Link{0, who.size(), *link});
      mentioned shown = with_mentions(who, std::move(spans), in, now);
      body.parts.forwarded.emplace(forward_line(*colours_, std::move(shown.text), std::move(shown.links),
                                                outgoing ? colours_->sent_time : colours_->accent,
                                                link ? std::optional<std::string>(said.forwarded->from) : std::nullopt));
    }
    // Something done, not said: a line in the middle, on a plate of its own,
    // with no avatar and no name -- as tdesktop's service messages.
    if (said.service) {
      fStack.justify = nodes::justify::middle{};
      face.setVisible(false);
      body.apply({.cornerRadius = 12.0f,
                  .background = at_opacity(colours_->tile, element_opacity_of(looks_->bubbles, &config::element_opacity::service))});
      // Frosted as its own blur says.
      if (frosts(looks_->bubbles))
        body.frosted(element_blur_of(looks_->bubbles, &config::element_blur::service, looks_->window));
      // Not shown where the chat's settings say so: kept, and out of the
      // flow, taking no room.
      events_shown = show_events;
      this->setVisible(show_events);
    }
    // Anyone's words can be selected and copied, as in Telegram.
    body.parts.text.setSelectable(true);
    body.parts.text.setSelectionColour((colours_->accent & 0x00FFFFFFu) | (110u << 24));  // the accent, see-through
    // In an encrypted room, a message that did not come encrypted says so,
    // as Element's "Not encrypted": it may have been put there by the server
    // or by anyone, in the clear.
    // One's own too: a client of one's own that sends in the clear is as
    // much to be seen. What came before the room was encrypted is not
    // marked: it was said in the clear, as the room was then.
    const bool plain_in_encrypted = in.encrypted && !said.encrypted && !said.service &&
                                    (said.came_plain || !in.encrypted_since || said.at >= *in.encrypted_since);
    // And one that came encrypted from a device its sender did not
    // cross-sign: the server may have made that device up.
    // Element's warnings, shortened to fit beside the time: not encrypted;
    // encrypted by a device not verified by its owner; its authenticity not
    // guaranteed on this device (its key from the backup or an import).
    const std::string warning = plain_in_encrypted                        ? std::string("not encrypted \u00b7 ")
                                : said.encrypted && said.unverified       ? std::string("not verified by its owner \u00b7 ")
                                : said.encrypted && said.unauthenticated ? std::string("authenticity not guaranteed \u00b7 ")
                                                                          : std::string();
    std::string when = warning + mark_of(said) + clock_of(said.at);
    when += spl::visit(spl::overloaded{[](const delivery::sending&) { return " · sending"; },
                                  [](const delivery::failed&) { return " · not sent"; },
                                  [](const auto&) { return ""; }},
                       said.delivery);
    body.parts.time.setText(when);
    // And the time beside the last line, which is the one shown wherever it
    // fits: made with the bare time, it never said "not encrypted", nor
    // "sending" -- a short message, the most of them, hid both.
    body.parts.inline_time.setText(when);
    // A sticker its protocol draws itself (a Telegram TGS or WebM): its node,
    // not the picture.
    const bool theirs = said.sticker && spl::visit(
                                            [&](const auto& now) {
                                              using sticker_defaults::make_sticker;
                                              return body.place_sticker(make_sticker(now, said));
                                            },
                                            protocol_state_of(*shared_, in.id.account));
    // What a picture of it opens, pressed: it in the viewer -- a video played
    // there, or, built without video, by the system's player.
    const std::string sender_shown = sender_name(in, said.sender);
    const std::string when_shown = std::format("{:%d.%m.%Y} at {}",
        std::chrono::year_month_day{std::chrono::floor<std::chrono::days>(said.at)}, clock_of(said.at));
    const auto opens = [&](const std::string& source, const std::optional<std::string>& video, const std::string& name) -> media_open {
      if (video && !mux::platform::video::kPlays)
        return request::open_file{*video, name};
      if (video)
        return request::open_video{source, *video, said.sender, sender_shown, when_shown};
      return request::open_picture{source, said.sender, sender_shown, when_shown};
    };
    // What it carries: a picture, sized as tdesktop's; or a file's row.
    if (said.attachment && !theirs) {
      const mux::attachment& carried = *said.attachment;
      const bool bare = said.body.plain.empty() && !said.body.html;
      body.sticker = said.sticker && is_picture(carried.kind);
      // Its time over it: a sticker sent, at its bottom right corner (tdesktop's
      // UnwrappedMedia); a picture with no caption.
      const bool time_over = body.sticker ? said.outgoing : bare;
      spl::visit(spl::overloaded{[&](attachment_kind::image) {
                              body.parts.picture.emplace(picture_view(*colours_, carried.source, carried.width, carried.height,
                                  {.sticker = body.sticker,
                                   .video_ms = carried.video ? std::optional<std::int64_t>(carried.duration_ms) : std::nullopt,
                                   .thumbnail = !carried.video || carried.video != carried.source,
                                   .time = time_over ? when : std::string()},
                                  opens(carried.source, carried.video, carried.name)));
                            },
                            [&](attachment_kind::file) {
                              const bool sound = audio_type(carried.mimetype, carried.name);
                              body.parts.file.emplace(file_view(*colours_, carried.source, carried.name, carried.size, sound,
                                  sound ? file_open(request::play_audio{carried.source})
                                        : file_open(request::open_file{carried.source, carried.name})));
                            }},
                 carried.kind);
      // A sticker: on nothing -- no bubble, no padding.
      if (body.sticker && body.parts.picture) {
        body.parts.text.setVisible(false);
        if (said.outgoing) {
          body.parts.time.setVisible(false);
        } else {
          // One that came: its time beside it at the bottom, to its right, on
          // a plate of its own, shown while the pointer is over the message,
          // as tdesktop's needInfoDisplay (isUnderCursor): skiff shows and
          // hides it.
          body.parts.time.apply({.place = scene::anchor::kBottomRight, .origin = scene::anchor::kBottomLeft, .x = 6.0f,
                                 .padding = {2.0f, 6.0f, 2.0f, 6.0f}, .cornerRadius = 8.0f, .background = body.plate,
                                 .visible = false, .revealOnHover = true});
        }
        body.parts.frost.reset();
        body.apply({.padding = {0.0f, 0.0f, 0.0f, 0.0f}, .background = skia::SkColor{0},
                    .border = scene::Border{skia::SkColor{0}, 0.0f}});
      } else if (bare) {
        // No caption: the text goes, and a picture has its time over it.
        body.parts.text.setVisible(false);
        if (body.parts.picture) {
          body.parts.time.setVisible(false);
          body.apply({.padding = {3.0f, 3.0f, 3.0f, 3.0f}});
          body.sync_frost();
        }
      }
    }
    // A gallery: its pictures as an album, its body the caption under it,
    // each pressed opening it in the viewer.
    if (!said.album.empty())
      body.parts.album.emplace(album_view(*colours_, said.album,
          said.album | std::views::transform([&](const attachment& one) { return opens(one.source, std::nullopt, one.name); }) |
              std::ranges::to<std::vector>()));
    // Pictures at the chat's look's opacity for them.
    if (const float images = static_cast<float>(element_opacity_of(looks_->bubbles, &config::element_opacity::images)) / 100.0f;
        images < 1.0f) {
      if (body.parts.picture)
        body.parts.picture->apply({.alpha = images});
      if (body.parts.album)
        body.parts.album->apply({.alpha = images});
    }
    // Formatted, it is drawn from its HTML: its text, and its links; plain,
    // its links are the URLs in it.
    // Its links in its text, where they stand: an <a>'s label going where
    // its href says, and the addresses in a plain text.
    mux::body readable = said.body;
    if (!said.reaction_key.empty() && said.service) {
      std::optional<std::string> label;
      if (said.replies_to)
        if (const auto* parent = held_message(in, *said.replies_to))
          if (const auto event = std::ranges::find(parent->reaction_events, said.id, &message::reaction_event::event);
              event != parent->reaction_events.end()) label = event->shortcode;
      auto reaction = reaction_body_of(said.reaction_key, in.emotes, label);
      if (reaction.html) {
        readable.plain = sender_name(in, said.sender) + " reacted " + reaction.plain;
        readable.html = std::format(R"(<a href="https://matrix.to/#/{}">{}</a> reacted )",
            chevron::escaped(said.sender), chevron::escaped(sender_name(in, said.sender))) + *reaction.html;
      }
    }
    mentioned shown;
    if (readable.html) {
      auto read = read_html(*readable.html);
      shown = with_mentions(std::move(read.text), std::move(read.spans), in, now, std::move(read.styles), std::move(read.disclosures));
    } else {
      shown = with_mentions(readable.plain, link_spans_in(readable.plain), in, now);
    }
    rooms_waiting = std::move(shown.waiting);
    rooms_unknown = std::move(shown.unknown);
    // A reply whose text opens with its only quote, of the message it
    // answers: the quote shown in the reply's header, as the part answered,
    // and not again in the text. Only where that message says all of it --
    // one that has a piece of the quote, or another's words, is answered
    // with a quote of its own, shown as one.
    if (said.replies_to) {
      const message* answered = held_message(in, *said.replies_to);
      mentioned taken = shown;
      if (const auto quote = take_opening_quote(taken);
          quote && answered && squeezed(quote_line_of(*answered, in, now)).contains(squeezed(*quote))) {
        header_quote = quote;
        shown = std::move(taken);
      }
    }
    {
      // The quote's colour: the accent on theirs; on one's own, the text's,
      // as tdesktop's outgoing blockquote -- not the accent on its accent.
      const skia::SkColor quote_colour = outgoing ? colours_->text : colours_->accent;
      // Keep words, code blocks and link cards in their original order.
      const std::vector<text_piece> pieces = pieces_of(shown.text, shown.links, shown.styles, shown.cards, shown.disclosures);
      body.parts.text.setText(pieces.front().text);
      body.parts.text.setLinks(pieces.front().links, colours_->accent);
      body.parts.text.setStyles(pieces.front().styles, quote_colour);
      body.parts.text.setVisible(!pieces.front().text.empty());
      for (std::size_t i = 1; i + 1 < pieces.size(); i += 2) {
        if (pieces[i].disclosure)
          body.parts.blocks.emplace_back(std::in_place_type<disclosure_piece_t>,
              disclosure_piece(*colours_, shown, pieces[i + 1], now, quote_colour));
        else if (pieces[i].card)
          body.parts.blocks.emplace_back(std::in_place_type<link_piece_t>,
              link_piece(*colours_, *pieces[i].card, pieces[i + 1], now, quote_colour));
        else
          body.parts.blocks.emplace_back(std::in_place_type<code_piece_t>,
              code_piece(*colours_, pieces[i], pieces[i + 1], quote_colour, quote_colour, colours_->text));
      }
    }
    // The last of a run: its bottom corner on the sender's side squared and
    // a tail grown from it, as Telegram draws one -- not for a line of what
    // was done, nor a picture with nothing under it.
    const bool bare_picture = said.attachment && said.body.plain.empty() && !said.body.html && body.parts.picture;
    if (last_of_run && !said.service && !bare_picture && !as_lines)
      body.grow_tail(outgoing);
    // The first link's preview, where it has come.
    previews_shown = show_preview;
    if (const auto link = first_link_of(said); link && now && show_preview)
      if (const auto found = now->previews.find(*link); found != now->previews.end()) {
        body.parts.preview.emplace(page_preview(*colours_, found->second, *link));
        preview_known = true;
      }
    if (said.replies_to) {
      // In the timeline, in a thread -- an answer quoting another -- or aside.
      const message* found = held_message(in, *said.replies_to);
      const bool known = found != nullptr;
      quote_known = known;
      // A picture's: its thumbnail, and its caption or "Photo"; a file's:
      // its name; else its text.
      std::optional<std::string> picture;
      std::string line = header_quote ? *header_quote : known ? quote_line_of(*found, in, now) : std::string("not loaded");
      if (known && found->attachment) {
        if (is_picture(found->attachment->kind))
          picture = found->attachment->source;
        if (line.empty())
          line = is_picture(found->attachment->kind) ? std::string("Photo") : found->attachment->name;
      }
      if (!header_quote)
        std::ranges::replace(line, '\n', ' ');
      const bool with_picture = picture.has_value();
      body.parts.quote.emplace(quote_row(*colours_, known ? avatar_colour(found->sender) : colours_->accent,
                         known ? (found->outgoing ? std::string("You") : sender_name(in, found->sender))
                               : std::string("A message"),
                         std::move(line), header_quote ? std::nullopt : std::move(picture), header_quote.has_value(), known && found->sticker));
      // The quote spans its bubble, as tdesktop's; the bubble is at least as
      // wide as the quote asks -- its name and its line, the line counted up
      // to maxSignatureSize (240), so that a short answer to a long message
      // does not stretch its bubble to the full width.
      float asks = 160.0f;
      if (skia::SkFont* font = skiff::paint::defaultFont()) {
        skiff::paint::Painter measure(nullptr, *font);
        const auto& texts = std::get<2>(body.parts.quote->fParts).fParts;
        const auto& name = std::get<0>(texts).fParts;
        // The name line with "quoted" after it, whole: the bubble widened for
        // the tag, rather than the tag drawn over the name.
        const auto& tag = std::get<1>(name);
        const float who = measure.measure(std::get<0>(name).text(), 13.0f) +
                          (tag ? 8.0f + std::ceil(measure.measure(tag->text(), 11.0f)) : 0.0f);
        const float words = std::max(who, std::min(measure.measure(std::get<1>(texts).text(), 13.0f), kReplyLineMax));
        const float around = (with_picture ? 7.0f + 32.0f + 4.0f : 11.0f) + 6.0f + 2.0f * kPadX;
        asks = std::min(std::ceil(words) + around, kMaxWidth + 2.0f * kPadX);
      }
      body.base_min = asks;
      body.apply({.minWidth = asks});
    }
    // A sticker's: no bubble under it, so what is over it -- the sender's
    // name, a forward's line, the quote of what it answers -- each on a small
    // plate of its own, as Telegram's; they stood on the wallpaper.
    if (body.sticker && body.parts.picture) {
      const auto plated = [&](scene::Node& part) {
        part.apply({.padding = {3.0f, 8.0f, 3.0f, 8.0f}, .cornerRadius = 8.0f, .background = body.plate});
      };
      // No sender's name over it: tdesktop's drawSurrounding draws a topic,
      // a forward, via and the reply, never the name.
      if (body.parts.name)
        body.parts.name->setVisible(false);
      if (body.parts.forwarded)
        plated(*body.parts.forwarded);
      if (body.parts.quote)
        plated(*body.parts.quote);
      // Beside it, not over it: the sticker's own width, not a quote's.
      body.beside = true;
      body.beside_left = said.outgoing;
      body.base_min = 0.0f;
      body.apply({.minWidth = 0.0f});
    }
    if (said.threaded && said.threaded->count > 0) {
      const thread_summary& summary = *said.threaded;
      std::string line = std::format("\U0001F4AC {} {}", summary.count, summary.count == 1 ? "reply" : "replies");
      if (!summary.last_text.empty())
        line += std::format(" \u00b7 {}: {}", sender_name(in, summary.last_sender), summary.last_text);
      if (!header_quote)
        std::ranges::replace(line, '\n', ' ');
      body.parts.thread.emplace(std::move(line), 13.0f, colours_->accent, true);
      body.parts.thread->setElided(true);
      body.parts.thread->apply({.fillX = true, .margin = {4.0f, 0.0f, 0.0f, 0.0f}});
    }
    std::ranges::for_each(proto::message_lines(protocol_state_of(*shared_, in.id.account), in, said), [&](const proto::part::line& one) {
      nodes::Text& shown = body.parts.lines.emplace_back(one.text, 12.0f, tone_colour(*colours_, one.tone));
      shown.setWrapped(true);
      shown.apply({.fillX = true, .margin = {4.0f, 0.0f, 0.0f, 0.0f}});
    });
    spl::visit(
        [&](const auto& now) {
          using view_defaults::make_message_view;
          body.place_view(make_message_view(now, said));
        },
        protocol_state_of(*shared_, in.id.account));
    trailers_t::build(body.parts.trailers, trailer_facts{*colours_, *looks_, said, in, message_id});
  }

  // Swiped to the left to answer it: how far, following the pointer, and
  // back to its place when let go. Its avatar and bubble drawn moved --
  // where they are laid out does not change -- and the arrow of a reply
  // coming in at the right, out of the row's flow, lit once it is far
  // enough.
  // Whether the message its reply quotes was there to quote when it was
  // made: made again once it is, from the timeline or fetched beside it.
  bool quote_known = true;
  // What the message replied to said as this was made: made again where it changes.
  std::optional<decltype(message::body)> quote_said;
  // Whether room events were shown when it was made: made again when that
  // changes.
  bool events_shown = true;
  // Whether its link's preview had come when it was made.
  bool preview_known = false;
  // Whether its chat shows link previews, as it was made.
  bool previews_shown = true;
  // Rooms it names whose picture or whose being there is still to come; and
  // those not joined, for the server to be asked of.
  std::vector<std::string> rooms_waiting;
  std::vector<std::string> rooms_unknown;
  // A picture waited on has come, or a room not known was found: each is
  // waited on for that alone -- a found room without a picture is not
  // "come" again at every frame.
  [[nodiscard]] bool rooms_came(const model& now) const {
    return std::ranges::any_of(rooms_waiting, [](const std::string& key) { return avatar_images().has(key); }) ||
           std::ranges::any_of(rooms_unknown, [&](const std::string& key) { return now.rooms_found.contains(key); });
  }
  skiff::paint::Tween swipe{0.0f, 180.0f, skiff::paint::movement::subtle{}};
  static constexpr float kSwipeToReply = 70.0f;
  // Where it was jumped to: the whole row -- from the message to the edges,
  // as Telegram's -- washed in the accent, fading.
  skiff::paint::Tween flash{0.0f, 1200.0f};
  // A message that has just come: in from below, fading in, as Telegram's.
  // Unseen until its first frame is laid out, so the time is where it goes
  // when it is first seen -- not under the text, then beside it.
  skiff::paint::Tween appearing{1.0f, 220.0f};
  // A stretch of its text marked -- what a reply quoted of it -- while it
  // is flashed; let go as the flash ends.
  bool marked = false;
  // Selected, among messages selected: the whole row washed in the accent,
  // as the flash washes it, and held.
  bool selected = false;
  [[nodiscard]] skia::SkColor wash(float strength) const {
    return (colours_->accent & 0x00FFFFFFu) | (static_cast<skia::SkColor>(std::lround(strength)) << 24);
  }
  void select(bool on) {
    if (on == selected)
      return;
    selected = on;
    if (!flash.moving())
      fState.apply({.background = selected ? this->wash(56.0f) : skia::SkColor{0}});
  }
  // The quote its text opened with, shown in its header instead: what a
  // click on the header goes to, marked.
  std::optional<std::string> header_quote;
  // Marks what is found of `fragment` in its text; where, as the offset in
  // it, or nothing.
  std::optional<std::size_t> mark(std::string_view fragment) {
    auto& text = parts.body.parts.text;
    const auto at = std::string_view(text.text()).find(fragment);
    if (fragment.empty() || at == std::string_view::npos)
      return std::nullopt;
    auto styles = text.styles();
    styles.push_back({.first = at, .last = at + fragment.size(), .marked = true});
    text.setStyles(std::move(styles), outgoing ? colours_->text : colours_->accent);
    marked = true;
    return at;
  }
  void unmark() {
    auto& text = parts.body.parts.text;
    auto styles = text.styles();
    std::erase_if(styles, [](const auto& one) { return one.marked; });
    text.setStyles(std::move(styles), outgoing ? colours_->text : colours_->accent);
    marked = false;
  }
  void appear() {
    appearing.jump(0.0f);
    appearing.setTarget(1.0f);
    fState.apply({.alpha = 0.0f, .shiftY = 12.0f});
    scene::work::mark(fState.fId);  // its frames asked for: nothing else asks, and it stayed shifted
  }
  // The swipe's offset as last drawn: a drag jumps the tween, which then
  // does not move, and the bubble followed only once it was let go.
  float swipe_drawn = 0.0f;
  [[nodiscard]] bool settling() const {
    return swipe.moving() || swipe.value() != swipe_drawn || flash.moving() || appearing.moving() || marked;
  }
  // Ticked while it flashes, appears or is swiped: at rest, not.
  [[nodiscard]] bool wantsTick() const { return this->settling(); }
  void update(double now_ms) {
    if (flash.step(now_ms))
      fState.apply({.background = this->wash(std::max(80.0f * flash.value(), selected ? 56.0f : 0.0f))});
    if (marked && !flash.moving())
      this->unmark();
    if (appearing.step(now_ms)) {
      const float shown = appearing.value();
      fState.apply({.alpha = shown, .shiftY = (1.0f - shown) * 12.0f});
    }
    const bool stepped = swipe.step(now_ms);
    if (!stepped && swipe.value() == swipe_drawn)
      return;
    auto& [face, body, swipe_mark, unread_bar, readers] = parts;
    const float shift = swipe.value();
    swipe_drawn = shift;
    const float reached = std::clamp(-shift / kSwipeToReply, 0.0f, 1.0f);
    face.apply({.shiftX = shift});
    body.apply({.shiftX = shift});
    swipe_mark.apply({.background = reached >= 1.0f ? colours_->accent : colours_->tile, .alpha = reached});
    swipe_mark.setColour(reached >= 1.0f ? colours_->on_accent : colours_->dim);
  }

  // Pressed with the right button, it asks for its menu.
  [[nodiscard]] bool acceptsInput() const { return true; }
};

}  // namespace legacy

}  // namespace mux::ui

// Outside a release build each child is walked through its table, and a
// stack lays itself out through its own (Stack::layoutChildren): so a unit
// that lays out or walks one of these would make its table, and the walks
// of everything under it, there -- the chats screen's took this unit past
// ten minutes. Their tables are made once, each in a walks unit of this
// module (src/ui/walks_*.cc); here, and wherever they are walked, a call.
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::legacy::message_bubble> = true;  // walks_bubbles.cc
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::legacy::message_bubble>() noexcept;
