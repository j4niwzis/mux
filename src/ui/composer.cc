// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:composer -- Where a message is written.
export module mux.ui:composer;

import std;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.compose;
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

// Prefix each displayed line as a quote, shifting its inline images by
// the added markers. Image identity comes from the selection, not a pack.
[[nodiscard]] inline scene::ClipboardFragment quoted_fragment(const scene::ClipboardFragment& selected) {
  std::string display;
  std::vector<scene::ClipboardAtom> atoms;
  for (std::size_t at = 0; at <= selected.display.size();) {
    const std::size_t end = std::min(selected.display.find('\n', at), selected.display.size());
    display += "> ";
    const std::size_t start = display.size();
    display.append(selected.display, at, end - at);
    for (auto atom : selected.atoms)
      if (atom.first >= at && atom.last <= end) {
        atom.first = start + atom.first - at;
        atom.last = start + atom.last - at;
        atoms.push_back(std::move(atom));
      }
    display += '\n';
    at = end + 1;
  }
  display += '\n';
  return scene::clipboardFragment(std::move(display), std::move(atoms));
}

inline void prepend_quote(auto& field, const scene::ClipboardFragment& selected) {
  field.select(0, 0);
  field.insertFragment(quoted_fragment(selected));
}

// An edge between two parts of the window, to drag: the pointer turns into
// a resize arrow over it, and a drag asks `on_drag(x)` for the edge to be at
// x. It draws a thin line where `with_line`.
// A round button over the bottom right corner of the messages, on the
// panels' colour: the @ and the heart, "↓", the way back.
[[nodiscard]] inline scene::Spec corner_button(const palette& colours) {
  return {.place = scene::anchor::kBottomRight,
                  .x = -18.0f,
                  .y = -12.0f,
                  .width = 42.0f,
                  .height = 42.0f,
                  .cornerRadius = 21.0f,
                  .background = colours.sidebar,
                  .hoverBackground = colours.chosen,
                  .border = scene::Border{colours.band, 1.0f}};
}

