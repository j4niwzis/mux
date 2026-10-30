// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:message -- A message: its bubble, its pictures, files, reactions and mentions.
module;
#include "skiff/named.h"
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

[[nodiscard]] skia::SkColor quote_tint(skia::SkColor colour, float alpha) {
  return (colour & 0x00FFFFFFu) | (static_cast<skia::SkColor>(std::lround(alpha * 255.0f)) << 24);
}
[[nodiscard]] auto quote_row(const palette& colours, skia::SkColor colour,
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
// What a plugin puts above a message's text (contract::message_heads): a
// quoted line, pressed to go where it says -- the replies', the message
// answered -- and the bubble's least width for it.
using head_node_t = decltype(skiff::compose::onClick(request::jump_to_message{}, quote_row(std::declval<const palette&>(), skia::SkColor{},
                                                                                          std::string{}, std::string{})));
struct message_head {
  head_node_t node;
  float least_width = 0.0f;
};
// What a plugin is given to make it from: the message, its chat, and its
// text as it will be shown -- the plugin may take a part of it into its head.
struct head_facts {
  const palette& colours;
  const conversation& in;
  const message& said;
  const model* now;
  mentioned& shown;
};

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
auto sender_line(const palette& colours, std::string who, skia::SkColor colour, std::string role, float widest) {
  auto name = elided(nodes::Text(std::move(who), 13.0f, colour, true));
  name.setMaxWidth(widest);
  const bool has_role = !role.empty();
  return SKIFF_NAMED(sender_line, skiff::compose::row(skiff::compose::hbox(10.0f, {.autoSize = scene::axes::kBoth}), std::move(name),
                             skiff::compose::visible(has_role, skiff::compose::styled({.alignSelf = scene::align::kEnd},
                                                                                      nodes::Text(std::move(role), 12.0f, colours.dim)))));
}
// tdesktop's bar over the first unread: its words in the middle of a band
// the width of the chat, over the row.
inline constexpr float kUnreadBarHeight = 26.0f;
auto unread_bar(const palette& colours) {
  return SKIFF_NAMED(unread_bar, skiff::compose::column(
      skiff::compose::justified(skiff::compose::vbox(0.0f, {.place = scene::anchor::kTopLeft, .y = -(kUnreadBarHeight + 4.0f), .fillX = true,
                                                            .height = kUnreadBarHeight, .background = colours.sidebar}),
                                nodes::justify::middle{}),
      skiff::compose::styled({.alignSelf = scene::align::kMiddle}, nodes::Text("Unread messages", 13.0f, colours.dim, true))));
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
// The message a message's head shows, as its plugin says (head_waits_for):
// fetched where it is not there; the row made again once it is.
template <class... Features>
[[nodiscard]] std::optional<std::string> heads_wait_for(plugin_list<Features...>, const message& said) {
  std::optional<std::string> out;
  ((out ? void() : void(out = head_waits_for(contract::message_heads{}, type_tag<Features>{}, said))), ...);
  return out;
}
[[nodiscard]] inline std::optional<std::string> head_message_of(const message& said) {
  return heads_wait_for(composition::providers<contract::message_heads, enabled_plugins>{}, said);
}
// A message's head: the quote of the message its plugin says it shows
// (head_message_of: a reply's, the one it answers) -- who wrote it, a line
// of it, or a picture's thumbnail and caption, a file's name; pressed, that
// message. A text that opens with its only quote, of that message: that
// quote in the head, marked "quoted", and not again in the text (`shown`
// taken from) -- only where the message says all of it.
[[nodiscard]] inline std::optional<message_head> quoted_head(head_facts f) {
  namespace c = skiff::compose;
  const auto shows = head_message_of(f.said);
  if (!shows)
    return std::nullopt;
  const message* found = held_message(f.in, *shows);
  const bool known = found != nullptr;
  std::optional<std::string> header_quote;
  {
    mentioned taken = f.shown;
    if (const auto quote = take_opening_quote(taken); quote && found && squeezed(quote_line_of(*found, f.in, f.now)).contains(squeezed(*quote))) {
      header_quote = quote;
      f.shown = std::move(taken);
    }
  }
  std::optional<std::string> pictured;
  std::string line = header_quote ? *header_quote : known ? quote_line_of(*found, f.in, f.now) : std::string("not loaded");
  if (known && found->attachment) {
    if (is_picture(found->attachment->kind))
      pictured = found->attachment->source;
    if (line.empty())
      line = is_picture(found->attachment->kind) ? std::string("Photo") : found->attachment->name;
  }
  if (!header_quote)
    std::ranges::replace(line, '\n', ' ');
  const bool with_picture = pictured.has_value();
  const std::string from = known ? (found->outgoing ? std::string("You") : sender_name(f.in, found->sender)) : std::string("A message");
  // The bubble at least as wide as the head asks -- its name, "quoted"
  // after it, and its line up to maxSignatureSize -- so that a short answer
  // to a long message does not stretch its bubble to the full width.
  float least = 160.0f;
  if (skia::SkFont* font = skiff::paint::defaultFont()) {
    skiff::paint::Painter measure(nullptr, *font);
    const float who = measure.measure(from, 13.0f) + (header_quote ? 8.0f + std::ceil(measure.measure("quoted", 11.0f)) : 0.0f);
    const float words_wide = std::max(who, std::min(measure.measure(line, 13.0f), kReplyLineMax));
    const float around = (with_picture && !header_quote ? 7.0f + 32.0f + 4.0f : 11.0f) + 6.0f + 2.0f * kBubblePadX;
    least = std::min(std::ceil(words_wide) + around, kBubbleMaxWidth + 2.0f * kBubblePadX);
  }
  return message_head{c::onClick(request::jump_to_message{*shows, header_quote, f.said.id},
                                 quote_row(f.colours, known ? avatar_colour(found->sender) : f.colours.accent, from, std::move(line),
                                           header_quote ? std::nullopt : std::move(pictured), header_quote.has_value(), known && found->sticker),
                                 "Go to the message answered"),
                      least};
}

// What plugins put in the bubble after its text and blocks
// (contract::message_cards) -- a link's preview -- in their order, each in
// a slot of its own: empty where it has nothing to show of the message.
template <class List>
struct plugin_cards;
template <class... Features>
struct plugin_cards<plugin_list<Features...>> {
  template <class Feature>
  static auto made(const card_facts& facts) -> decltype(extend(contract::message_cards{}, type_tag<Feature>{}, facts)) {
    static_assert(requires { extend(contract::message_cards{}, type_tag<Feature>{}, facts); },
                  composition::broken<contract::message_cards, Feature>());
    return extend(contract::message_cards{}, type_tag<Feature>{}, facts);
  }
  template <class Feature>
  using node_of = composition::node_t<decltype(made<Feature>(std::declval<const card_facts&>()))>;
  using nodes_t = std::tuple<std::optional<node_of<Features>>...>;
  static nodes_t built(const card_facts& facts) {
    nodes_t slots;
    [&]<std::size_t... At>(std::index_sequence<At...>) {
      (composition::put(std::get<At>(slots), made<Features>(facts)), ...);
    }(std::index_sequence_for<Features...>{});
    return slots;
  }
  // Whether any is shown: the time then goes under them.
  static bool any(const nodes_t& slots) {
    return std::apply([](const auto&... slot) { return (slot.has_value() || ...); }, slots);
  }
  // The link a press at x, y in a row is on, where it is on a card that
  // has one.
  template <class Feature>
    requires requires(const node_of<Feature>& shown, float x, float y) { pressed_link(contract::message_cards{}, type_tag<Feature>{}, shown, x, y); }
  static std::optional<std::string> pressed_in(auto& row, float x, float y) {
    const node_of<Feature>* shown = skiff::scene::findNode<node_of<Feature>>(row);
    return shown ? pressed_link(contract::message_cards{}, type_tag<Feature>{}, *shown, x, y) : std::nullopt;
  }
  // A card with no link of its own.
  template <class Feature>
  static std::optional<std::string> pressed_in(auto&, float, float) {
    return std::nullopt;
  }
  static std::optional<std::string> pressed(auto& row, float x, float y) {
    std::optional<std::string> found;
    ((found = found ? found : pressed_in<Features>(row, x, y)), ...);
    return found;
  }
};
using cards_t = plugin_cards<composition::providers<contract::message_cards, enabled_plugins>>;

// What plugins put under a message's row (contract::message_footers) -- who
// has read up to it -- in their order, each in a slot of its own: empty
// where it has nothing to show of the message.
template <class List>
struct plugin_footers;
template <class... Features>
struct plugin_footers<plugin_list<Features...>> {
  template <class Feature>
  static auto made(const footer_facts& facts) -> decltype(extend(contract::message_footers{}, type_tag<Feature>{}, facts)) {
    static_assert(requires { extend(contract::message_footers{}, type_tag<Feature>{}, facts); },
                  composition::broken<contract::message_footers, Feature>());
    return extend(contract::message_footers{}, type_tag<Feature>{}, facts);
  }
  using nodes_t = std::tuple<std::optional<composition::node_t<decltype(made<Features>(std::declval<const footer_facts&>()))>>...>;
  static nodes_t built(const footer_facts& facts) {
    nodes_t slots;
    [&]<std::size_t... At>(std::index_sequence<At...>) {
      (composition::put(std::get<At>(slots), made<Features>(facts)), ...);
    }(std::index_sequence_for<Features...>{});
    return slots;
  }
  // The room under the row they take: the tallest shown's.
  static float height(const nodes_t& slots) {
    float most = 0.0f;
    [&]<std::size_t... At>(std::index_sequence<At...>) {
      ((std::get<At>(slots) ? void(most = std::max(most, footer_height(contract::message_footers{}, type_tag<Features>{}))) : void()), ...);
    }(std::index_sequence_for<Features...>{});
    return most;
  }
};
using footers_t = plugin_footers<composition::providers<contract::message_footers, enabled_plugins>>;

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
  // An earlier version of a message, shown in its edit history: not edited
  // again, nor its history opened, from its menu.
  bool version = false;
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
[[nodiscard]] mentioned shown_text_of(const conversation& in, const message& said, const model* now) {
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
[[nodiscard]] named_rooms rooms_named_in(const conversation& in, const message& said, const model* now) {
  mentioned shown = shown_text_of(in, with_actor(in, said), now);
  return {std::move(shown.waiting), std::move(shown.unknown)};
}

// The body's parts, by where they are in it.
namespace bubble_part {
inline constexpr std::size_t frost = 0, name = 1, forwarded = 2, quote = 3, picture = 4, their_sticker = 5, album = 6, file = 7,
                             text = 8, blocks = 9, cards = 10, trailers = 11, thread = 12, lines = 13, their_view = 14, time = 15,
                             inline_time = 16;
}
template <std::size_t Part> [[nodiscard]] auto& part_of(auto& body) { return std::get<Part>(body.fParts); }


// Words of a message, as its text and its pieces show them: wrapped,
// selectable, its links and quotes in their colours.
auto message_words(const palette& colours, const text_piece& words, skia::SkColor colour, skia::SkColor quote) {
  auto text = wrapped(nodes::BasicText<message_pictures>(words.text, 13.0f, colour));
  text.setSelectable(true);
  text.setSelectionColour((colours.accent & 0x00FFFFFFu) | (110u << 24));  // the accent, see-through
  text.setLinks(words.links, colours.accent);
  text.setStyles(words.styles, quote);
  text.setShrinksToLines(true);
  return text;
}
// The bubble's words, marked where they are made: found by their mark where
// what holds them asks of them -- its rules, its menu, a jump to a quote.
struct bubble_words {};
using bubble_words_t = skiff::compose::Marked<bubble_words, decltype(skiff::compose::styled({}, message_words(std::declval<const palette&>(),
                                                                                               std::declval<const text_piece&>(), 0u, 0u)))>;
[[nodiscard]] bubble_words_t* words_in(auto& node) { return skiff::scene::findNode<bubble_words_t>(node); }

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
    const float sticker = std::max(picture->bounds().width(), skiff::compose::state_of(*picture).fWidth);
    // countCurrentSize: beside where the sticker and the least of what goes
    // beside it fit -- msgReplyPadding.left() + msgMinWidth / 2.
    constexpr float kLeastBeside = 10.0f + 190.0f / 2.0f;
    return sticker + kBesideGap + std::min(widest, kLeastBeside) <= body.fState.fLastConstraint.width();
  }
  static void prepare(auto& body, State& state) {
    const bool side = beside_now(body, state);
    if (auto& picture = part_of<bubble_part::picture>(body); side && picture)
      state.beside_room = std::max(0.0f, body.fState.fLastConstraint.width() -
                                             std::max(picture->bounds().width(), skiff::compose::state_of(*picture).fWidth) - kBesideGap);
    const auto flow = [&](auto& part) {
      if (!part)
        return;
      skiff::compose::state_of(*part).setOutOfFlow(side);
      if (!side)
        skiff::compose::state_of(*part).shiftTo(0.0f, 0.0f);
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
    if (state.time_placed && text.visible() && part_of<bubble_part::blocks>(body).empty() && !cards_t::any(part_of<bubble_part::cards>(body)) &&
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
      skiff::compose::state_of(*part).shiftTo(x - at.fLeft, sticker.fTop + y - at.fTop);
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
    // Its words, by their mark: none found where they are hidden.
    bubble_words_t* said = words_in(body);
    static const std::string kNothing;
    const std::string& words = said ? said->text() : kNothing;
    const bool ends_quoted = said && !words.empty() && std::ranges::any_of(said->styles(), [&](const auto& one) {
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
    if (!part_of<bubble_part::blocks>(body).empty() || cards_t::any(part_of<bubble_part::cards>(body)) || (!text.visible() && !trailer)) {
      // Under all of it, on a line of its own: what follows the text -- a
      // code block, a preview -- is no line to put it beside. Left as the
      // first guess had it, it stayed beside the text, in the middle of the
      // bubble, over what came after.
      if (inline_time.visible() && text.visible()) {
        inline_time.setVisible(false);
        time.setVisible(true);
        body.invalidateLayout();
      }
      state.time_placed = true;
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
    const float last_width = trailer ? trailer->width : said ? said->lastLineWidth() : 0.0f;
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

// The bubble: its parts in their places, which its rules name -- the one
// box of the bubble, its parts each held.
template <class... Parts> skiff::compose::Box<Parts...> bubble_box(skiff::compose::Look look, Parts... parts) {
  look.fHorizontal = false;
  return skiff::compose::Box<Parts...>(std::move(look), std::move(parts)...);
}
// The bubble's colour as its chat's look has it: solid, or at its opacity --
// over what is behind it, frosted where it is so.
[[nodiscard]] skia::SkColor plate_of(const palette& colours, const config::bubble_look& look, bool mine) {
  const skia::SkColor solid = mine ? colours.out_bubble : colours.bubble;
  return spl::visit(spl::overloaded{[&](config::bubbles::solid) { return solid; }, [&](const auto&) { return at_opacity(solid, look.opacity); }},
                    look.kind);
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
  // Its words, found by their mark under what is pressed.
  std::optional<request::jump_to_message> operator()(auto& pressed, float x, float y) const {
    const bubble_words_t* text = words_in(pressed);
    if (!replies_to || !text || text->hasSelection())
      return std::nullopt;
    const auto quote = text->quoteAt(x, y);
    if (!quote)
      return std::nullopt;
    return request::jump_to_message{
        *replies_to, trimmed_fragment(std::string_view(text->text()).substr(quote->first, quote->second - quote->first)), message_id};
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

// Keep the action on a container: Text's own pointer handler is for text
// selection and does not run an onClick inherited around the text itself.
inline auto message_thread_link(const palette& colours, std::string root, std::string line) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(message_thread_link, c::onClick(request::open_thread{std::move(root)},
      c::row(c::hbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY, .margin = {4.0f, 0.0f, 0.0f, 0.0f},
                            .hoverBackground = colours.chosen}),
          c::styled({.grow = scene::axes::kX}, elided(nodes::Text(std::move(line), 13.0f, colours.accent, true)))),
      "Open thread"));
}
using message_thread_link_t = decltype(message_thread_link(std::declval<const palette&>(), std::string{}, std::string{}));

// The bubble: a column of the name, the forward, the quote, the picture or
// file or album, the text and its pieces, the preview, the reactions, the
// thread's summary, the protocol's lines and node, the time.
// Generic in what it is given, so that this interface does not make it: it is
// made where it is called -- outside a release build only in its own unit
// (message_body.cc), held there.
auto message_body(const auto& n, const auto& in, const auto& f, const auto* now) {
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
  // One's own still on its way is not one: mux sends only encrypted to an
  // encrypted room, and its echo is marked as its copy from the server is
  // read -- "not encrypted" until then, on every message sent.
  const bool on_its_way = said.outgoing && spl::visit(spl::overloaded{[](const delivery::sending&) { return true; },
                                                                      [](const auto&) { return false; }},
                                                      said.delivery);
  const bool plain_in_encrypted = in.encrypted && !said.encrypted && !said.service && !on_its_way &&
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
                             said.album | std::views::transform([&](const attachment& one) { return opens(one.source, std::nullopt, one.name); }) | std::ranges::to<std::vector>()));
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
  // What goes above it, as a plugin says (contract::message_heads) -- the
  // quote of the message it answers, the replies' -- which may take the
  // text's opening quote into it.
  std::optional<message_head> head = quoted_head(head_facts{colours, in, said, now, shown});
  // The quote's colour: the accent on theirs; on one's own, the text's, as
  // tdesktop's outgoing blockquote -- not the accent on its accent.
  const skia::SkColor quote_colour = mine ? colours.text : colours.accent;
  // Words, code blocks and link cards in their own order.
  const std::vector<text_piece> pieces = pieces_of(shown.text, shown.links, shown.styles, shown.cards, shown.disclosures);
  auto words = c::styled({.maxWidth = kBubbleMaxWidth}, message_words(colours, pieces.front(), colours.text, quote_colour));
  words.setVisible(text_shown && !pieces.front().text.empty());
  // What the time's first guess is made from, before the words are held.
  const bool words_shown = words.visible();
  const std::string words_said = words.text();
  std::vector<message_piece> blocks;
  for (std::size_t i = 1; i + 1 < pieces.size(); i += 2) {
    if (pieces[i].disclosure)
      blocks.emplace_back(std::in_place_type<disclosure_piece_m>, disclosure_piece(colours, shown, pieces[i + 1], now, quote_colour));
    else if (pieces[i].card)
      blocks.emplace_back(std::in_place_type<link_piece_m>, link_piece(colours, *pieces[i].card, pieces[i + 1], now, quote_colour));
    else
      blocks.emplace_back(std::in_place_type<code_piece_m>, code_piece(colours, pieces[i], pieces[i + 1], quote_colour, quote_colour, colours.text));
  }
  auto text = c::onClickAt(quote_pressed{said.id, said.replies_to},
                           c::applied<message_marks>([id = said.id](const message_marks& marks) {
                             return marks.flashed == id ? marks.fragment : std::nullopt;
                           },
                           marks_fragment{pieces.front().styles, quote_colour}, bubble_words_t(std::move(words))));
  // The plugins' cards: the first link's preview, where it has come.
  auto cards = cards_t::built(card_facts{colours, said, first_link_of(said), now, f.previews_shown});
  // Its head: the bubble at least as wide as it asks.
  std::optional<head_node_t> quote;
  if (head) {
    rules.base_min = head->least_width;
    spec.minWidth = rules.base_min;
    quote.emplace(std::move(head->node));
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
  std::optional<message_thread_link_t> thread;
  if (said.threaded && said.threaded->count > 0) {
    const thread_summary& summary = *said.threaded;
    std::string line = std::format("\U0001F4AC {} {}", summary.count, summary.count == 1 ? "reply" : "replies");
    if (!summary.last_text.empty())
      line += std::format(" · {}: {}", sender_name(in, summary.last_sender), summary.last_text);
    std::ranges::replace(line, '\n', ' ');
    thread.emplace(message_thread_link(colours, said.id, std::move(line)));
  }
  // What its protocol says under it, and its node of its own.
  auto lines = proto::message_lines(protocol_state_of(*n.shared, in.id.account), in, said) | std::views::transform([&](const proto::part::line& one) {
                 return c::styled({.fillX = true, .margin = {4.0f, 0.0f, 0.0f, 0.0f}},
                                  wrapped(nodes::Text(one.text, 12.0f, tone_colour(colours, one.tone))));
               }) | std::ranges::to<std::vector>();
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
  // Only where the words are what ends the bubble: after a code block or a
  // preview, the time goes under them.
  if (skia::SkFont* font = skiff::paint::defaultFont(); font && words_shown && !words_said.empty() && !rules.sticker && pieces.size() == 1 && !cards_t::any(cards)) {
    const skiff::paint::Painter p(nullptr, *font);
    if (const auto wrapped_lines = p.wrap(words_said, kBubbleMaxWidth, 13.0f, false); !wrapped_lines.empty()) {
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
      bubble_box(c::vbox(2.0f, spec), std::move(frost), std::move(name), std::move(forwarded), std::move(quote), std::move(picture),
                std::move(their_sticker), std::move(album), std::move(file), std::move(text), std::move(blocks), std::move(cards),
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
             }) | std::views::transform([&](const auto& read) {
               const auto read_at = chat.receipt_times.find(read.first);
               return seen_reader{read.first, sender_name(chat, read.first),
                                  read_at == chat.receipt_times.end() ? std::nullopt : std::optional(read_at->second)};
             }) | std::ranges::to<std::vector>();
  std::ranges::sort(out, {}, &seen_reader::name);
  return out;
}

// A message's menu, for a right press on its row wherever it is shown -- the
// timeline, a thread: what it offers, from the message, its chat as the
// model has it now, what is selected in its text and the link pressed.
struct row_menu {
  bubble_needs needs;
  conversation_id in;
  message said;
  bool version = false;
  std::optional<menu_facts> operator()(auto& row, float x, float y) const {
    const bubble_words_t* text = words_in(row);
    const conversation* chat = needs.now ? needs.now->find(in) : nullptr;
    menu_facts facts;
    facts.id = said.id;
    if (chat && !facts.id.empty() && !held_message(*chat, facts.id))
      facts.event = said;
    facts.own = said.outgoing;
    facts.answerable = answerable(said);
    facts.text = said.body.plain;
    facts.selection = text && text->hasSelection();
    if (facts.selection)
      facts.copied_fragment = text->selectedFragment();
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
      const protocol_state_t now = protocol_state_of(*needs.shared, chat->id.account);
      // What its account does, offered only where anything may be done now.
      if (proto::available(now))
        facts.can = ops_of(*needs.shared, chat->id.account);
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
    else if (auto on_card = cards_t::pressed(row, x, y))
      facts.pressed_link = std::move(*on_card);
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
    // A row of what is not held as an event of its own -- a reaction kept
    // without its id: nothing it offers can be aimed at it.
    if (facts.id.empty()) {
      facts.can.react = false;
      facts.deletable = false;
    }
    if (version) {
      facts.editable = false;
      facts.history = false;
    }
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


// The bubble as the row holds it: outside a release build made once, in its
// own unit, and held; in one, made here, its type kept.
skiff::compose::Held message_body_held(const bubble_needs& n, const conversation& in, const bubble_facts& f, const model* now);
template <bool Erase = skiff::scene::kErasedCombinators>
auto bubble_of(const auto& n, const auto& in, const auto& f, const auto* now) {
  if constexpr (Erase)
    return message_body_held(n, in, f, now);
  else
    return message_body(n, in, f, now);
}
// A message's row: the avatar's room and the bubble -- at the right where it
// is one's own, in the middle where it is something done -- the arrow a
// swipe shows, the unread bar over it, who has read it under it. Swiped
// left, it is answered; washed while it is selected, flashed where a jump
// went to it; coming in moving where it has just come (arrives).
auto message_row(const bubble_needs& n, const conversation& in, const bubble_facts& f, const model* now, bool arrives) {
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
                                                         face_of(colours, avatar_of::person{}, said.sender, sender_name(in, said.sender), kBubbleAvatar),
                                                         sender_name(in, said.sender))));
  const nodes::Justify justify = said.service                    ? nodes::Justify{nodes::justify::middle{}}
                                 : said.outgoing && !as_lines ? nodes::Justify{nodes::justify::end{}}
                                                              : nodes::Justify{nodes::justify::start{}};
  auto inner = c::onClickAt(reaction_line_press{is_reaction_line(said) ? said.replies_to : std::nullopt, said.id},
                            c::row(c::justified(c::hbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY,
                                                               .padding = {f.first ? 8.0f : 1.0f, 0.0f, 1.0f, with_face ? kBubbleAvatar + 8.0f : 0.0f}}),
                                                justify),
                                   std::move(face), bubble_of(n, in, f, now)));
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
  // The plugins' footers under it: who has read up to it, for one.
  auto footers = footers_t::built(footer_facts{colours, in, f.readers});
  const float footer = footers_t::height(footers);
  // Drawn once and played back until something in it changes -- not one
  // with a picture or a file (a loader turns in it while it comes, a picture
  // may move), nor a frosted one (its backdrop is where it is on the screen).
  const bool frosted = frosts(looks.bubbles);
  auto row = c::onRightPressAt(row_menu{n, in.id, said, f.version}, c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY,
                                      .padding = {f.unread_start ? kUnreadBarHeight + 6.0f : 0.0f, 0.0f,
                                                  footer > 0.0f ? footer + 2.0f : 0.0f, 0.0f}}),
                       c::swiped(kSwipeToReply, 120.0f, sends<request::reply_to>{{said.id, said.body.plain}}, swipe_shown{&colours},
                                 std::move(mark), std::move(inner)),
                       std::move(unread), std::move(footers)));
  skiff::compose::state_of(row).setRecorded(!said.attachment && said.album.empty() && !frosted);
  // Not shown where the chat's settings say so: kept, and out of the flow.
  row.setVisible(!said.service || f.events_shown);
  return SKIFF_NAMED(message_row, c::washed_for<message_marks>(colours.accent, 56.0f, 80.0f,
      [id = said.id](const message_marks& marks) {
        return skiff::compose::Wash{.held = marks.previewed ? marks.previewed->contains(id) : marks.selected.contains(id),
                                    .flash = marks.flashed == id ? marks.flash : 0};
      },
      c::appearing(arrives,
                   // Messages being selected: a press on one is the list's --
                   // it selects it or lets it go, or a drag spans several --
                   // not what is pressed in it.
                   c::held_off_for<message_marks>([](const message_marks& marks) { return !marks.selected.empty(); }, std::move(row)))));
}
using message_row_t = decltype(message_row(std::declval<const bubble_needs&>(), std::declval<const conversation&>(),
                                           std::declval<const bubble_facts&>(), nullptr, false));
// A message shown alone, as the chat shows it -- in a list of mentions, an
// edit history, who reacted: its row, first and last of its run as said.
message_row_t message_alone(const ui_needs& n, const conversation& in, const message& said, bool first, bool last,
                                   const model* now, bool version = false) {
  return message_row(bubble_needs{.colours = n.colours, .looks = n.looks, .shared = n.shared, .now = now}, in,
                     bubble_facts{.said = said, .first = first, .last = last, .version = version}, now, false);
}

}  // namespace mux::ui
