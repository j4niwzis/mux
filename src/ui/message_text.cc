// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:message_text -- A message's text as shown: its mentions and quotes, its code, its forward line and readers.
export module mux.ui:message_text;

import std;
import chevron.escape;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes.box;
import skiff.nodes.flow;
import skiff.nodes.icon;
import skiff.nodes.image;
import skiff.nodes.text;
import skiff.widgets.loader;
import skiff.widgets.pill;
import mux.platform.audio;
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
import :html;
import :message_pieces;
import :message_media;

export namespace mux::ui {
// Mentions, as pills: Matrix IDs in a message's text -- #alias:server,
// !room:server, @user:server -- and its HTML's matrix.to links to them, each
// drawn as a pill with a small avatar and the name it goes by here (a
// room's name, a member's), a link to it. The text is given back with the
// names in place of the IDs, and its links with it.
// Rooms not joined here whose server said they are there (model::
// rooms_found) are shown as pills; one not said to be is plain text.
struct mentioned;
[[nodiscard]] inline std::optional<std::string> take_opening_quote(mentioned& shown);
// A room event said before its people were pills -- kept so, read back
// from the disk -- with who did it as a person: its line starts with their
// name in the chat. A Matrix one's: a person's link is a matrix.to one.
[[nodiscard]] inline message with_actor(const conversation& in, const message& said) {
  if (!said.service || said.body.html)
    return said;
  // The person's link, where their protocol has one.
  const auto link = proto::person_link(state_before(protocol_of(said.sender)), said.sender);
  if (!link)
    return said;
  const std::string name = sender_name(in, said.sender);
  if (name.empty() || !said.body.plain.starts_with(name))
    return said;
  message out = said;
  out.body.html = std::format(R"(<a href="{}">{}</a>)", chevron::escaped(*link), chevron::escaped(name)) +
                  chevron::escaped(std::string_view(said.body.plain).substr(name.size()));
  return out;
}
struct mentioned {
  // Rooms named in it whose picture is still to come, and rooms not known
  // to be there: made again when the one comes or the other is found.
  std::vector<std::string> waiting;
  std::vector<std::string> unknown;
  std::string text;
  std::vector<nodes::Text::Link> links;
  std::vector<std::pair<std::string, logic::link_t>> cards;  // the link, and the place it is to
  std::vector<nodes::Text::Styled> styles;
};
[[nodiscard]] inline mentioned with_mentions(std::string text, std::vector<nodes::Text::Link> links,
                                             const conversation& in, const model* now,
                                             std::vector<nodes::Text::Styled> styles = {}) {
  // What a mention is called here, and what it links to: a person by their
  // name in the chat, a room by its name where it is known.
  const auto name_of = [&](const logic::link_t& what) -> std::pair<std::string, std::string> {
    return spl::visit(spl::overloaded{[&](const logic::mention::person& one) { return std::pair(sender_name(in, one.id), one.id); },
                                 [&](const logic::mention::place& one) {
                                   if (now)
                                     if (const auto chat = logic::chat_of(*now, what))
                                       if (const conversation* found = now->find(*chat))
                                         return std::pair(display_name(*found), found->id.id);
                                   // Not joined, but its server named it.
                                   if (now)
                                     if (const auto named = now->rooms_found.find(one.id);
                                         named != now->rooms_found.end() && !named->second.empty())
                                       return std::pair(named->second, one.id);
                                   return std::pair(one.id, one.id);
                                 },
                                 [](logic::mention::kept) { return std::pair(std::string(), std::string()); }},
                      logic::mention_in(what));
  };
  // A word that is an ID, as some protocol writes one (a Matrix ID: a sigil,
  // a name, a colon, a server).
  const auto id_in = [](std::string_view word) -> std::optional<logic::link_t> { return logic::link_of_id(word); };
  // What in the text is replaced: by a pill, or by nothing where it is
  // shown as a card instead.
  struct replaced {
    std::size_t first, last;
    std::optional<logic::link_t> pill;
    // Shown as written, not by its name: a room's address as the message
    // has it -- a pill only where the room is known.
    std::optional<std::string> as_written = std::nullopt;
  };
  std::vector<replaced> spans;
  std::vector<nodes::Text::Link> kept;
  mentioned out;
  for (auto& link : links) {
    const auto what = logic::link_of(link.target);
    const std::string_view label = std::string_view(text).substr(link.first, link.last - link.first);
    const bool bare = label == link.target;  // the URL itself, not words over it
    if (!what) {
      kept.push_back(std::move(link));
      continue;
    }
    // A person: a pill. A place named by words over it: a pill; given as
    // its URL, or a message in it: a card, the URL out of the text. One
    // kept: a link as it is.
    spl::visit(spl::overloaded{[&](const logic::mention::person&) { spans.push_back({link.first, link.last, what}); },
                          [&](const logic::mention::place& one) {
                            if (bare || one.event) {
                              spans.push_back({link.first, link.last, std::nullopt});
                              out.cards.emplace_back(link.target, *what);
                            } else {
                              spans.push_back({link.first, link.last, what});
                            }
                          },
                          [&](logic::mention::kept) { kept.push_back(link); }},
               logic::mention_in(*what));
  }
  for (std::size_t at = 0; at < text.size();) {
    const bool starts = at == 0 || std::string_view(" \n\t(").contains(text[at - 1]);
    if (starts) {
      std::size_t end = at;
      while (end < text.size() && !std::string_view(" \n\t,;)").contains(text[end]))
        ++end;
      while (end > at && std::string_view(".!?").contains(text[end - 1]))
        --end;
      const bool inside = std::ranges::any_of(spans, [&](const replaced& p) { return at < p.last && end > p.first; }) ||
                          std::ranges::any_of(kept, [&](const auto& l) { return at < l.last && end > l.first; });
      if (end > at && !inside) {
        if (auto what = id_in(std::string_view(text).substr(at, end - at))) {
          spans.push_back({at, end, std::move(what), std::string(text.substr(at, end - at))});
          at = end;
          continue;
        }
      }
    }
    ++at;
  }
  // Replaced from the end, so what comes before keeps its place; links
  // after a replaced stretch move with it.
  std::ranges::sort(spans, std::ranges::greater{}, &replaced::first);
  for (const replaced& span : spans) {
    std::string shown;
    std::optional<nodes::Text::Link> pill;
    const bool person = span.pill && spl::visit(spl::overloaded{[](const logic::mention::person&) { return true; },
                                                           [](const auto&) { return false; }},
                                                logic::mention_in(*span.pill));
    // What a pill to a place opens: as its protocol says.
    const std::string opens = !span.pill ? std::string()
                                         : spl::visit(spl::overloaded{[](const logic::mention::place& one) { return one.link; },
                                                                            [](const logic::mention::person& one) {
                                                                              return proto::person_link(state_before(protocol_of(one.id)), one.id)
                                                                                  .value_or(std::string());
                                                                            },
                                                                            [](logic::mention::kept) { return std::string(); }},
                                                         logic::mention_in(*span.pill));
    if (span.pill && !person) {
      // A room: a pill where it is joined here, or its server said it is
      // there -- its picture in it only where it has a real one; a room
      // not known to be there is its text alone, nothing drawn for it.
      // Written as an address, it stays as written; named by words over a
      // link, by its name.
      const bool known = now && logic::chat_of(*now, *span.pill).has_value();
      const auto [name, target] = name_of(*span.pill);
      const bool found = known || (now && now->rooms_found.contains(target));
      const bool pictured = avatar_images().has(target);
      const std::string words = span.as_written ? *span.as_written
                                                : std::string(text.substr(span.first, span.last - span.first));
      if (!found) {
        out.unknown.push_back(target);
        shown = words;
        if (!span.as_written)
          out.links.push_back(nodes::Text::Link{span.first, span.first + shown.size(), opens});
      } else {
        if (!pictured)
          out.waiting.push_back(target);
        // An address written in the text stays as written -- but an ID
        // its protocol says is no name (a Matrix room's !id): by the name.
        const bool by_address =
            span.as_written && proto::id_reads_as_name(state_before(protocol_of(*span.as_written)), *span.as_written);
        shown = (pictured ? std::string("\u2002\u2002") : std::string()) + (by_address ? words : name);
        pill = nodes::Text::Link{span.first, span.first + shown.size(), opens, true};
      }
    } else if (span.pill) {
      const auto [name, target] = name_of(*span.pill);
      shown = "\u2002\u2002" + name;  // room for its avatar
      pill = nodes::Text::Link{span.first, span.first + shown.size(), opens, true};
    }
    const std::ptrdiff_t grew =
        static_cast<std::ptrdiff_t>(shown.size()) - static_cast<std::ptrdiff_t>(span.last - span.first);
    text.replace(span.first, span.last - span.first, shown);
    for (auto* list : {&kept, &out.links})
      for (auto& link : *list)
        if (link.first >= span.last) {
          link.first = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(link.first) + grew);
          link.last = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(link.last) + grew);
        }
    // And the styles: moved where they are after it, grown or shrunk where
    // it is inside them.
    for (auto& style : styles) {
      if (style.first >= span.last)
        style.first = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(style.first) + grew);
      if (style.last >= span.last)
        style.last = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(style.last) + grew);
    }
    if (pill)
      out.links.push_back(std::move(*pill));
  }
  for (auto& link : kept)
    out.links.push_back(std::move(link));
  // What is left of the text: without the space a card's link stood in.
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
    text.pop_back();
  for (auto& style : styles)
    style.last = std::min(style.last, text.size());
  std::erase_if(styles, [](const auto& style) { return style.first >= style.last; });
  out.styles = std::move(styles);
  out.text = std::move(text);
  return out;
}

