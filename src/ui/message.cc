// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:message -- A message: its bubble, its pictures, files, reactions and mentions.
export module mux.ui:message;

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

export import :message_pieces;
export import :message_media;
export import :message_text;

export namespace mux::ui {

// A protocol's own sticker nodes, and its making of one for a message:
// none by default -- the picture.
namespace sticker_defaults {
template <class Actions>
constexpr proto::sticker_view_list<> sticker_views(const auto&, type_tag<Actions>) {
  return {};
}
template <class Actions>
constexpr std::nullopt_t make_sticker(const auto&, const message&, type_tag<Actions>) {
  return std::nullopt;
}
}  // namespace sticker_defaults
template <class State, class Actions>
constexpr auto sticker_views_for(const State& state, type_tag<Actions> tag) {
  using sticker_defaults::sticker_views;
  return sticker_views(state, tag);
}
// And the nodes a protocol shows of a message of its own, under its text
// (Telegram's inline buttons, a poll): listed by message_views(state,
// type_tag<Actions>), made for a message by make_message_view -- by its
// part (message::theirs), say. None by default.
namespace view_defaults {
template <class Actions>
constexpr proto::sticker_view_list<> message_views(const auto&, type_tag<Actions>) {
  return {};
}
template <class Actions>
constexpr std::nullopt_t make_message_view(const auto&, const message&, type_tag<Actions>) {
  return std::nullopt;
}
}  // namespace view_defaults
template <class State, class Actions>
constexpr auto message_views_for(const State& state, type_tag<Actions> tag) {
  using view_defaults::message_views;
  return message_views(state, tag);
}

// A message as the chat shows it. A template on the program's Actions, made
// where every protocol's UI module is seen: its sticker part is the basic
// picture or one of a protocol's own sticker nodes (sticker_views).
template <class Actions>
struct message_bubble : nodes::Stack {
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
        type_list<>, typename sticker_nodes<decltype(sticker_views_for(::mux::state_of<Tags>{}, type_tag<Actions>{}))>::type...>::type;
  };
  // Never empty: a text, where no protocol draws its own.
  using their_sticker_t = typename variant_of_types<
      typename joined<type_list<nodes::Text>, typename protocol_sticker_nodes<protocols>::type>::type>::type;
  template <class>
  struct protocol_message_nodes;
  template <class... Tags>
  struct protocol_message_nodes<protocol_list<Tags...>> {
    using type = typename joined<
        type_list<>, typename sticker_nodes<decltype(message_views_for(::mux::state_of<Tags>{}, type_tag<Actions>{}))>::type...>::type;
  };
  using their_view_t = typename variant_of_types<
      typename joined<type_list<nodes::Text>, typename protocol_message_nodes<protocols>::type>::type>::type;
  struct view_holder : nodes::Stack {
    struct parts_t {
      their_view_t shown;
    } parts;
    template <class View>
    explicit view_holder(View made) : parts{.shown = their_view_t(std::move(made))} {
      fState.apply({.fillX = true, .autoSize = scene::axes::kY});
    }
  };
  // As a node: the protocol's sticker in it.
  struct sticker_holder : nodes::Stack {
    struct parts_t {
      their_sticker_t shown;
    } parts;
    template <class View>
    explicit sticker_holder(View made) : parts{.shown = their_sticker_t(std::move(made))} {
      fState.apply({.autoSize = scene::axes::kBoth});
    }
  };
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
  struct quote_row : nodes::Stack {
    // Who said it, and "quoted" after the name -- thin and grey, at the
    // right: what it shows is the part the reply quoted, not the message's
    // text. In the line's flow, so the name is cut before it rather than
    // drawn under it, and the quote is at least as wide as both.
    struct who_row : nodes::Stack {
      struct parts_t {
        nodes::Text who;
        std::optional<nodes::Text> tag;
      } parts;
      who_row(const palette& colours, skia::SkColor colour, std::string name, bool quoted)
          : parts{.who = nodes::Text(std::move(name), 13.0f, colour, true)} {
        auto& [who, tag] = parts;
        this->setHorizontal();
        this->setGap(8.0f);
        fState.apply({.fillX = true, .autoSize = scene::axes::kY});
        who.setElided(true);
        who.apply({.grow = scene::axes::kX});
        if (quoted) {
          tag.emplace("quoted", 11.0f, colours.dim);
          tag->apply({.alignSelf = scene::align::kStart, .margin = {1.0f, 0.0f, 0.0f, 0.0f}});
        }
      }
    };
    // Who said it over a line of it, each cut at the bubble's width.
    struct said_column : nodes::Stack {
      struct parts_t {
        who_row who;
        nodes::Text said;
      } parts;
      said_column(const palette& colours, skia::SkColor colour, std::string name, std::string line, bool quoted)
          : parts{.who = who_row(colours, colour, std::move(name), quoted),
                  .said = nodes::Text(std::move(line), 13.0f, colours.text)} {
        auto& [who, said] = parts;
        fState.apply({.autoSize = scene::axes::kY, .grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
        // As wide as the quote, cut where it ends: the quote is as wide as
        // its bubble.
        said.setElided(true);
        said.apply({.fillX = true});
      }
    };
    struct parts_t {
      nodes::Box<> bar;
      // A picture quoted: its thumbnail.
      std::optional<nodes::Image<from_thumbnails>> thumb;
      said_column texts;
    } parts;
    quote_row(const palette& colours, skia::SkColor colour, std::string who, std::string said,
              std::optional<std::string> picture = std::nullopt, bool quoted = false)
        : parts{.bar = nodes::Box<>(with_alpha(colour, 0.9f)),
                .texts = said_column(colours, colour, std::move(who), std::move(said), quoted)} {
      auto& [bar, thumb, texts] = parts;
      this->setHorizontal();
      this->setGap(4.0f);
      fState.apply({.fillX = true,
                    .autoSize = scene::axes::kY,
                    .margin = {2.0f, 0.0f, 4.0f, 0.0f},
                    .padding = {2.0f, 6.0f, 2.0f, picture ? 7.0f : 11.0f},
                    .cornerRadius = 5.0f,
                    .background = with_alpha(colour, 0.12f),
                    .masking = true});
      bar.apply({.place = scene::anchor::kTopLeft, .x = picture ? -7.0f : -11.0f, .y = -2.0f, .fillY = true, .width = 3.0f});
      if (picture) {
        thumb.emplace(from_thumbnails{*picture});
        thumb->apply({.width = 32.0f, .height = 32.0f, .alignSelf = scene::align::kMiddle,
                      .margin = {2.0f, 0.0f, 2.0f, 0.0f}, .cornerRadius = 3.0f, .background = colours.tile});
      }
    }
  };
  // The bubble: as wide as what it says, up to its largest.
  struct body_column : nodes::Stack {
    bool outgoing = false;
    // The sender's name over their run, in their colour, and after it their
    // role in the room, dim, as Telegram shows "admin".
    struct name_row : nodes::Stack {
      struct parts_t {
        nodes::Text name;
        nodes::Text role;
      } parts;
      name_row(const palette& colours, std::string who, skia::SkColor colour, std::string role)
          : parts{.name = nodes::Text(std::move(who), 13.0f, colour, true),
                  .role = nodes::Text(std::move(role), 12.0f, colours.dim)} {
        this->setHorizontal();
        this->setGap(10.0f);
        fState.apply({.autoSize = scene::axes::kBoth});
        // Sized as the name alone was: cut where it passes the bubble's
        // widest, the role's room kept.
        parts.name.setElided(true);
        parts.name.setMaxWidth(kMaxWidth - 60.0f);
        parts.role.setVisible(!parts.role.text().empty());
        parts.role.apply({.alignSelf = scene::align::kEnd});
      }
    };
    struct parts_t {
      // Frosted: what is behind, blurred, under all of it.
      std::optional<frost_pane> frost;
      std::optional<name_row> name;
      // Forwarded: from whom, in the accent, as Telegram's.
      std::optional<forward_line> forwarded;
      std::optional<quote_row> quote;
      std::optional<picture_view> picture;
      // A sticker its protocol draws itself, in the picture's place.
      std::optional<sticker_holder> their_sticker;
      std::optional<album_view> album;
      std::optional<file_view> file;
      nodes::BasicText<message_pictures> text;
      // Its blocks of code, each with the words after it.
      std::vector<code_piece> blocks;
      std::vector<link_card> cards;
      std::optional<page_preview> preview;
      std::optional<reaction_row> reactions;
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
      // The last of a run's tail, as Telegram's: out of the flow, at the
      // corner on the sender's side, in the bubble's colour.
      std::optional<nodes::Icon> tail;
    } parts;
    skia::SkColor plate = 0;
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
      parts.their_sticker.emplace(std::move(*made));
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
      auto& [frost, name, forwarded, quote, picture, their_sticker, album, file, text, blocks, cards, preview, reactions, thread, protocol_lines, their_view, time, inline_time, tail] = parts;
      // A sticker's time is over it, and nowhere else: placed beside its
      // reactions too, it was shown twice.
      if (picture && picture->sticker) {
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
      if (text.visible() && (words.empty() || ends_quoted) && !reactions) {
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
      if (!blocks.empty() || !cards.empty() || preview || (!text.visible() && !reactions)) {
        time_placed = true;  // under it, as it is
        return;
      }
      skia::SkFont* font = skiff::paint::defaultFont();
      if (font == nullptr)
        return;
      if (!reactions && text.bounds().isEmpty()) {
        this->guess_time(*font);
        return;
      }
      // What the time goes beside: the reactions where there are some, as
      // Telegram puts it on their line, else the text's last line.
      const skia::SkRect last = reactions ? rethis->send(::mux::ui::request::bounds{}) : text.bounds();
      if (last.isEmpty())
        return;
      time_placed = true;
      float last_width = text.lastLineWidth();
      if (reactions)
        last_width = rethis->send(::mux::ui::request::chips{}).empty() ? 0.0f : rethis->send(::mux::ui::request::chips{}).back().bounds().fRight - last.fLeft;
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
        this->invalidateLayout();
      }
      // On the last line: its bottom where the text's is, wherever the text
      // ends in the bubble -- anchored to the bubble's bottom alone, it stood
      // above the line it is beside.
      if (inside) {
        const float drop = last.fBottom - fState.contentBox().fBottom;
        if (std::abs(drop - time_drop) > 0.25f) {
          time_drop = drop;
          inline_time.apply({.y = drop + kTimeLower});
          this->invalidateLayout();
        }
      }
    }
    float time_drop = 0.0f;
    skia::SkRect placed_beside = skia::SkRect::MakeEmpty();
    float placed_width = -1.0f;
    std::string placed_time;
    // The tail: 10 by 12, its straight side on the bubble's edge, curving
    // down and out to its tip at the bubble's bottom.
    static IconShape tail_shape(bool mine) {
      const float side = mine ? -5.0f : 5.0f, tip = -side;
      return {{{marks::path{{steps::move{side + (mine ? -1.0f : 1.0f), -6.0f}, steps::line{side, -6.0f},
                             steps::cubic{side, 1.0f, side * 0.2f, 5.0f, tip, 6.0f},
                             steps::line{side + (mine ? -1.0f : 1.0f), 6.0f}, steps::close{}}},
                0.0f, true}}};
    }
    void grow_tail(bool mine) {
      const scene::Corners squared = mine ? scene::Corners{12.0f, 12.0f, 0.0f, 12.0f} : scene::Corners{12.0f, 12.0f, 12.0f, 0.0f};
      // The bubble's own fill and its tail one shape (skiff's Tail): a
      // see-through bubble is so once, with no seam -- a tail of its own
      // over the bubble's edge showed both through, darker where they met.
      if (!parts.frost) {
        fState.apply({.corners = squared,
                      .tail = scene::Tail{.side = mine ? scene::TailSide{scene::tail_side::right{}} : scene::TailSide{scene::tail_side::left{}}}});
        return;
      }
      // Frosted, the fill is the pane's, under the bubble's own: the tail a
      // shape of its own, in the pane's tint.
      parts.tail.emplace(tail_shape(mine), plate);
      parts.tail->apply({.place = mine ? scene::anchor::kBottomRight : scene::anchor::kBottomLeft,
                         .x = mine ? kPadX + 10.0f : -(kPadX + 10.0f),
                         .y = kPadY,
                         .width = 10.0f,
                         .height = 12.0f});
      fState.apply({.corners = squared});
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
      auto& [frost, name, forwarded, quote, picture, their_sticker, album, file, text, blocks, cards, preview, reactions, thread, protocol_lines, their_view, time, inline_time, tail] = parts;
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
                          .corners = fState.fCorners});
    }
    body_column(const palette& colours, const looks_shown& looks, bool mine, std::string said, std::string when)
        : outgoing(mine),
          parts{.text = nodes::BasicText<message_pictures>(std::move(said), 13.0f, colours.text),
                .time = nodes::Text(when, 11.0f, mine ? colours.sent_time : colours.dim),
                .inline_time = nodes::Text(when, 11.0f, mine ? colours.sent_time : colours.dim)},
          plate(plate_of(colours, looks.bubbles, mine)) {
      auto& [frost, name, forwarded, quote, picture, their_sticker, album, file, text, blocks, cards, preview, reactions, thread, protocol_lines, their_view, time, inline_time, tail] = parts;
      this->setGap(2.0f);
      fState.apply({.autoSize = scene::axes::kBoth, .maxWidth = kMaxWidth + 2.0f * kPadX,
                    .padding = {kPadY, kPadX, kPadY, kPadX}, .cornerRadius = 12.0f, .background = plate});
      // Frosted: what is behind blurred under the tint; glass: a light edge.
      spl::visit(spl::overloaded{[&](config::bubbles::frosted) { this->frosted(blur_of(looks.bubbles, looks.window)); },
                                       [&](config::bubbles::glass) {
                                         fState.apply({.border = scene::Border{skia::colorSetARGB(70, 255, 255, 255), 1.0f}});
                                       },
                                       [](const auto&) {}},
                    looks.bubbles.kind);
      text.setWrapped(true);
      text.setShrinksToLines(true);
      // Wrapped at the bubble's width however wide the room it is first
      // measured in: not at the chat's, the bubble then capped narrower
      // than its lines.
      text.apply({.maxWidth = kMaxWidth});
      time.apply({.alignSelf = scene::align::kEnd});
      // Shown once the last line is found to leave room for it.
      inline_time.apply({.place = scene::anchor::kBottomRight, .y = kTimeLower});
      inline_time.setVisible(false);
    }
  };

