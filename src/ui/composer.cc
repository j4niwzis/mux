// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:composer -- Where a message is written.
export module mux.ui:composer;

import std;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes.box;
import skiff.nodes.flow;
import skiff.nodes.icon;
import skiff.nodes.text;
import skiff.widgets.button;
import skiff.widgets.textarea;
import mux.core;
import mux.config;
import :base;
import :icons;
import :controls;
import :themes;
import :message;

export namespace mux::ui {

// An edge between two parts of the window, to drag: the pointer turns into
// a resize arrow over it, and a drag asks `on_drag(x)` for the edge to be at
// x. It draws a thin line where `with_line`.
template <class OnDrag>
struct drag_edge : scene::Node {
  OnDrag on_drag;
  bool with_line = true;
  bool dragging = false;

  // The thin line down its middle, where it has one.
  struct parts_t {
    nodes::Box<> line;
  } parts{.line = nodes::Box<>(band_colour)};

  explicit drag_edge(OnDrag what, bool line = true) : on_drag(std::move(what)), with_line(line) {
    fState.setCursor(scene::cursor::resize_horizontal{});
    parts.line.apply({.place = scene::anchor::kTopCentre, .fillY = true, .width = 1.0f});
    parts.line.setVisible(line);
  }

  [[nodiscard]] bool acceptsInput() const { return true; }
  [[nodiscard]] bool focusable() const { return false; }