// A text with every run of spaces, tabs and newlines one space, and none at
// either end: how a quote and what it quotes are compared.
[[nodiscard]] inline std::string squeezed(std::string_view given) {
  // Without the room a pill's avatar takes in the text (two en spaces).
  std::string text(given);
  for (std::size_t at = text.find("\u2002"); at != std::string::npos; at = text.find("\u2002", at))
    text.erase(at, std::string_view("\u2002").size());
  const auto blank = [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; };
  std::string out = std::ranges::to<std::string>(std::views::join(std::views::transform(std::views::chunk_by(text, [&](char a, char b) { return blank(a) == blank(b); }), [&](auto run) { return blank(run.front()) ? std::string(" ") : std::string(run.begin(), run.end()); })));
  const auto first = out.find_first_not_of(' ');
  const auto last = out.find_last_not_of(' ');
  return first == std::string::npos ? std::string() : out.substr(first, last - first + 1);
}

// The quote a text opens with, where it is its only one: taken out of the
// text -- with the spaces after it -- its links and styles moved back with
// what is left; what it said, trimmed. Nothing where the text does not
// open with a quote, or has another.
[[nodiscard]] inline std::optional<std::string> take_opening_quote(mentioned& shown) {
  std::size_t end = 0;
  bool opens = false;
  for (const auto& one : shown.styles) {
    if (!one.quote)
      continue;
    if (one.first == 0 || (opens && one.first <= end)) {
      opens = true;
      end = std::max(end, one.last);
    }
  }
  if (!opens || end == 0)
    return std::nullopt;
  for (const auto& one : shown.styles)
    if (one.quote && one.first > end)
      return std::nullopt;  // another quote after it
  std::string quoted = shown.text.substr(0, end);
  std::size_t cut = end;
  while (cut < shown.text.size() && std::isspace(static_cast<unsigned char>(shown.text[cut])))
    ++cut;
  shown.text.erase(0, cut);
  const auto moved = [cut](auto& spans) {
    std::erase_if(spans, [cut](const auto& one) { return one.last <= cut; });
    for (auto& one : spans) {
      one.first = one.first > cut ? one.first - cut : 0;
      one.last -= cut;
    }
  };
  moved(shown.links);
  moved(shown.styles);
  while (!quoted.empty() && std::isspace(static_cast<unsigned char>(quoted.back())))
    quoted.pop_back();
  std::ranges::replace(quoted, '\n', ' ');
  return quoted;
}