  // tdesktop's bar over the first unread: its words in the middle of a band
  // the width of the chat.
  struct unread_bar_t : nodes::Stack {
    static constexpr float kHeight = 26.0f;
    struct parts_t {
      nodes::Text label;
    } parts;
    explicit unread_bar_t(const palette& colours) : parts{.label = nodes::Text("Unread messages", 13.0f, colours.dim, true)} {
      fStack.justify = nodes::justify::middle{};
      fState.apply({.place = scene::anchor::kTopLeft, .y = -(kHeight + 4.0f), .fillX = true, .height = kHeight,
                    .background = colours.sidebar});
      parts.label.apply({.alignSelf = scene::align::kMiddle});
    }
  };
  // The colours it is made in: handed down, kept for what it makes later.
  const palette* colours_ = nullptr;
  // The looks shown: the program's.
  const looks_shown* looks_ = nullptr;
  // What the window's parts share: the accounts' protocol states.
  const ui_shared* shared_ = nullptr;
  struct parts_t {
    // The sender's avatar, beside the last of their run in a group; the
    // same room, empty, beside the rest.
    avatar_mark face;
    body_column body;
    // The arrow a swipe shows, filling as it reaches its mark.
    nodes::Icon swipe_mark;
    // Over the first unread message of a chat opened: tdesktop's bar.
    std::optional<unread_bar_t> unread_bar;
    // Under it, where the chat shows them: who has read up to it.
    std::optional<readers_row> readers;
  } parts;
  // Whether it has the bar: kept while the chat's first unread is it.
  // Who has read up to it, as shown: while the same, the bubble is kept.
  std::vector<std::string> readers_shown;
  static constexpr float kReaders = 16.0f;
  void show_readers(const conversation& in, std::vector<std::string> users) {
    readers_shown = std::move(users);
    if (readers_shown.empty())
      return;
    parts.readers.emplace(*colours_, in, readers_shown);
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
    parts.unread_bar.emplace(*colours_);
    // Across the whole list, not the row's content box: held out past the
    // row's own padding (a group's room for the avatar) and the list's sides,
    // which a relative width is measured inside of.
    parts.unread_bar->apply({.margin = {0.0f, -(fState.fPadding.fRight + kListSide), 0.0f,
                                        -(fState.fPadding.fLeft + kListSide)}});
    fState.apply({.padding = {fState.fPadding.fTop + unread_bar_t::kHeight + 6.0f, fState.fPadding.fRight,
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
  message_bubble(const needs& n, const conversation& in, const message& said, bool first_of_run, bool last_of_run, const model* now,
                 bool show_events, bool show_preview, made_t)
      : said(said), first(first_of_run), last(last_of_run), message_id(said.id), plain(said.body.plain),
        outgoing(said.outgoing), sender(said.sender), colours_(n.colours), looks_(n.looks), shared_(n.shared),
        parts{.face = avatar_mark(said.sender, sender_name(in, said.sender), kAvatar),
              .body = body_column(*n.colours, *n.looks, said.outgoing, said.body.plain, mark_of(said) + clock_of(said.at)),
              .swipe_mark = nodes::Icon(shape_of(icon::back{}), n.colours->dim)} {
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
    swipe_mark.apply({.place = scene::anchor::kCentreRight,
                      .x = -6.0f,
                      .width = 28.0f,
                      .height = 28.0f,
                      .cornerRadius = 14.0f,
                      .background = colours_->tile,
                      .alpha = 0.0f});
    this->setHorizontal();
    this->setGap(8.0f);
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
    face.apply({.place = scene::anchor::kBottomLeft, .x = -(kAvatar + 8.0f), .y = -1.0f});
    if (!(group && !outgoing && last_of_run))
      face.fState.setAlpha(0.0f);  // its room kept, so the run's bubbles line up
    else
      face.fState.setAlpha(static_cast<float>(element_opacity_of(looks_->bubbles, &config::element_opacity::avatars)) / 100.0f);
    if (((group && !outgoing) || as_lines) && first_of_run && !said.service) {
      // Their role, as the chat's protocol says it (Matrix: its power levels).
      body.parts.name.emplace(*colours_, sender_name(in, said.sender), avatar_colour(said.sender),
                              proto::sender_role(protocol_state_of(*shared_, in.id.account), in, said.sender));
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
      body.parts.forwarded.emplace(*colours_, std::move(shown.text), std::move(shown.links), outgoing ? colours_->sent_time : colours_->accent,
                                   link ? said.forwarded->from : std::string());
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
                                              return body.place_sticker(make_sticker(now, said, type_tag<Actions>{}));
                                            },
                                            protocol_state_of(*shared_, in.id.account));
    // What it carries: a picture, sized as tdesktop's; or a file's row.
    if (said.attachment && !theirs) {
      const mux::attachment& carried = *said.attachment;
      spl::visit(spl::overloaded{[&](attachment_kind::image) {
                              body.parts.picture.emplace(*colours_, carried.source, carried.width, carried.height);
                              if (carried.video)
                                body.parts.picture->show_video(carried.duration_ms, carried.video != carried.source);
                            },
                            [&](attachment_kind::file) {
                              body.parts.file.emplace(*colours_, n.sound, carried.source, carried.name, carried.size,
                                                      audio_type(carried.mimetype, carried.name));
                            }},
                 carried.kind);
      // A sticker: on nothing -- no bubble, no padding -- its time over it.
      if (said.sticker && body.parts.picture) {
        body.parts.picture->as_sticker();
        body.parts.text.setVisible(false);
        // Its time as tdesktop's (UnwrappedMedia, calculateFullRight): one
        // sent, at its bottom right corner, over it; one that came, beside
        // it at the bottom, to its right, on a plate of its own.
        if (said.outgoing) {
          body.parts.picture->show_time(when);
          body.parts.time.setVisible(false);
        } else {
          // Shown while the pointer is over the message, as tdesktop's
          // needInfoDisplay (isUnderCursor): skiff shows and hides it.
          body.parts.time.apply({.place = scene::anchor::kBottomRight, .origin = scene::anchor::kBottomLeft, .x = 6.0f,
                                 .padding = {2.0f, 6.0f, 2.0f, 6.0f}, .cornerRadius = 8.0f, .background = body.plate,
                                 .visible = false, .revealOnHover = true});
        }
        body.parts.frost.reset();
        body.apply({.padding = {0.0f, 0.0f, 0.0f, 0.0f}, .background = skia::SkColor{0},
                    .border = scene::Border{skia::SkColor{0}, 0.0f}});
      } else if (said.body.plain.empty() && !said.body.html) {
        // No caption: the text goes, and a picture has its time over it.
        body.parts.text.setVisible(false);
        if (body.parts.picture) {
          body.parts.picture->show_time(when);
          body.parts.time.setVisible(false);
          body.apply({.padding = {3.0f, 3.0f, 3.0f, 3.0f}});
          body.sync_frost();
        }
      }
    }
    // A gallery: its pictures as an album, its body the caption under it.
    if (!said.album.empty())
      body.parts.album.emplace(*colours_, said.album);
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
    mentioned shown;
    if (said.body.html) {
      auto read = read_html(*said.body.html);
      shown = with_mentions(std::move(read.text), std::move(read.spans), in, now, std::move(read.styles));
    } else {
      shown = with_mentions(said.body.plain, link_spans_in(said.body.plain), in, now);
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
      // Cut at its blocks of code: the first words here, each block with the
      // words after it below.
      const std::vector<text_piece> pieces = pieces_of(shown.text, shown.links, shown.styles);
      body.parts.text.setText(pieces.front().text);
      body.parts.text.setLinks(pieces.front().links, colours_->accent);
      body.parts.text.setStyles(pieces.front().styles, quote_colour);
      body.parts.text.setVisible(!pieces.front().text.empty());
      for (std::size_t i = 1; i + 1 < pieces.size(); i += 2)
        body.parts.blocks.emplace_back(*colours_, pieces[i], &pieces[i + 1], quote_colour, quote_colour, colours_->text);
      for (const auto& [url, room] : shown.cards)
        body.parts.cards.push_back(card_of(*colours_, url, room, now));
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
        body.parts.preview.emplace(*colours_, found->second, *link);
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
      std::ranges::replace(line, '\n', ' ');
      const bool with_picture = picture.has_value();
      body.parts.quote.emplace(*colours_, known ? avatar_colour(found->sender) : colours_->accent,
                         known ? (found->outgoing ? std::string("You") : sender_name(in, found->sender))
                               : std::string("A message"),
                         std::move(line), header_quote ? std::nullopt : std::move(picture), header_quote.has_value());
      // The quote spans its bubble, as tdesktop's; the bubble is at least as
      // wide as the quote asks -- its name and its line, the line counted up
      // to maxSignatureSize (240), so that a short answer to a long message
      // does not stretch its bubble to the full width.
      float asks = 160.0f;
      if (skia::SkFont* font = skiff::paint::defaultFont()) {
        skiff::paint::Painter measure(nullptr, *font);
        const auto& texts = body.parts.quote->parts.texts.parts;
        // The name line with "quoted" after it, whole: the bubble widened for
        // the tag, rather than the tag drawn over the name.
        const auto& tag = texts.who.parts.tag;
        const float who = measure.measure(texts.who.parts.who.text(), 13.0f) +
                          (tag ? 8.0f + std::ceil(measure.measure(tag->text(), 11.0f)) : 0.0f);
        const float words = std::max(who, std::min(measure.measure(texts.said.text(), 13.0f), kReplyLineMax));
        const float around = (with_picture ? 7.0f + 32.0f + 4.0f : 11.0f) + 6.0f + 2.0f * kPadX;
        asks = std::min(std::ceil(words) + around, kMaxWidth + 2.0f * kPadX);
      }
      body.base_min = asks;
      body.apply({.minWidth = asks});
    }
    // A sticker's: no bubble under it, so what is over it -- the sender's
    // name, a forward's line, the quote of what it answers -- each on a small
    // plate of its own, as Telegram's; they stood on the wallpaper.
    if (said.sticker && body.parts.picture) {
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
          body.place_view(make_message_view(now, said, type_tag<Actions>{}));
        },
        protocol_state_of(*shared_, in.id.account));
    if (!said.reactions.empty()) {
      body.parts.reactions.emplace();
      for (const auto& [key, who] : said.reactions)
        if (!who.empty())
          body.parts.rethis->send(::mux::ui::request::chips{}).emplace_back(*colours_, *looks_, key, who.size(), who.contains(said.in.account.address), [&] {
            std::vector<std::pair<std::string, std::string>> people;
            for (const std::string& one : who)
              people.emplace_back(one, sender_name(in, one));
            return people;
          }());
    }
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

}  // namespace mux::ui