  using Node::onPointer;
  void onPointer(scene::phase::target, const scene::pointer::down&, scene::PointerReply& reply) {
    dragging = true;
    reply.capturePointer();
    reply.handle();
  }
  void onPointer(scene::phase::target, const scene::pointer::move& at, scene::PointerReply& reply) {
    if (!dragging)
      return;
    on_drag(at.x);
    reply.handle();
  }
  void onPointer(scene::phase::target, const scene::pointer::up&, scene::PointerReply& reply) {
    dragging = false;
    reply.releasePointer();
    reply.handle();
  }
  void onPointer(scene::phase::target, const scene::pointer::cancel&, scene::PointerReply& reply) {
    dragging = false;
    reply.releasePointer();
  }
};

// Where an edge was dragged to, asked of the program.
template <class Actions>
struct resize_sidebar_to {
  Actions* actions = nullptr;
  void operator()(float x) const { actions->resize_sidebar(x); }
};
template <class Actions>
struct resize_info_to {
  Actions* actions = nullptr;
  void operator()(float x) const { actions->resize_info(x); }
};

// ---- the message field --------------------------------------------------------------

// A block of code the text opens and does not close -- its closing ``` not
// written: closed at the end, as the field showed it, so that what is sent
// says the block where it ends, as the field did.
[[nodiscard]] inline std::string with_blocks_closed(std::string_view text) {
  const bool open = std::ranges::count_if(text | std::views::split('\n'), [](auto line) {
                      return std::string_view(line.begin(), line.end()).starts_with("```");
                    }) % 2 == 1;
  std::string out(text);
  if (open)
    out += out.ends_with('\n') ? "```" : "\n```";
  return out;
}

// What Enter in the message field does: asks for its text to be sent.
template <class Actions>
struct submit_message {
  Actions* actions = nullptr;
  void operator()(std::string_view text) const { actions->submit_message(with_blocks_closed(text)); }
};


// Where a message is written, across the bottom of a chat as in Telegram
// Quotes in the field, as Telegram's: a paragraph starting "> " -- a "> "
// for each level -- is shown as a quote. Its marks are hidden, its lines
// stand in by 12 for each level with room at the right for the quote's mark,
// and each level lies on a rounded plate of its own, faint in the level's
// colour, with a bar at its left and the mark at its top right: a quote
// inside a quote on both tints. Shift+Enter goes on in the quote, or ends it
// on an empty line; Backspace right after the marks takes a level off;
// Ctrl+Down leaves the quote for what is under it. The field's `Blocks`.
//
// And blocks of code, as Telegram's field shows them while they are written:
// from a line starting ``` to the next such line, on a plate faint in the
// accent with a bar at its left, in the monospace face. Enter inside one
// goes to a new line of it rather than sending; Enter on an opening ```
// line with no closing one after it closes the block, the caret in it.
struct field_quotes {
  static constexpr float kIndent = 12.0f, kRight = 18.0f, kRadius = 5.0f;
  static constexpr float kCodeIndent = 10.0f, kCodeRight = 8.0f;
  // Where a paragraph stands as to code: none; the ``` opening a block; a
  // line inside one; the ``` closing it. A block in a quote is one there:
  // its fences after the quote's marks, its lines at the quote's depth.
  enum class code_line { none, opening, inside, closing };
  // Whether the paragraph at `start` is a fence: ``` after its quote marks.
  [[nodiscard]] static bool fence_at(std::string_view text, std::size_t start) {
    return text.substr(body_at(text, start)).starts_with("```");
  }
  [[nodiscard]] static code_line code_at(std::string_view text, std::size_t start) {
    bool open = false;
    int open_depth = 0;
    for (std::size_t at = 0; at < start;) {
      const std::size_t end = text.find('\n', at);
      if (end == std::string_view::npos || end >= start)
        break;
      if (fence_at(text, at)) {
        if (!open) {
          open = true;
          open_depth = depth(text, at);
        } else if (depth(text, at) == open_depth) {
          open = false;
        }
      }
      at = end + 1;
    }
    const bool fence = fence_at(text, start);
    if (open && depth(text, start) != open_depth)
      return code_line::none;
    if (fence)
      return open ? code_line::closing : code_line::opening;
    return open ? code_line::inside : code_line::none;
  }
  // Where a paragraph's text begins: after its quote marks.
  [[nodiscard]] static std::size_t body_at(std::string_view text, std::size_t start) {
    return start + 2 * static_cast<std::size_t>(depth(text, start));
  }
  // The quote marks of a depth, as a line of it begins.
  [[nodiscard]] static std::string marks_of(int deep) {
    std::string out;
    for (int level = 0; level < deep; ++level)
      out += "> ";
    return out;
  }
  // How long the paragraph at `start` is, up to its newline.
  [[nodiscard]] static std::size_t paragraph_length(std::string_view text, std::size_t start) {
    const std::size_t end = text.find('\n', start);
    return (end == std::string_view::npos ? text.size() : end) - start;
  }
  // The language an opening fence names: what follows its ```, trimmed.
  [[nodiscard]] static std::string_view language_of(std::string_view text, std::size_t start) {
    const std::size_t body = body_at(text, start);
    std::string_view line = text.substr(body, paragraph_length(text, body)).substr(3);
    while (!line.empty() && line.front() == ' ')
      line.remove_prefix(1);
    while (!line.empty() && line.back() == ' ')
      line.remove_suffix(1);
    return line;
  }
  // Whether a block opened at `start` is closed somewhere after it.
  [[nodiscard]] static bool closed_after(std::string_view text, std::size_t start) {
    return closing_after(text, start).has_value();
  }
  // Where the fence closing a block opened at `start` starts: the next one
  // at its depth.
  [[nodiscard]] static std::optional<std::size_t> closing_after(std::string_view text, std::size_t start) {
    const int deep = depth(text, start);
    for (std::size_t at = text.find('\n', start); at != std::string_view::npos; at = text.find('\n', at + 1))
      if (depth(text, at + 1) == deep && fence_at(text, at + 1))
        return at + 1;
    return std::nullopt;
  }
  // How deep a paragraph is in quotes: how many "> " it starts with.
  [[nodiscard]] static int depth(std::string_view text, std::size_t start) {
    int depth = 0;
    while (text.substr(start).starts_with("> ")) {
      ++depth;
      start += 2;
    }
    return depth;
  }
  // Where the paragraph an offset is in starts.
  [[nodiscard]] static std::size_t start_of(std::string_view text, std::size_t at) {
    const std::size_t before = at == 0 ? std::string_view::npos : text.rfind('\n', at - 1);
    return before == std::string_view::npos ? 0 : before + 1;
  }
  // A level's colour: the accent at the first, then Telegram's, as a
  // message's quotes are coloured.
  [[nodiscard]] static skia::SkColor colour(skia::SkColor accent, int level) {
    return skiff::nodes::quoteLevelColour(accent, level + 1);
  }