// A message's text as a quote's one line shows it: its HTML read, and its
// mentions -- people, rooms -- by their names, as in the message itself,
// not the raw addresses; without the room a pill's avatar takes.
[[nodiscard]] inline std::string quote_line_of(const message& said, const conversation& in, const model* now) {
  mentioned shown;
  if (said.body.html) {
    auto read = read_html(*said.body.html);
    shown = with_mentions(std::move(read.text), std::move(read.spans), in, now);
  } else {
    shown = with_mentions(said.body.plain, link_spans_in(said.body.plain), in, now);
  }
  std::string out = std::move(shown.text);
  for (std::size_t at = out.find("\u2002\u2002"); at != std::string::npos; at = out.find("\u2002\u2002", at))
    out.erase(at, std::string_view("\u2002\u2002").size());
  return out;
}

// A card for a link to a room, or to a message in one: as the chat it is of
// is known here, or as a room not joined.
[[nodiscard]] inline link_card card_of(const palette& colours, const std::string& url, const logic::link_t& where, const model* now) {
  const logic::mention::place room = spl::visit(
      spl::overloaded{[](const logic::mention::place& one) { return one; }, [](const auto&) { return logic::mention::place{}; }},
      logic::mention_in(where));
  const conversation* chat = nullptr;
  if (now)
    if (const auto found = logic::chat_of(*now, where))
      chat = now->find(*found);
  const std::string name = chat ? display_name(*chat) : room.id;
  const std::string id = chat ? chat->id.id : room.id;
  if (!room.event) {
    const std::string what =
        chat ? (chat->member_count > 0 ? std::format("Room · {} members", chat->member_count) : std::string("Room"))
             : std::string("Room · not joined");
    return link_card(colours, url, id, name, name, what);
  }
  std::string said = "A message";
  if (chat)
    if (const auto it = std::ranges::find(chat->timeline, *room.event, &message::id); it != chat->timeline.end()) {
      said = (it->outgoing ? std::string("You") : sender_name(*chat, it->sender)) + ": " + it->body.plain;
      std::ranges::replace(said, '\n', ' ');
    }
  return link_card(colours, url, id, name, "Message from " + name, said);
}


