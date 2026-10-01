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

// What Enter in the message field does: asks for its text to be sent.
template <class Actions>
struct submit_message {
  Actions* actions = nullptr;
  void operator()(std::string_view text) const { actions->submit_message(std::string(text)); }
};

// What a message being written answers or edits, as shown over the field:
// its icon, its title ("Reply to <name>", "Edit message"), a line of it.
struct compose_context {
  icon_t mark;
  std::string title;
  std::string line;
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
  // line inside one; the ``` closing it.
  enum class code_line { none, opening, inside, closing };
  [[nodiscard]] static code_line code_at(std::string_view text, std::size_t start) {
    bool open = false;
    for (std::size_t at = 0; at < start;) {
      const std::size_t end = text.find('\n', at);
      if (end == std::string_view::npos || end >= start)
        break;
      if (text.substr(at, end - at).starts_with("```"))
        open = !open;
      at = end + 1;
    }
    if (text.substr(start).starts_with("```"))
      return open ? code_line::closing : code_line::opening;
    return open ? code_line::inside : code_line::none;
  }
  // Whether a block opened at `start` is closed somewhere after it.
  [[nodiscard]] static bool closed_after(std::string_view text, std::size_t start) {
    for (std::size_t at = text.find('\n', start); at != std::string_view::npos; at = text.find('\n', at + 1))
      if (text.substr(at + 1).starts_with("```"))
        return true;
    return false;
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
    // A block's fences: their ``` hidden, as the field's other marks are --
    // the opening one's language, after it, still there to be read and
    // changed; the plate says where the block is.
    switch (code_at(text, start)) {
      case code_line::opening:
      case code_line::closing:
        return {.hidden = 3, .indent = kCodeIndent, .right = kCodeRight, .monospace = true};
      case code_line::inside:
        return {.indent = kCodeIndent, .right = kCodeRight, .monospace = true};
      case code_line::none:
        break;
    }
    const int deep = depth(text, start);
    if (deep == 0)
      return {};
    return {.hidden = 2 * static_cast<std::size_t>(deep), .indent = kIndent * static_cast<float>(deep), .right = kRight};
  }
  static void drawBehind(skia::SkCanvas* canvas, const skiff::paint::Painter& p, std::string_view text,
                         std::span<const widgets::ShownLine> lines, const skia::SkRect& box, const widgets::Theme& theme,
                         float size, float alpha) {
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
      const skia::SkRect plate = skia::SkRect::MakeLTRB(box.fLeft, lines[i].top, box.fRight, lines[j - 1].bottom);
      const int save = canvas->save();
      canvas->clipRRect(skia::SkRRect::MakeRectXY(plate, kRadius, kRadius), true);
      p.fillRect(plate, (theme.fAccent & 0x00FFFFFFu) | 0x1F000000u, alpha);
      p.fillRect(skia::SkRect::MakeXYWH(box.fLeft, plate.fTop, 3.0f, plate.height()), theme.fAccent, alpha);
      canvas->restoreToCount(save);
      i = j;
    }
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
  }
  [[nodiscard]] static std::optional<widgets::TextEdit> key(std::string_view text, std::size_t caret,
                                                            const scene::key::down& press) {
    namespace keys = scene::keys;
    namespace modifier = scene::modifier;
    const std::size_t start = start_of(text, caret);
    // In a block of code: Enter is a new line of it, not a send; Enter on an
    // opening ``` with nothing closing it closes it, the caret inside.
    if (press.key == keys::kEnter && !press.modifiers.has<modifier::shift>()) {
      const code_line at = code_at(text, start);
      if (at == code_line::opening && !closed_after(text, start)) {
        const std::size_t end = text.find('\n', caret) == std::string_view::npos ? text.size() : text.find('\n', caret);
        return widgets::TextEdit{.from = end, .to = end, .with = "\n\n```", .caret = end + 1};
      }
      if (at == code_line::opening || at == code_line::inside)
        return widgets::TextEdit{.from = caret, .to = caret, .with = "\n", .caret = caret + 1};
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

// Desktop: a line over it, a paperclip on the left, the text growing with
// what is written, and the send arrow on the right.
template <class Actions>
struct composer_bar : nodes::Stack {
  // What is written answers or edits, as tdesktop's FieldHeader shows it:
  // its icon in the left column (historyReplySkip wide), then two lines --
  // "Reply to <name>" or "Edit message" in the accent, semibold, over a
  // line of the message -- and ✕ on the right to go back to a plain one.
  struct context_row : nodes::Stack {
    static constexpr float kHeight = 49.0f;  // historyReplyHeight
    static constexpr float kSkip = 51.0f;    // historyReplySkip
    struct lines_column : nodes::Stack {
      struct parts_t {
        nodes::Text title{"", 13.0f, accent_colour, true};
        nodes::Text line{"", 13.0f, text_colour};
      } parts;
      lines_column() {
        this->setGap(2.0f);
        for (nodes::Text* each : {&parts.title, &parts.line}) {
          each->setElided(true);
          each->apply({.fillX = true});
        }
      }
    };
    using cancel_button = icon_button<ask<Actions, &Actions::cancel_compose>>;
    struct parts_t {
      nodes::Icon mark{IconShape{}, accent_colour};  // in the left column
      lines_column lines;
      cancel_button cancel;
    } parts;
    explicit context_row(Actions* a) : parts{.cancel = cancel_button(icon::close{}, {a})} {
      this->setHorizontal();
      this->setGap(8.0f);
      fState.apply({.fillX = true, .height = kHeight, .padding = {0.0f, 8.0f, 0.0f, 0.0f}});
      parts.mark.apply({.fillY = true, .width = kSkip - 8.0f});
      parts.lines.apply({.autoSize = scene::axes::kY, .grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
      parts.cancel.apply({.alignSelf = scene::align::kMiddle});
    }
  };
  // The paperclip, the field growing with what is written in it, the arrow.
  struct input_row : nodes::Stack {
    using attach_button = icon_button<ask<Actions, &Actions::attach_files>>;
    using field_t = widgets::TextArea<submit_message<Actions>, message_pictures, field_quotes>;
    using emoji_button = icon_button<ask<Actions, &Actions::toggle_emoji>>;
    using send_button = icon_button<ask<Actions, &Actions::send_typed>>;
    struct parts_t {
      attach_button attach;
      field_t field;
      emoji_button emoji;
      send_button send;
    } parts;
    explicit input_row(Actions* a)
        : parts{.attach = attach_button(icon::clip{}, {a}),
                .field = field_t("Write a message…", {a}),
                .emoji = emoji_button(icon::smile{}, {a}),
                .send = send_button(icon::send{}, {a})} {
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
  struct parts_t {
    nodes::Box<> divider{band_colour};
    unsent_row unsent;
    context_row context_line;
    input_row input;
    // Where the reader may not post: said in place of the field, as Element
    // says it.
    no_post_row no_post;
  } parts;
  // The old name, for what reads it.
  typename input_row::field_t& field = parts.input.parts.field;

  // Declared: the divider, the unsent bar, the answer's line where there is
  // one, the row -- or, where the reader may not post, the line saying so.
  explicit composer_bar(Actions* a) : parts{.unsent = unsent_row(a), .context_line = context_row(a), .input = input_row(a)} {
    parts.unsent.setVisible(false);
    parts.no_post.setVisible(false);
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
  // Whether what is written answers or edits something.
  [[nodiscard]] bool answering() const { return parts.context_line.visible(); }
  // What is written answers or edits something, shown; or nothing.
  void show_context(std::optional<compose_context> said) {
    auto& context_line = parts.context_line;
    context_line.setVisible(said.has_value());
    context_line.parts.mark.setShape(said ? shape_of(said->mark) : IconShape{});
    context_line.parts.lines.parts.title.setText(said ? said->title : std::string());
    context_line.parts.lines.parts.line.setText(said ? said->line : std::string());
    context_line.markDamaged();
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
  struct badge_t : nodes::Stack {
    struct parts_t {
      nodes::Text count{"", 11.0f, on_accent_colour, true};
    } parts;
    badge_t() {
      fState.apply({.place = scene::anchor::kTopCentre,
                    .y = -10.0f,
                    .height = 18.0f,
                    .autoSize = scene::axes::kX,
                    .minWidth = 20.0f,
                    .padding = {1.0f, 5.0f, 1.0f, 5.0f},
                    .cornerRadius = 9.0f,
                    .background = accent_colour});
      fStack.justify = nodes::justify::middle{};
      parts.count.apply({.alignSelf = scene::align::kMiddle});
    }
  };
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
};

// "↓": back to the newest, with how many came while one read above them.
template <class Actions>
struct jump_button : scene::Node {
  Actions* actions = nullptr;
  int unseen = 0;
  // A round plate with a chevron down, and over its top the count of what
  // came while the reader was above, on a badge in the accent.
  struct badge_t : nodes::Stack {
    struct parts_t {
      nodes::Text count{"", 11.0f, on_accent_colour, true};
    } parts;
    badge_t() {
      fState.apply({.place = scene::anchor::kTopCentre,
                     .y = -10.0f,
                     .height = 18.0f,
                     .autoSize = scene::axes::kX,
                     .minWidth = 20.0f,
                     .padding = {1.0f, 5.0f, 1.0f, 5.0f},
                     .cornerRadius = 9.0f,
                     .background = accent_colour});
      fStack.justify = nodes::justify::middle{};
      parts.count.apply({.alignSelf = scene::align::kMiddle});
      this->setVisible(false);
    }
  };
  struct parts_t {
    nodes::Icon chevron;
    badge_t badge;
  } parts{.chevron = nodes::Icon(shape_of(icon::down{}), text_colour)};
  explicit jump_button(Actions* a) : actions(a) {
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