  [[nodiscard]] static widgets::BlockLook look(std::string_view text, std::size_t start) {
    // A block's fences not seen at all: each line of one hidden whole --
    // ```cpp too -- the opening one the plate's head, where drawBehind puts
    // the block's language (or "Code"), as a message's block shows it; the
    // closing one the plate's foot.
    // In a quote: past its marks and its plates.
    const int deep = depth(text, start);
    const float quoted = kIndent * static_cast<float>(deep);
    const float right = deep > 0 ? kRight : 0.0f;
    switch (code_at(text, start)) {
      case code_line::opening:
        return {.hidden = paragraph_length(text, start), .indent = quoted + kCodeIndent, .right = right + kCodeRight, .monospace = true};
      // Not seen at all, as a sent block has no foot.
      case code_line::closing:
        return {.hidden = paragraph_length(text, start), .indent = quoted + kCodeIndent, .right = right + kCodeRight, .monospace = true,
                .collapsed = true};
      case code_line::inside:
        return {.hidden = 2 * static_cast<std::size_t>(deep), .indent = quoted + kCodeIndent, .right = right + kCodeRight,
                .monospace = true};
      case code_line::none:
        break;
    }
    if (deep == 0)
      return {};
    return {.hidden = 2 * static_cast<std::size_t>(deep), .indent = kIndent * static_cast<float>(deep), .right = kRight};
  }
  static void drawBehind(skia::SkCanvas* canvas, const skiff::paint::Painter& p, std::string_view text,
                         std::span<const widgets::ShownLine> lines, const skia::SkRect& box, const widgets::Theme& theme,
                         float size, float alpha) {
    int deepest = 0;
    for (const widgets::ShownLine& line : lines)
      deepest = std::max(deepest, depth(text, line.paragraph));
    const auto deep = [&](std::size_t i) { return depth(text, lines[i].paragraph); };
    for (int level = 0; level < deepest; ++level) {
      const skia::SkColor tint = colour(theme.fAccent, level);
      // Each run of lines this deep or deeper: one plate.
      for (std::size_t i = 0; i < lines.size();) {
        if (deep(i) <= level) {
          ++i;
          continue;
        }
        std::size_t j = i;
        while (j < lines.size() && deep(j) > level)
          ++j;
        const float left = box.fLeft + static_cast<float>(level) * kIndent;
        const skia::SkRect plate = skia::SkRect::MakeLTRB(left, lines[i].top, box.fRight, lines[j - 1].bottom);
        const int save = canvas->save();
        canvas->clipRRect(skia::SkRRect::MakeRectXY(plate, kRadius, kRadius), true);
        p.fillRect(plate, (tint & 0x00FFFFFFu) | 0x1F000000u, alpha);
        p.fillRect(skia::SkRect::MakeXYWH(left, plate.fTop, 3.0f, plate.height()), tint, alpha);
        canvas->restoreToCount(save);
        p.text("\u201D", box.fRight - kRight + 5.0f, plate.fTop + size, size, tint, alpha);
        i = j;
      }
    }
    // Blocks of code: each run of their lines on one plate, a bar at its left.
    const auto code = [&](std::size_t i) { return code_at(text, lines[i].paragraph) != code_line::none; };
    for (std::size_t i = 0; i < lines.size();) {
      if (!code(i)) {
        ++i;
        continue;
      }
      std::size_t j = i;
      while (j < lines.size() && code(j))
        ++j;
      // At its depth in the quotes it is in, inside their plates.
      const float left = box.fLeft + kIndent * static_cast<float>(depth(text, lines[i].paragraph));
      const float right = box.fRight - (depth(text, lines[i].paragraph) > 0 ? kRight : 0.0f);
      const skia::SkRect plate = skia::SkRect::MakeLTRB(left, lines[i].top, right, lines[j - 1].bottom);
      const int save = canvas->save();
      canvas->clipRRect(skia::SkRRect::MakeRectXY(plate, kRadius, kRadius), true);
      p.fillRect(plate, (theme.fAccent & 0x00FFFFFFu) | 0x1F000000u, alpha);
      p.fillRect(skia::SkRect::MakeXYWH(left, plate.fTop, 3.0f, plate.height()), theme.fAccent, alpha);
      canvas->restoreToCount(save);
      // Its head: the language its opening fence names, small, in the accent.
      if (code_at(text, lines[i].paragraph) == code_line::opening) {
        const std::string_view language = language_of(text, lines[i].paragraph);
        const float label = size * 0.8f;
        p.text(language.empty() ? std::string("Code") : std::string(language), left + kCodeIndent,
               lines[i].top + (lines[i].bottom - lines[i].top + label) * 0.5f - 1.0f, label, theme.fAccent, alpha, true);
      }
      i = j;
    }
  }
  // Typography as it is typed, as Telegram's: "--" an em dash, "<<" and
  // ">>" guillemets -- not in code, nor where a line begins (a quote's
  // marks). Backspace right after one gives back the two it was.
  static constexpr std::array<std::pair<std::string_view, std::string_view>, 3> kTypography{
      {{"--", "\u2014"}, {"<<", "\u00AB"}, {">>", "\u00BB"}}};
  [[nodiscard]] static bool plain_at(std::string_view text, std::size_t caret) {
    const std::size_t start = start_of(text, caret);
    if (code_at(text, start) != code_line::none)
      return false;
    // Inside `code` on its line: an odd number of backticks before it.
    return std::ranges::count(text.substr(start, caret - start), '`') % 2 == 0;
  }
  [[nodiscard]] static std::optional<widgets::TextEdit> typed(std::string_view text, std::size_t caret, std::string_view what) {
    if (what.size() != 1 || caret == 0 || !plain_at(text, caret) || caret - 1 < body_at(text, start_of(text, caret)) + 1)
      return std::nullopt;
    const std::string pair{text[caret - 1], what[0]};
    for (const auto& [two, one] : kTypography)
      if (pair == two)
        return widgets::TextEdit{.from = caret - 1, .to = caret, .with = std::string(one), .caret = caret - 1 + one.size()};
    return std::nullopt;
  }
  [[nodiscard]] static std::optional<widgets::TextEdit> key(std::string_view text, std::size_t caret,
                                                            const scene::key::down& press) {
    namespace keys = scene::keys;
    namespace modifier = scene::modifier;
    if (press.key == keys::kBackspace && !press.modifiers.template has<modifier::control>() && plain_at(text, caret))
      for (const auto& [two, one] : kTypography)
        if (caret >= one.size() && text.substr(caret - one.size(), one.size()) == one)
          return widgets::TextEdit{.from = caret - one.size(), .to = caret, .with = std::string(two), .caret = caret - one.size() + two.size()};
    const std::size_t start = start_of(text, caret);
    // In a block of code -- in a quote, at its depth: Enter is a new line of
    // it, not a send; Enter on an opening ``` with nothing closing it closes
    // it, the caret inside; Ctrl+Down leaves it, for the line under its
    // closing fence -- in the quote it is in, or plain text.
    const code_line in_code = code_at(text, start);
    const std::string fence_marks = marks_of(depth(text, start));
    const auto line_end = [&](std::size_t from) {
      const std::size_t end = text.find('\n', from);
      return end == std::string_view::npos ? text.size() : end;
    };
    if (press.key == keys::kEnter && !press.modifiers.has<modifier::shift>()) {
      if (in_code == code_line::opening && !closed_after(text, start)) {
        const std::size_t end = line_end(caret);
        return widgets::TextEdit{.from = end, .to = end, .with = "\n" + fence_marks + "\n" + fence_marks + "```", .caret = end + 1 + fence_marks.size()};
      }
      if (in_code == code_line::opening || in_code == code_line::inside)
        return widgets::TextEdit{.from = caret, .to = caret, .with = "\n" + fence_marks, .caret = caret + 1 + fence_marks.size()};
    }
    if (press.key == keys::kDown && press.modifiers.has<modifier::control>() && in_code != code_line::none) {
      const std::optional<std::size_t> closing =
          in_code == code_line::closing ? std::optional<std::size_t>(start) : closing_after(text, in_code == code_line::opening ? start : [&] {
            // The block's opening fence, above.
            std::size_t at = start;
            while (at > 0 && code_at(text, at) != code_line::opening)
              at = start_of(text, at - 1);
            return at;
          }());
      if (!closing) {
        // Not closed: closed here, and a line of the quote (or plain) after.
        const std::size_t end = line_end(caret);
        const std::string with = "\n" + fence_marks + "```\n" + fence_marks;
        return widgets::TextEdit{.from = end, .to = end, .with = with, .caret = end + with.size()};
      }
      const std::size_t after = line_end(*closing);
      // The line under it, where it is of the same quote and not code: there.
      if (after < text.size() && depth(text, after + 1) == depth(text, start) && code_at(text, after + 1) == code_line::none)
        return widgets::TextEdit{.from = caret, .to = caret, .with = "", .caret = body_at(text, after + 1)};
      const std::string with = "\n" + fence_marks;
      return widgets::TextEdit{.from = after, .to = after, .with = with, .caret = after + with.size()};
    }
    const int deep = depth(text, start);
    if (deep == 0)
      return std::nullopt;
    const std::size_t marks = start + 2 * static_cast<std::size_t>(deep);
    const bool control = press.modifiers.has<modifier::control>();
    if (press.key == keys::kEnter && press.modifiers.has<modifier::shift>()) {
      // An empty quoted line: the quote ends there.
      if (caret == marks && (caret == text.size() || text[caret] == '\n'))
        return widgets::TextEdit{.from = start, .to = marks, .with = "", .caret = start};
      std::string next = "\n";
      for (int level = 0; level < deep; ++level)
        next += "> ";
      return widgets::TextEdit{.from = caret, .to = caret, .with = next, .caret = caret + next.size()};
    }
    // Right after the marks: a level taken off, the text kept.
    if (press.key == keys::kBackspace && !control && caret == marks)
      return widgets::TextEdit{.from = marks - 2, .to = marks, .with = "", .caret = marks - 2};
    // Out of this quote, one level: to the quote around it (or to plain
    // text, from the outermost) -- the line under it where that is the
    // level, else a new line of that level put right after it.
    if (press.key == keys::kDown && control) {
      const int outer = deep - 1;
      std::size_t end = text.find('\n', caret);
      while (end != std::string_view::npos && depth(text, end + 1) >= deep)
        end = text.find('\n', end + 1);
      if (end != std::string_view::npos && depth(text, end + 1) == outer)
        return widgets::TextEdit{.from = caret, .to = caret, .with = "", .caret = end + 1};
      const std::size_t at = end == std::string_view::npos ? text.size() : end;
      std::string line = "\n";
      for (int level = 0; level < outer; ++level)
        line += "> ";
      return widgets::TextEdit{.from = at, .to = at, .with = line, .caret = at + line.size()};
    }
    return std::nullopt;
  }
};

// Where a message is written: the paperclip, the field growing with what is
// written in it -- quotes and custom emoji shown as they will be sent -- the
// emoji, the arrow. The chat's composer and a thread's; what each
// button does is said by where it is.
template <class Submit, class Attach, class Emoji, class Send>
struct message_input : nodes::Stack {
  using attach_button = icon_button<Attach>;
  using field_t = widgets::TextArea<Submit, message_pictures, field_quotes>;
  using emoji_button = icon_button<Emoji>;
  using send_button = icon_button<Send>;
  struct parts_t {
    attach_button attach;
    field_t field;
    emoji_button emoji;
    send_button send;
  } parts;
  message_input(std::string placeholder, Submit submit, Attach attach_it, Emoji emoji_it, Send send_it)
      : parts{.attach = attach_button(icon::clip{}, std::move(attach_it)),
              .field = field_t(std::move(placeholder), std::move(submit)),
              .emoji = emoji_button(icon::smile{}, std::move(emoji_it)),
              .send = send_button(icon::send{}, std::move(send_it))} {
    auto& [attach, field, emoji, send] = parts;
    emoji.apply({.alignSelf = scene::align::kEnd});
    this->setHorizontal();
    this->setGap(6.0f);
    fState.apply({.fillX = true, .autoSize = scene::axes::kY, .minHeight = 54.0f, .padding = {9.0f, 8.0f, 9.0f, 8.0f}});
    attach.apply({.alignSelf = scene::align::kEnd});
    send.apply({.alignSelf = scene::align::kEnd});
    field.apply({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
    // What is typed looks as it will be sent: the messages' size, as in
    // tdesktop, whose field takes the message font.
    field.setFontSize(13.0f);
    send.set_colour(accent_colour);
  }
};

// Where what is written goes, as a composer is told it: what its ✕, its
// Enter, its paperclip, its emoji and its arrow do, and what its empty field
// says. The chat's own; a thread's is its panel's.
template <class Actions>
struct in_chat {
  using cancel = ask<Actions, &Actions::cancel_compose>;
  using submit = submit_message<Actions>;
  using attach = ask<Actions, &Actions::attach_files>;
  using emoji = ask<Actions, &Actions::toggle_emoji>;
  using send = ask<Actions, &Actions::send_typed>;
  static constexpr std::string_view placeholder = "Write a message…";
};

// Desktop: a line over it, a paperclip on the left, the text growing with
// what is written, and the send arrow on the right. The chat's, and a
// thread's: one composer, told where it writes.
template <class Actions, class Where = in_chat<Actions>>
struct composer_bar : nodes::Stack {
  // What is written answers or edits: the reply bar, its ✕ going back to
  // a plain message.
  using context_row = context_bar<typename Where::cancel>;
  using input_row = message_input<typename Where::submit, typename Where::attach, typename Where::emoji, typename Where::send>;
  // Element's bar over the field while messages here were not sent
  // (RoomStatusBar's): a warning, and "Delete all" and "Retry all".
  struct unsent_row : nodes::Stack {
    using delete_button = widgets::Button<ask<Actions, &Actions::discard_unsent>>;
    using retry_button = widgets::Button<ask<Actions, &Actions::retry_unsent>>;
    struct parts_t {
      nodes::Text said;
      delete_button remove;
      retry_button retry;
    } parts;
    explicit unsent_row(Actions* a)
        : parts{.said = nodes::Text("Some of your messages have not been sent", 13.0f, error_colour),
                .remove = delete_button("Delete all", {a}),
                .retry = retry_button("Retry all", {a})} {
      this->setHorizontal();
      this->setGap(8.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {6.0f, 12.0f, 6.0f, 12.0f}});
      parts.said.setElided(true);
      parts.said.apply({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
      parts.retry.setPrimary(true);
    }
  };
  // Where the reader may not post: a row as high as the input's, its line
  // in the middle -- padded inside it, not by a margin the bar's height
  // leaves out.
  struct no_post_row : nodes::Stack {
    struct parts_t {
      nodes::Text line{"You don't have permission to post in this chat", 13.0f, dim_colour};
    } parts;
    no_post_row() {
      this->setHorizontal();
      fStack.justify = nodes::justify::middle{};
      fState.apply({.fillX = true, .autoSize = scene::axes::kY, .minHeight = 54.0f, .padding = {18.0f, 12.0f, 18.0f, 12.0f}});
      parts.line.apply({.alignSelf = scene::align::kMiddle});
    }
  };
  // A tombstoned room's: "This room has been replaced and is no longer
  // active", and the room it goes on in, opened -- joined, where it is not
  // yet.
  using go_on = ask<Actions, &Actions::open_replacement>;
  struct replaced_row : nodes::Stack {
    struct parts_t {
      nodes::Text line{"This room has been replaced and is no longer active.", 13.0f, dim_colour};
      widgets::Button<go_on> go;
    } parts;
    explicit replaced_row(Actions* a) : parts{.go = widgets::Button<go_on>("The conversation continues here", {a})} {
      this->setHorizontal();
      this->setGap(10.0f);
      fStack.justify = nodes::justify::middle{};
      fState.apply({.fillX = true, .autoSize = scene::axes::kY, .minHeight = 54.0f, .padding = {12.0f, 12.0f, 12.0f, 12.0f}});
      parts.line.apply({.alignSelf = scene::align::kMiddle});
      parts.go.setPrimary(true);
      parts.go.apply({.height = 30.0f, .alignSelf = scene::align::kMiddle});
    }
  };
  // Those asking to join, for those who may let them in: the first
  // of them -- who, and why -- with Approve (an invite) and Deny (their
  // knock refused), and how many more.
  struct approve_it {
    Actions* actions;
    std::string user;
    void operator()() const { actions->room_act(room_action::invite{user}); }
  };
  struct deny_it {
    Actions* actions;
    std::string user;
    void operator()() const { actions->room_act(room_action::kick{user}); }
  };
  struct knock_row : nodes::Stack {
    struct parts_t {
      nodes::Text said;
      widgets::Button<deny_it> deny;
      widgets::Button<approve_it> approve;
    } parts;
    knock_row(Actions* a, const knock_request& one, std::size_t more)
        : parts{.said = nodes::Text(std::format("{} asks to join{}{}", one.name, one.reason.empty() ? std::string() : ": " + one.reason,
                                                more ? std::format(" (and {} more)", more) : std::string()),
                                    13.0f, text_colour),
                .deny = widgets::Button<deny_it>("Deny", {a, one.id}),
                .approve = widgets::Button<approve_it>("Approve", {a, one.id})} {
      this->setHorizontal();
      this->setGap(8.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {6.0f, 12.0f, 6.0f, 12.0f}});
      parts.said.setElided(true);
      parts.said.apply({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
      parts.approve.setPrimary(true);
    }
  };
  struct parts_t {
    nodes::Box<> divider{band_colour};
    std::optional<knock_row> knocks;
    unsent_row unsent;
    context_row context_line;
    input_row input;
    // Where the reader may not post: said in place of the field, as Element
    // says it.
    no_post_row no_post;
    // Upgraded away: Element's notice, and the way to where it goes on.
    replaced_row replaced;
  } parts;
  // The old name, for what reads it.
  typename input_row::field_t& field = parts.input.parts.field;

  // Declared: the divider, the unsent bar, the answer's line where there is
  // one, the row -- or, where the reader may not post, the line saying so.
  explicit composer_bar(Actions* a) : composer_bar(a, {a}, {a}, {a}, {a}, {a}) {}
  composer_bar(Actions* a, typename Where::cancel cancel, typename Where::submit submit, typename Where::attach attach,
               typename Where::emoji emoji, typename Where::send send)
      : parts{.unsent = unsent_row(a),
              .context_line = context_row(std::move(cancel)),
              .input = input_row(std::string(Where::placeholder), std::move(submit), std::move(attach), std::move(emoji), std::move(send)),
              .replaced = replaced_row(a)} {
    parts.unsent.setVisible(false);
    parts.no_post.setVisible(false);
    parts.replaced.setVisible(false);
    parts.context_line.setVisible(false);
    fState.apply({.fillX = true, .autoSize = scene::axes::kY, .background = sidebar_colour});
    parts.divider.apply({.fillX = true, .height = 1.0f});
  }

  // What is in the field, as it holds it: a mention picked, the room its
  // pill's picture takes and the name.
  [[nodiscard]] const std::string& text() const { return parts.input.parts.field.text(); }
  // What is written, as it is sent and kept: each mention by its name.
  [[nodiscard]] std::string plain() const { return parts.input.parts.field.plainText(); }
  // Who is mentioned in it: its pills, as they are now.
  [[nodiscard]] std::vector<mention> mentions() const {
    std::vector<mention> out;
    for (const auto& one : parts.input.parts.field.atoms())
      if (!one.picture)
        out.push_back({one.plain, one.target});
    return out;
  }
  // A mention picked from the list, over the @ and what was typed of it
  // (from `from` on): a pill, as the message will show it, and a space.
  void put_mention(std::size_t from, const std::string& name, const std::string& user) {
    auto& field = parts.input.parts.field;
    field.select(from, field.text().size());
    // Taken apart with Backspace: its user's id, not the name it is sent by.
    field.insertAtom("\u2002\u2002" + name, user, name, false, user);
    field.insertText(" ");
  }
  // The unsent bar up while any message here was not sent.
  void show_unsent(bool any) {
    if (any != parts.unsent.visible())
      parts.unsent.setVisible(any);
  }
  // The field, or the line that the reader may not post here.
  void set_can_post(bool can) {
    if (can == parts.input.visible())
      return;
    parts.input.setVisible(can);
    parts.no_post.setVisible(!can);
    if (!can)
      parts.context_line.setVisible(false);
  }
  // Those asking to join, where one may let them in: the first, and how many
  // more. Made again only as who is first changes.
  void show_knocks(Actions* a, const std::vector<knock_request>& knocking, bool may) {
    if (!may || knocking.empty()) {
      if (parts.knocks) {
        parts.knocks.reset();
        knocks_shown.clear();
        this->invalidateLayout();
      }
      return;
    }
    const std::string key = std::format("{}\n{}", knocking.front().id, knocking.size());
    if (key == knocks_shown)
      return;
    knocks_shown = key;
    parts.knocks.emplace(a, knocking.front(), knocking.size() - 1);
    this->invalidateLayout();
  }
  std::string knocks_shown;
  // Upgraded away: the notice in place of the field; or not.
  // After set_can_post: replaced, the field stays hidden whatever it said.
  void set_replaced(bool replaced) {
    if (!replaced && !parts.replaced.visible())
      return;
    if (replaced && parts.replaced.visible() && !parts.input.visible() && !parts.no_post.visible())
      return;
    parts.replaced.setVisible(replaced);
    if (replaced) {
      parts.input.setVisible(false);
      parts.no_post.setVisible(false);
      parts.context_line.setVisible(false);
    } else {
      parts.input.setVisible(true);
    }
    this->invalidateLayout();
  }
  // Whether what is written answers or edits something.
  [[nodiscard]] bool answering() const { return parts.context_line.visible(); }
  // What is written answers or edits something, shown; or nothing.
  void show_context(std::optional<compose_context> said) {
    parts.context_line.show(std::move(said));
    this->invalidateLayout();
  }
  void set_text(std::string text) { parts.input.parts.field.setText(std::move(text)); }
  void clear() { parts.input.parts.field.setText({}); }
};

// Telegram's @ and heart over "↓": how many mentions of the user, or
// reactions to theirs, are not yet seen; pressed, the oldest is gone to.
template <class Actions>
struct mark_button : scene::Node {
  Actions* actions = nullptr;
  mark_kind_t kind;
  using badge_t = count_badge;
  struct parts_t {
    nodes::Text glyph;
    badge_t badge;
  } parts;
  mark_button(Actions* a, mark_kind_t which, std::string glyph)
      : actions(a), kind(which), parts{.glyph = nodes::Text(std::move(glyph), 18.0f, text_colour, true)} {
    fState.apply({.place = scene::anchor::kBottomRight,
                  .x = -18.0f,
                  .y = -12.0f,
                  .width = 42.0f,
                  .height = 42.0f,
                  .cornerRadius = 21.0f,
                  .background = sidebar_colour,
                  .hoverBackground = chosen_colour,
                  .border = scene::Border{band_colour, 1.0f}});
    parts.glyph.apply({.place = scene::anchor::kCentre});
    this->setVisible(false);
  }
  // How many, and which place up the stack it takes: 0 at the bottom.
  void show(std::size_t count, int slot) {
    const bool up = count > 0;
    if (up != this->visible())
      this->setVisible(up);
    const std::string said = std::to_string(count);
    if (parts.badge.parts.count.text() != said)
      parts.badge.parts.count.setText(said);
    const float y = -12.0f - 52.0f * static_cast<float>(slot);
    if (fState.fY != y) {
      fState.apply({.y = y});
      this->invalidateLayout();
    }
  }
  [[nodiscard]] bool acceptsInput() const { return true; }
  [[nodiscard]] bool hoverChangesAppearance() const { return true; }
  [[nodiscard]] bool onClick(float, float) {
    actions->jump_to_mark(kind);
    return true;
  }
  // The other button: all of them, listed. On the way back up, not at the
  // target: a press there of its own hid the one that clicks, and a left
  // press did nothing.
  using Node::onPointer;
  void onPointer(scene::phase::bubble, const scene::pointer::down& press, scene::PointerReply& reply) {
    if (press.button != 3)
      return;
    actions->list_marks(kind);
    reply.handle();
  }
  // And where the press is on the button itself -- the target, which the
  // way back up does not reach: the right button lists them; any other
  // press as a node's is, a click.
  void onPointer(scene::phase::target, const scene::pointer::down& press, scene::PointerReply& reply) {
    if (press.button == 3) {
      actions->list_marks(kind);
      reply.handle();
      return;
    }
    scene::defaultPointer(*this, scene::phase::target{}, press, reply);
  }
};

// "↓": back to the newest, with how many came while one read above them.
template <class Actions>
struct jump_button : scene::Node {
  Actions* actions = nullptr;
  int unseen = 0;
  // A round plate with a chevron down, and over its top the count of what
  // came while the reader was above, on a badge in the accent.
  using badge_t = count_badge;
  struct parts_t {
    nodes::Icon chevron;
    badge_t badge;
  } parts{.chevron = nodes::Icon(shape_of(icon::down{}), text_colour)};
  explicit jump_button(Actions* a) : actions(a) {
    parts.badge.setVisible(false);
    fState.apply({.place = scene::anchor::kBottomRight,
                  .x = -18.0f,
                  .y = -12.0f,
                  .width = 42.0f,
                  .height = 42.0f,
                  .cornerRadius = 21.0f,
                  .background = sidebar_colour,
                  .hoverBackground = chosen_colour,
                  .border = scene::Border{band_colour, 1.0f}});
    parts.chevron.apply({.fill = true});
  }
  void set_unseen(int count) {
    unseen = count;
    parts.badge.parts.count.setText(std::to_string(count));
    parts.badge.setVisible(count > 0);
    this->invalidateLayout();
  }
  [[nodiscard]] bool acceptsInput() const { return true; }
  [[nodiscard]] bool hoverChangesAppearance() const { return true; }
  [[nodiscard]] bool onClick(float, float) {
    actions->jump_to_end();
    return true;
  }
};

// Back to the chat a jump came from -- a link or a reply into another chat
// -- over "↓", as Telegram's: the chat as it was left.
template <class Actions>
struct back_button : scene::Node {
  Actions* actions = nullptr;
  struct parts_t {
    nodes::Icon mark;
  } parts{.mark = nodes::Icon(shape_of(icon::back{}), text_colour)};
  explicit back_button(Actions* a) : actions(a) {
    fState.apply({.place = scene::anchor::kBottomRight,
                  .x = -18.0f,
                  .y = -12.0f,
                  .width = 42.0f,
                  .height = 42.0f,
                  .cornerRadius = 21.0f,
                  .background = sidebar_colour,
                  .hoverBackground = chosen_colour,
                  .border = scene::Border{band_colour, 1.0f}});
    parts.mark.apply({.fill = true});
    this->setVisible(false);
  }
  // Up or not, at a place in the stack of buttons over the list's corner.
  void show(bool up, int slot) {
    if (up != this->visible())
      this->setVisible(up);
    const float y = -12.0f - 52.0f * static_cast<float>(slot);
    if (fState.fY != y) {
      fState.apply({.y = y});
      this->invalidateLayout();
    }
  }
  [[nodiscard]] bool acceptsInput() const { return true; }
  [[nodiscard]] bool hoverChangesAppearance() const { return true; }
  [[nodiscard]] bool onClick(float, float) {
    actions->return_to_chat();
    return true;
  }
};

}  // namespace mux::ui