// What a message's text draws of the program's: a pill's avatar -- the one
// of what it names, else its initials on its colours -- and a custom
// emoji's picture, fetched as an avatar is. Given to its text as a type.
struct message_pictures {
  static std::optional<skiff::scene::PillPicture> pill(std::string_view target) {
    const auto at = target.find("#/");
    const std::string_view id = at == std::string_view::npos ? target : target.substr(at + 2);
    // A room's -- a place, as its protocol reads the ID: its real picture only.
    const auto link = logic::link_of_id(id);
    const bool place = link && spl::visit(spl::overloaded{[](const logic::mention::place&) { return true; },
                                                                [](const auto&) { return false; }},
                                             logic::mention_in(*link));
    if (place) {
      const skia::Sp<skia::SkImage>* real = avatar_images().find(id);
      if (!real || !*real)
        return std::nullopt;
      return skiff::scene::PillPicture{real, 0, 0, std::string()};
    }
    const auto [top, bottom] = userpic_colours(id);
    return skiff::scene::PillPicture{avatar_images().find(id), top, bottom, initials_of(id)};
  }
  static const skia::Sp<skia::SkImage>* picture(std::string_view target) {
    return avatar_images().find(std::string(target));
  }
};

// A block of code, as Telegram draws one: a rounded plate faint in the
// quote's colour with a bar at its left; over the code, its language in the
// colour and a Copy at the right; the code in the monospace face, wrapped,
// selectable.
struct code_block : nodes::Stack {
  // Copy: the block's code, as it is, onto the clipboard.
  struct copy_mark : nodes::Text {
    std::string code;
    copy_mark(std::string what, skia::SkColor colour) : nodes::Text("Copy", 12.0f, colour, true), code(std::move(what)) {
      fState.setCursor(scene::cursor::hand{});
    }
    [[nodiscard]] bool acceptsInput() const { return true; }
    [[nodiscard]] bool onClick(float, float) {
      skiff::scene::setClipboardText(code);
      return true;
    }
  };
  struct head_row : nodes::Stack {
    struct parts_t {
      nodes::Text language;
      copy_mark copy;
    } parts;
    head_row(std::string language, std::string code, skia::SkColor colour)
        : parts{.language = nodes::Text(language.empty() ? std::string("Code") : std::move(language), 12.0f, colour, true),
                .copy = copy_mark(std::move(code), colour)} {
      this->setHorizontal();
      fState.apply({.fillX = true, .autoSize = scene::axes::kY});
      parts.language.setElided(true);
      parts.language.apply({.grow = scene::axes::kX});
    }
  };
  struct parts_t {
    nodes::Box<> bar;
    head_row head;
    nodes::BasicText<message_pictures> code;
  } parts;
  code_block(const palette& colours, std::string code, std::string language, skia::SkColor colour, skia::SkColor text)
      : parts{.bar = nodes::Box<>(colour),
              .head = head_row(std::move(language), code, colour),
              .code = nodes::BasicText<message_pictures>(code, 13.0f, text)} {
    this->setGap(4.0f);
    // As wide as its code (and its head), within the bubble: filling a
    // bubble that takes its width from what it holds, neither said a width,
    // and a short block was squeezed to a few letters a line.
    fState.apply({.autoSize = scene::axes::kBoth, .minWidth = 120.0f, .margin = {4.0f, 0.0f, 4.0f, 0.0f},
                  .padding = {6.0f, 8.0f, 6.0f, 12.0f}, .cornerRadius = 5.0f,
                  .background = (colour & 0x00FFFFFFu) | (0x1Fu << 24), .masking = true});
    parts.bar.apply({.place = scene::anchor::kTopLeft, .x = -12.0f, .y = -6.0f, .fillY = true, .width = 3.0f});
    parts.code.setMonospace(true);
    parts.code.setWrapped(true);
    parts.code.setSelectable(true);
    parts.code.setSelectionColour((colours.accent & 0x00FFFFFFu) | (110u << 24));
    parts.code.setShrinksToLines(true);
  }
};
// A message's text cut at its blocks of code: words, a block, words...
struct text_piece {
  bool code = false;
  std::string text;
  std::string language;
  std::vector<nodes::Text::Link> links;
  std::vector<nodes::Text::Styled> styles;
};
[[nodiscard]] inline std::vector<text_piece> pieces_of(const std::string& text, const std::vector<nodes::Text::Link>& links,
                                                      const std::vector<nodes::Text::Styled>& styles) {
  std::vector<nodes::Text::Styled> blocks =
      std::ranges::to<std::vector>(std::views::filter(styles, [](const nodes::Text::Styled& one) { return one.block; }));
  std::ranges::sort(blocks, {}, &nodes::Text::Styled::first);
  // Words from `a` to `b`: their links and styles cut to them, counted from
  // their start; the line breaks around a block gone.
  const auto words = [&](std::size_t a, std::size_t b) {
    while (a < b && text[a] == '\n')
      ++a;
    while (b > a && text[b - 1] == '\n')
      --b;
    text_piece out{.text = text.substr(a, b - a)};
    out.links = std::ranges::to<std::vector>(std::views::transform(std::views::filter(links, [&](const auto& one) { return one.first >= a && one.last <= b; }), [&](auto one) {
                  one.first -= a;
                  one.last -= a;
                  return one;
                }));
    out.styles = std::ranges::to<std::vector>(std::views::transform(std::views::filter(styles, [&](const auto& one) { return !one.block && one.last > a && one.first < b; }), [&](auto one) {
                   one.first = std::max(one.first, a) - a;
                   one.last = std::min(one.last, b) - a;
                   return one;
                 }));
    return out;
  };
  std::vector<text_piece> out;
  std::size_t at = 0;
  for (const nodes::Text::Styled& block : blocks) {
    if (block.first < at)
      continue;
    out.push_back(words(at, block.first));
    out.push_back(text_piece{.code = true, .text = text.substr(block.first, block.last - block.first), .language = block.language});
    at = block.last;
  }
  out.push_back(words(at, text.size()));
  return out;
}
// After the first words: a block, then the words after it, as many times
// as the text has blocks.
struct code_piece : nodes::Stack {
  struct parts_t {
    code_block block;
    std::optional<nodes::BasicText<message_pictures>> after;
  } parts;
  code_piece(const palette& colours, const text_piece& code, const text_piece* words, skia::SkColor colour, skia::SkColor quote,
             skia::SkColor text)
      : parts{.block = code_block(colours, code.text, code.language, colour, text)} {
    fState.apply({.autoSize = scene::axes::kBoth});
    if (words && !words->text.empty()) {
      parts.after.emplace(words->text, 13.0f, text);
      parts.after->setWrapped(true);
      parts.after->setSelectable(true);
      parts.after->setSelectionColour((colours.accent & 0x00FFFFFFu) | (110u << 24));
      parts.after->setLinks(words->links, colours.accent);
      parts.after->setStyles(words->styles, quote);
      parts.after->setShrinksToLines(true);
    }
  }
};