template <class OnDrag> struct drag_edge : skiff::compose::Specced {
  OnDrag on_drag;
  // Where it was last dragged to, and what its act answers there.
  float dragged_to = 0.0f;
  auto onPress()
    requires skiff::scene::Answering<OnDrag>
  {
    return on_drag(dragged_to);
  }
  bool with_line = true;
  bool dragging = false;

  // The thin line down its middle, where it has one.
  struct parts_t {
    nodes::Box<> line;
  } parts;

  drag_edge(const palette &colours, OnDrag what, bool line = true)
      : Specced({}), on_drag(std::move(what)), with_line(line),
        parts{.line = skiff::compose::visible(
                  line,
                  skiff::compose::styled({.place = scene::anchor::kTopCentre,
                                          .fillY = true,
                                          .width = 1.0f},
                                         nodes::Box<>(colours.band)))} {
    fState.setCursor(scene::cursor::resize_horizontal{});
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
    dragged_to = at.x;
    act_on(fState, on_drag, at.x);
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
  using Answer = std::optional<::mux::ui::request::resize_sidebar>;
  std::optional<::mux::ui::request::resize_sidebar> operator()(float x) { return ::mux::ui::request::resize_sidebar{x}; }
};
template <class Actions>
struct resize_info_to {
  using Answer = std::optional<::mux::ui::request::resize_info>;
  std::optional<::mux::ui::request::resize_info> operator()(float x) { return ::mux::ui::request::resize_info{x}; }
};

// ---- the message field --------------------------------------------------------------

// A block of code the text opens and does not close -- its closing ``` not
// written: closed at the end, as the field showed it, so that what is sent
// says the block where it ends, as the field did.
[[nodiscard]] inline std::string with_blocks_closed(std::string_view text) {
  const bool open = std::ranges::count_if(std::views::split(text, '\n'), [](auto line) {
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
  using Answer = ::mux::ui::request::submit_message;
  Answer operator()(std::string_view text) const { return ::mux::ui::request::submit_message{with_blocks_closed(text)}; }
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

  // Formatting, as Telegram's field has it (lib_ui's input_field.cpp): runs
  // of what is written bold, italic, underlined, struck through, a spoiler
  // or code -- spans the field keeps beside the text, drawn as the message
  // will show them and sent as Matrix's HTML. Made by tdesktop's shortcuts
  // on what is selected, and by its Markdown as it is typed: a run closed
  // by the mark that opened it is that run, the marks gone.
  using format = mux::run_style_t;
  using span = widgets::TextSpan<format>;
  using field_view = widgets::FieldView<format>;
  using field_change = widgets::FieldChange<format>;
  [[nodiscard]] static widgets::RunLook runLook(const format& style) {
    return spl::visit(spl::overloaded{[](const run_style::bold&) { return widgets::RunLook{.face = {.bold = true}}; },
                                      [](const run_style::italic&) { return widgets::RunLook{.face = {.italic = true}}; },
                                      [](const run_style::underline&) { return widgets::RunLook{.underline = true}; },
                                      [](const run_style::strike&) { return widgets::RunLook{.strike = true}; },
                                      [](const run_style::spoiler&) { return widgets::RunLook{.plate = true}; },
                                      [](const run_style::code&) { return widgets::RunLook{.monospace = true, .accent = true}; },
                                      [](const run_style::custom_emoji&) { return widgets::RunLook{}; },
                                      [](const run_style::link&) { return widgets::RunLook{.underline = true, .accent = true}; }},
                      style);
  }
  // What is typed at a run's end is not of it: tdesktop's field goes on in
  // its default format after a run its Markdown made.
  [[nodiscard]] static bool grows(const format&) { return false; }

  // The spans with [from, to) cut out of those `of` takes.
  template <class Of>
  [[nodiscard]] static std::vector<span> without(const std::vector<span>& spans, std::size_t from, std::size_t to, Of of) {
    return std::ranges::to<std::vector<span>>(std::views::join(std::views::transform(spans, [&](const span& one) {
      if (!of(one) || one.last <= from || one.first >= to)
        return std::vector<span>{one};
      std::vector<span> kept;
      if (one.first < from)
        kept.push_back({one.first, from, one.format});
      if (one.last > to)
        kept.push_back({to, one.last, one.format});
      return kept;
    })));
  }
  // A style put on what is selected -- or taken off it, where all of it has
  // it already: tdesktop's toggleSelectionMarkdown. A link put on replaces
  // the links there.
  [[nodiscard]] static std::optional<field_change> toggled(const field_view& field, format style) {
    if (!field.selection())
      return std::nullopt;
    const std::size_t low = field.low(), high = field.high();
    const auto same = [&](const span& one) { return one.format.index() == style.index(); };
    std::vector<span> kind = std::ranges::to<std::vector<span>>(std::views::filter(field.spans, same));
    std::ranges::sort(kind, {}, &span::first);
    const std::size_t reached = std::ranges::fold_left(
        kind, low, [](std::size_t at, const span& one) { return one.first <= at && one.last > at ? one.last : at; });
    std::vector<span> spans = without(field.spans, low, high, same);
    if (reached < high)
      spans.push_back({low, high, std::move(style)});
    return field_change{.spans = std::move(spans), .caret = field.caret, .anchor = field.anchor};
  }
  // Ctrl+Shift+. -- tdesktop's blockquote: the lines selected (or the
  // caret's) made a quote, a level of "> " before each; where all of them
  // are quoted already, a level taken off them instead.
  [[nodiscard]] static std::optional<field_change> quoted(const field_view& field) {
    const std::string_view text = field.text;
    const std::size_t low = field.low(), high = field.high();
    std::vector<std::size_t> starts;
    for (std::size_t at = start_of(text, low);;) {
      starts.push_back(at);
      const std::size_t end = text.find('\n', at);
      // The line the selection ends at the start of is not in it.
      if (end == std::string_view::npos || end + 1 >= high + (high == low ? 1 : 0))
        break;
      at = end + 1;
    }
    const bool off = std::ranges::all_of(starts, [&](std::size_t at) { return depth(text, at) > 0; });
    // Back to front: each edit leaves the offsets before it as they were.
    std::vector<widgets::TextReplace> edits = std::ranges::to<std::vector<widgets::TextReplace>>(std::views::transform(
        std::views::reverse(starts), [&](std::size_t at) {
          return off ? widgets::TextReplace{at, at + 2, ""} : widgets::TextReplace{at, at, "> "};
        }));
    // The selection moved by the marks put in or taken out before it.
    const auto moved = [&](std::size_t at) {
      const auto before = static_cast<std::size_t>(std::ranges::count_if(starts, [&](std::size_t one) { return one <= at; }));
      return off ? at - std::min(at - starts.front(), 2 * before) : at + 2 * before;
    };
    return field_change{.edits = std::move(edits), .caret = moved(field.caret), .anchor = moved(field.anchor)};
  }
  // tdesktop's shortcuts (InputField's kShortcuts): Ctrl+B bold, Ctrl+I
  // italic, Ctrl+U underlined; with Shift, X struck through, M code, P a
  // spoiler, N every format taken off what is selected.
  [[nodiscard]] static std::optional<field_change> edit(const field_view& field, const scene::key::down& press) {
    namespace keys = scene::keys;
    namespace modifier = scene::modifier;
    if (!press.modifiers.has<modifier::control>())
      return std::nullopt;
    if (!press.modifiers.has<modifier::shift>()) {
      if (press.key == keys::kB)
        return toggled(field, run_style::bold{});
      if (press.key == keys::kI)
        return toggled(field, run_style::italic{});
      if (press.key == keys::kU)
        return toggled(field, run_style::underline{});
      return std::nullopt;
    }
    if (press.key == keys::kX)
      return toggled(field, run_style::strike{});
    if (press.key == keys::kM)
      return toggled(field, run_style::code{});
    if (press.key == keys::kP)
      return toggled(field, run_style::spoiler{});
    if (press.key == keys::kPeriod)
      return quoted(field);
    if (press.key == keys::kN && field.selection())
      return field_change{.spans = without(field.spans, field.low(), field.high(), [](const span&) { return true; }),
                          .caret = field.caret,
                          .anchor = field.anchor};
    return std::nullopt;
  }

  // Telegram's Markdown as it is typed (TextUtilities' Markdown*GoodBefore
  // and BadAfter): a run closed by the mark that opened it on its line --
  // the opening mark at the line's start or after what is not a letter, a
  // digit or the mark's own character; the run not starting or ending with
  // a space, nor with the mark's character (``` is a fence, *** not bold).
  // Not in a block of code, nor in a quote's marks.
  // Its marks, read into the styles they make: the one place they are text.
  [[nodiscard]] static const std::array<std::pair<std::string_view, format>, 5>& markdown_marks() {
    static const std::array<std::pair<std::string_view, format>, 5> marks{{{"**", run_style::bold{}},
                                                                            {"__", run_style::italic{}},
                                                                            {"~~", run_style::strike{}},
                                                                            {"||", run_style::spoiler{}},
                                                                            {"`", run_style::code{}}}};
    return marks;
  }
  [[nodiscard]] static std::optional<field_change> typedIn(const field_view& field) {
    const std::string_view text = field.text;
    const std::size_t caret = field.caret;
    if (field.selection() || caret == 0)
      return std::nullopt;
    const std::size_t start = start_of(text, caret);
    if (code_at(text, start) != code_line::none)
      return std::nullopt;
    const std::size_t body = body_at(text, start);
    const auto blank = [](char c) { return c == ' ' || c == '\t' || c == '\n'; };
    const auto word = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0; };
    for (const auto& [mark, style] : markdown_marks()) {
      const std::size_t size = mark.size();
      if (caret < body + 2 * size + 1 || text.substr(caret - size, size) != mark)
        continue;
      const std::size_t close = caret - size;
      const char own = mark.front();
      if (blank(text[close - 1]) || text[close - 1] == own)
        continue;
      for (std::size_t from = close - size - 1;;) {
        const std::size_t open = text.rfind(mark, from);
        if (open == std::string_view::npos || open < body)
          break;
        const bool good_before = open == body || (!word(text[open - 1]) && text[open - 1] != own);
        const char first = text[open + size];
        if (good_before && open + size < close && !blank(first) && first != own) {
          // The closing mark out, then the opening one: the run where its
          // text now is, the caret after it.
          return field_change{.edits = {{close, caret, ""}, {open, open + size, ""}},
                              .added = {span{open, close - size, style}},
                              .caret = close - size,
                              .anchor = close - size};
        }
        if (open == 0)
          break;
        from = open - 1;
      }
    }
    return std::nullopt;
  }
};

// Where a message is written: the paperclip, the field growing with what is
// written in it -- quotes and custom emoji shown as they will be sent -- the
// emoji, the arrow. The chat's composer and a thread's; what each
// button does is said by where it is.
template <class Submit, class Attach, class Emoji, class Send>
struct message_input : skiff::compose::Stacked {
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
  message_input(const palette& colours, std::string placeholder, Submit submit, Attach attach_it, Emoji emoji_it, Send send_it)
      : Stacked(skiff::compose::hbox(6.0f, {.fillX = true, .autoSize = scene::axes::kY, .minHeight = 54.0f, .padding = {9.0f, 8.0f, 9.0f, 8.0f}})),
        parts{.attach = skiff::compose::styled({.alignSelf = scene::align::kEnd}, attach_button(colours, icon::clip{}, std::move(attach_it))),
              .field = skiff::compose::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle},
                                              message_sized(field_t(colours.widgets, std::move(placeholder), std::move(submit)))),
              .emoji = skiff::compose::styled({.alignSelf = scene::align::kEnd}, emoji_button(colours, icon::smile{}, std::move(emoji_it))),
              .send = skiff::compose::styled({.alignSelf = scene::align::kEnd}, accented(colours, send_button(colours, icon::send{}, std::move(send_it))))} {}
  // What is typed looks as it will be sent: the messages' size, as in
  // tdesktop, whose field takes the message font.
  [[nodiscard]] static field_t message_sized(field_t field) {
    field.setFontSize(13.0f);
    return field;
  }
  [[nodiscard]] static send_button accented(const palette& colours, send_button send) {
    send.set_colour(colours.accent);
    return send;
  }
};