// A forward's line over its message, as Telegram's: "Forwarded from" and
// the sender -- a person's pill, its avatar drawn by the message's pictures,
// as a mention's -- each a node of its own: the pill pressed opens them,
// the words the original.
struct forward_line : nodes::Stack {
  // Whose picture its pill waits for, where it has none yet: drawn again
  // when it comes.
  std::string from;
  bool had = true;
  struct parts_t {
    nodes::Text label;
    nodes::BasicText<message_pictures> who;
  } parts;
  forward_line(const palette& colours, std::string who, std::vector<nodes::Text::Link> links, skia::SkColor colour,
               std::string sender = {})
      : from(std::move(sender)), parts{.label = nodes::Text("Forwarded from", 13.0f, colour, true),
              .who = nodes::BasicText<message_pictures>(std::move(who), 13.0f, colour)} {
    this->setHorizontal();
    this->setGap(4.0f);
    fState.apply({.autoSize = scene::axes::kBoth});
    parts.label.apply({.alignSelf = scene::align::kMiddle});
    parts.who.setBold(true);
    // Wrapped, as a message text is: one on a single line draws its words
    // plain, its links and pills not at all -- no plate, no picture.
    parts.who.setWrapped(true);
    parts.who.setShrinksToLines(true);
    parts.who.setLinks(std::move(links), colours.accent);
    parts.who.apply({.alignSelf = scene::align::kMiddle});
    had = from.empty() || avatar_images().has(from);
    if (!had)
      avatar_images().waiting.wait(fState.fId);
  }
  // Woken as a picture comes: the pill drawn again where it is theirs.
  void update(double) {
    if (had)
      return;
    if (avatar_images().has(from)) {
      had = true;
      parts.who.markDamaged();
    } else {
      avatar_images().waiting.wait(fState.fId);
    }
  }
};

// Who has read up to a message, as Element shows it: their small faces at
// the row's right under it, three at most and the rest counted.
struct readers_row : nodes::Stack {
  static constexpr float kFace = 14.0f;  // Element's read receipt avatar
  static constexpr std::size_t kMost = 3;
  struct parts_t {
    std::vector<avatar_mark> faces;
    nodes::Text more;
  } parts;
  readers_row(const palette& colours, const conversation& in, const std::vector<std::string>& users)
      : parts{.more = nodes::Text(users.size() > kMost ? std::format("+{}", users.size() - kMost) : std::string(),
                                  10.0f, colours.dim)} {
    this->setHorizontal();
    this->setGap(2.0f);
    fState.apply({.autoSize = scene::axes::kBoth});
    parts.faces.reserve(std::min(users.size(), kMost));
    for (std::size_t i = 0; i < users.size() && i < kMost; ++i)
      parts.faces.emplace_back(users[i], sender_name(in, users[i]), kFace);
    parts.more.setVisible(users.size() > kMost);
    parts.more.apply({.alignSelf = scene::align::kMiddle});
  }
};

}  // namespace mux::ui