// Where what is written goes, as a composer is told it: what its ✕, its
// Enter, its paperclip, its emoji and its arrow do, and what its empty field
// says. The chat's own; a thread's is its panel's.
template <class Actions>
struct in_chat {
  using cancel = sends<::mux::ui::request::cancel_compose>;
  using submit = submit_message<Actions>;
  using attach = sends<::mux::ui::request::attach_files>;
  using emoji = sends<::mux::ui::request::toggle_emoji>;
  using send = sends<::mux::ui::request::send_typed>;
  static constexpr std::string_view placeholder = "Write a message…";
};

// Desktop: a line over it, a paperclip on the left, the text growing with
// what is written, and the send arrow on the right. The chat's, and a
// thread's: one composer, told where it writes.
// A bar over the field that says something and offers two ways on: the
// line, cut short where the room runs out, then the two buttons -- the
// second the one to take (Retry all, Approve).
// A round button in the stack over the list's corner: up or not, at its
// place in the stack.
inline void place_in_corner(auto& button, bool up, int slot) {
  if (up != button.visible())
    button.setVisible(up);
  const float y = -12.0f - 52.0f * static_cast<float>(slot);
  if (button.fState.fY != y) {
    button.fState.apply({.y = y});
    button.invalidateLayout();
  }
}

template <class First, class Second>
struct two_choice_bar : skiff::compose::Stacked {
  struct parts_t {
    nodes::Text said;
    widgets::Button<First> first;
    widgets::Button<Second> second;
  } parts;
  two_choice_bar(const palette& colours, std::string said, skia::SkColor said_colour, std::string first_name, First first,
                 std::string second_name, Second second)
      : Stacked(skiff::compose::hbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {6.0f, 12.0f, 6.0f, 12.0f}})),
        parts{.said = skiff::compose::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle}, elided(nodes::Text(std::move(said), 13.0f, said_colour))),
              .first = widgets::Button<First>(colours.widgets, std::move(first_name), std::move(first)),
              .second = primary(widgets::Button<Second>(colours.widgets, std::move(second_name), std::move(second)))} {}
};

template <class Actions, class Where = in_chat<Actions>>
struct composer_bar : skiff::compose::Stacked {
  // Child references and handlers require a fixed address.
  composer_bar(const composer_bar&) = delete;
  composer_bar& operator=(const composer_bar&) = delete;
  composer_bar(composer_bar&&) = delete;
  composer_bar& operator=(composer_bar&&) = delete;

  // What is written answers or edits: the reply bar, its ✕ going back to
  // a plain message.
  using context_row = context_bar<typename Where::cancel>;
  using input_row = message_input<typename Where::submit, typename Where::attach, typename Where::emoji, typename Where::send>;
  // Element's bar over the field while messages here were not sent
  // (RoomStatusBar's): a warning, and "Delete all" and "Retry all".
  using unsent_row = two_choice_bar<sends<::mux::ui::request::discard_unsent>, sends<::mux::ui::request::retry_unsent>>;
  // Where the reader may not post: a row as high as the input's, its line
  // in the middle -- padded inside it, not by a margin the bar's height
  // leaves out.
  struct no_post_row : skiff::compose::Stacked {
    struct parts_t {
      nodes::Text line;
    } parts;
    explicit no_post_row(const palette& colours)
        : Stacked(skiff::compose::justified(
              skiff::compose::hbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY, .minHeight = 54.0f, .padding = {18.0f, 12.0f, 18.0f, 12.0f}}),
              nodes::justify::middle{})),
          parts{.line = skiff::compose::styled({.alignSelf = scene::align::kMiddle},
                                               nodes::Text("You don't have permission to post in this chat", 13.0f, colours.dim))} {}
  };
  // A tombstoned room's: "This room has been replaced and is no longer
  // active", and the room it goes on in, opened -- joined, where it is not
  // yet.
  using go_on = sends<::mux::ui::request::open_replacement>;
  struct replaced_row : skiff::compose::Stacked {
    struct parts_t {
      nodes::Text line;
      widgets::Button<go_on> go;
    } parts;
    replaced_row(const palette& colours)
        : Stacked(skiff::compose::justified(
              skiff::compose::hbox(10.0f, {.fillX = true, .autoSize = scene::axes::kY, .minHeight = 54.0f, .padding = {12.0f, 12.0f, 12.0f, 12.0f}}),
              nodes::justify::middle{})),
          parts{.line = skiff::compose::styled({.alignSelf = scene::align::kMiddle},
                                               nodes::Text("This room has been replaced and is no longer active.", 13.0f, colours.dim)),
                .go = skiff::compose::styled({.height = 30.0f, .alignSelf = scene::align::kMiddle},
                                             primary(widgets::Button<go_on>(colours.widgets, "The conversation continues here", {})))} {}
  };
  // Those asking to join, for those who may let them in: the first
  // of them -- who, and why -- with Approve (an invite) and Deny (their
  // knock refused), and how many more.
  struct approve_it {
    using Answer = ::mux::ui::request::room_act;
    std::string user;
    ::mux::ui::request::room_act operator()() { return ::mux::ui::request::room_act{room_action::invite{user}}; }
  };
  struct deny_it {
    using Answer = ::mux::ui::request::room_act;
    std::string user;
    ::mux::ui::request::room_act operator()() { return ::mux::ui::request::room_act{room_action::kick{user}}; }
  };
  using knock_row = two_choice_bar<deny_it, approve_it>;
  [[nodiscard]] static std::string knock_said(const knock_request& one, std::size_t more) {
    return std::format("{} asks to join{}{}", one.name, one.reason.empty() ? std::string() : ": " + one.reason,
                       more ? std::format(" (and {} more)", more) : std::string());
  }
  // The colours it is made in, for the rows it makes later.
  const palette* colours_ = nullptr;
  struct parts_t {
    nodes::Box<> divider;
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
  explicit composer_bar(const ui_needs<Actions>& n) : composer_bar(n, {}, {}, {}, {}, {}) {}
  composer_bar(const ui_needs<Actions>& n, typename Where::cancel cancel, typename Where::submit submit, typename Where::attach attach,
               typename Where::emoji emoji, typename Where::send send)
      : Stacked(skiff::compose::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY, .background = n.colours->sidebar})),
        colours_(n.colours),
        parts{.divider = skiff::compose::styled({.fillX = true, .height = 1.0f}, nodes::Box<>(n.colours->band)),
              .unsent = skiff::compose::visible(false, unsent_row(*n.colours, "Some of your messages have not been sent", n.colours->error,
                                                                  "Delete all", {}, "Retry all", {})),
              .context_line = skiff::compose::visible(false, context_row(*n.colours, std::move(cancel))),
              .input = input_row(*n.colours, std::string(Where::placeholder), std::move(submit), std::move(attach), std::move(emoji),
                                 std::move(send)),
              .no_post = skiff::compose::visible(false, no_post_row(*n.colours)),
              .replaced = skiff::compose::visible(false, replaced_row(*n.colours))} {}

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
  // How what is written is formatted, by offsets in plain(): sent with it.
  [[nodiscard]] std::vector<mux::styled_run> styles() const {
    const auto& field = parts.input.parts.field;
    auto runs = std::ranges::to<std::vector<mux::styled_run>>(std::views::transform(
        field.plainSpans(), [](const auto& one) { return mux::styled_run{one.first, one.last, one.format}; }));
    std::ptrdiff_t shift = 0;
    for (const auto& atom : field.atoms()) {
      const auto first = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(atom.first) + shift);
      if (atom.picture)
        runs.push_back({first, first + atom.plain.size(), run_style::custom_emoji{atom.target}});
      shift += static_cast<std::ptrdiff_t>(atom.plain.size()) - static_cast<std::ptrdiff_t>(atom.last - atom.first);
    }
    return runs;
  }
  // What is selected, and the link on it where it has one: what Ctrl+K's
  // box starts with.
  [[nodiscard]] std::pair<std::string, std::string> link_asked() const {
    const auto& field = parts.input.parts.field;
    const auto [low, high] = field.selectionRange();
    std::string url;
    for (const auto& one : field.spans())
      if (one.first <= low && one.last >= high)
        spl::visit(spl::overloaded{[&](const run_style::link& link) { url = link.url; }, [](const auto&) {}}, one.format);
    return {field.text().substr(low, high - low), std::move(url)};
  }
  // A link put on what was selected, as tdesktop's EditLinkBox: its text as
  // given (what was selected replaced where it differs), the URL with a
  // scheme -- https where it has none; with no URL, the links there taken
  // off.
  void put_link(std::string text, std::string url) {
    auto& field = parts.input.parts.field;
    auto [low, high] = field.selectionRange();
    if (text.empty())
      text = url;
    if (text.empty())
      return;
    if (std::string_view(field.text()).substr(low, high - low) != text) {
      field.insertText(text);
      high = low + text.size();
    }
    if (!url.empty() && !url.contains("://") && !url.starts_with("mailto:"))
      url = "https://" + url;
    const auto link = [](const auto& one) {
      return spl::visit(spl::overloaded{[](const run_style::link&) { return true; }, [](const auto&) { return false; }}, one.format);
    };
    auto spans = field_quotes::without(field.spans(), low, high, link);
    if (!url.empty())
      spans.push_back({low, high, run_style::link{std::move(url)}});
    field.setSpans(std::move(spans));
    field.select(high, high);
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
  void show_knocks(const std::vector<knock_request>& knocking, bool may) {
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
    parts.knocks.emplace(*colours_, knock_said(knocking.front(), knocking.size() - 1), colours_->text, "Deny",
                         deny_it{knocking.front().id}, "Approve", approve_it{knocking.front().id});
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
  // What is written from `from` to its end replaced by `text`, the caret
  // after it: a suggestion picked over what was typed for it.
  void put_over_end(std::size_t from, const std::string& text) {
    auto& field = parts.input.parts.field;
    field.select(from, field.text().size());
    field.insertText(text);
  }
  // Text and its runs, as a message edited had them.
  void set_formatted(std::string text, const std::vector<mux::styled_run>& runs) {
    auto& field = parts.input.parts.field;
    field.setText(std::move(text));
    field.setSpans(std::ranges::to<std::vector<field_quotes::span>>(
        std::views::transform(runs, [](const mux::styled_run& one) { return field_quotes::span{one.first, one.last, one.style}; })));
  }
  void clear() { parts.input.parts.field.setText({}); }
};

// Telegram's @ and heart over "↓": how many mentions of the user, or
// reactions to theirs, are not yet seen; pressed, the oldest is gone to.
template <class Actions>
struct mark_button : skiff::compose::Specced {
  using Answer = std::variant<::mux::ui::request::jump_to_mark, ::mux::ui::request::list_marks>;
  mark_kind_t kind;
  using badge_t = count_badge;
  struct parts_t {
    nodes::Text glyph;
    badge_t badge;
  } parts;
  mark_button(const palette& colours, mark_kind_t which, std::string glyph)
      : Specced(corner_button(colours)),
        kind(which),
        parts{.glyph = skiff::compose::styled({.place = scene::anchor::kCentre}, nodes::Text(std::move(glyph), 18.0f, colours.text, true)),
              .badge = badge_t(colours)} {
    this->setVisible(false);
  }
  // How many, and which place up the stack it takes: 0 at the bottom.
  void show(std::size_t count, int slot) {
    const bool up = count > 0;
    const std::string said = std::to_string(count);
    if (parts.badge.parts.count.text() != said)
      parts.badge.parts.count.setText(said);
    place_in_corner(*this, up, slot);
  }
  [[nodiscard]] bool acceptsInput() const { return true; }
  [[nodiscard]] bool hoverChangesAppearance() const { return true; }
  std::optional<Answer> onClick(float, float) { return ::mux::ui::request::jump_to_mark{kind}; }
  // The other button: all of them, listed. On the way back up, not at the
  // target: a press there of its own hid the one that clicks, and a left
  // press did nothing.
  using Node::onPointer;
  std::optional<Answer> onPointer(scene::phase::bubble, const scene::pointer::down& press, scene::PointerReply& reply) {
    if (press.button != 3)
      return std::nullopt;
    reply.handle();
    return ::mux::ui::request::list_marks{kind};
  }
  // And where the press is on the button itself -- the target, which the
  // way back up does not reach: the right button lists them; any other
  // press as a node's is, a click.
  std::optional<Answer> onPointer(scene::phase::target, const scene::pointer::down& press, scene::PointerReply& reply) {
    if (press.button == 3) {
      reply.handle();
      return ::mux::ui::request::list_marks{kind};
    }
    return scene::defaultPointer(*this, scene::phase::target{}, press, reply);
  }
};

// "↓": back to the newest, with how many came while one read above them.
template <class Actions>
struct jump_button : skiff::compose::Specced {
  // What its handlers ask for, returned.
  using Answer = ::mux::ui::request::jump_to_end;
  int unseen = 0;
  // A round plate with a chevron down, and over its top the count of what
  // came while the reader was above, on a badge in the accent.
  using badge_t = count_badge;
  struct parts_t {
    nodes::Icon chevron;
    badge_t badge;
  } parts;
  jump_button(const palette& colours)
      : Specced(corner_button(colours)),
        parts{.chevron = skiff::compose::styled({.fill = true}, nodes::Icon(shape_of(icon::down{}), colours.text)),
              .badge = skiff::compose::visible(false, badge_t(colours))} {}
  void set_unseen(int count) {
    unseen = count;
    parts.badge.parts.count.setText(std::to_string(count));
    parts.badge.setVisible(count > 0);
    this->invalidateLayout();
  }
  [[nodiscard]] bool acceptsInput() const { return true; }
  [[nodiscard]] bool hoverChangesAppearance() const { return true; }
  std::optional<Answer> onClick(float, float) {
    return ::mux::ui::request::jump_to_end{};
  }
};

// Back to the chat a jump came from -- a link or a reply into another chat
// -- over "↓", as Telegram's: the chat as it was left.
template <class Actions>
struct back_button : skiff::compose::Specced {
  // What its handlers ask for, returned.
  using Answer = ::mux::ui::request::return_to_chat;
  struct parts_t {
    nodes::Icon mark;
  } parts;
  back_button(const palette& colours)
      : Specced(corner_button(colours)), parts{.mark = skiff::compose::styled({.fill = true}, nodes::Icon(shape_of(icon::back{}), colours.text))} {
    this->setVisible(false);
  }
  // Up or not, at a place in the stack of buttons over the list's corner.
  void show(bool up, int slot) { place_in_corner(*this, up, slot); }
  [[nodiscard]] bool acceptsInput() const { return true; }
  [[nodiscard]] bool hoverChangesAppearance() const { return true; }
  std::optional<Answer> onClick(float, float) {
    return ::mux::ui::request::return_to_chat{};
  }
};

}  // namespace mux::ui
