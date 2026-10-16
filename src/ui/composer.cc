// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:composer -- Where a message is written.
module;
#include "skiff/named.h"
export module mux.ui.messages:composer;

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
import mux.ui.base;
import mux.plugins.ui;
import mux.ui.proto;
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


// An edge between two columns, dragged across: the pointer a resize cursor
// over it, and each move of a drag answered with what its act makes of
// where it is. A thin line down its middle, where it has one.
template <class OnDrag> auto drag_edge(const palette& colours, OnDrag what, bool line = true) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(drag_edge, c::onDragAcross(std::move(what),
                         c::with_cursor(scene::cursor::resize_horizontal{},
                                        c::column(c::vbox(), c::visible(line, c::styled({.place = scene::anchor::kTopCentre, .fillY = true,
                                                                                          .width = 1.0f},
                                                                                         nodes::Box<>(colours.band)))))));
}

// Where an edge was dragged to, asked of the program.
struct resize_sidebar_to {
  using Answer = std::optional<::mux::ui::request::resize_sidebar>;
  std::optional<::mux::ui::request::resize_sidebar> operator()(float x) { return ::mux::ui::request::resize_sidebar{x}; }
};
struct resize_info_to {
  using Answer = std::optional<::mux::ui::request::resize_info>;
  std::optional<::mux::ui::request::resize_info> operator()(float x) { return ::mux::ui::request::resize_info{x}; }
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
        return {.hidden = paragraph_length(text, start), .indent = quoted + kCodeIndent, .right = right + kCodeRight, .monospace = true, .caret = false};
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
    return spans | std::views::transform([&](const span& one) {
      if (!of(one) || one.last <= from || one.first >= to)
        return std::vector<span>{one};
      std::vector<span> kept;
      if (one.first < from)
        kept.push_back({one.first, from, one.format});
      if (one.last > to)
        kept.push_back({to, one.last, one.format});
      return kept;
    }) | std::views::join | std::ranges::to<std::vector<span>>();
  }
  // A style put on what is selected -- or taken off it, where all of it has
  // it already: tdesktop's toggleSelectionMarkdown. A link put on replaces
  // the links there.
  [[nodiscard]] static std::optional<field_change> toggled(const field_view& field, format style) {
    if (!field.selection())
      return std::nullopt;
    const std::size_t low = field.low(), high = field.high();
    const auto same = [&](const span& one) { return one.format.index() == style.index(); };
    std::vector<span> kind = field.spans | std::views::filter(same) | std::ranges::to<std::vector<span>>();
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
    std::vector<widgets::TextReplace> edits = starts | std::views::reverse | std::views::transform([&](std::size_t at) {
          return off ? widgets::TextReplace{at, at + 2, ""} : widgets::TextReplace{at, at, "> "};
        }) | std::ranges::to<std::vector<widgets::TextReplace>>();
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

// ---- what is written, as the program keeps it ------------------------------------
// The draft of a field: its text, its pills and pictures, its runs and where
// the caret is -- skiff-widgets' document, of the message's runs. The
// program keeps the chat's and the thread's (chat_draft, thread_draft); the
// field shows them, and tells each edit (draft_told) as a request carrying
// the whole document, Enter as its send. What the program does to what is
// written -- a link put on, a name picked, a quote, an emoji -- it does to
// the draft with the functions below, the field's own way of editing.
using composer_doc = widgets::TextDocument<run_style_t>;
// Text typed at a run's end goes into it as the field's quotes say.
struct composer_grows {
  [[nodiscard]] bool operator()(const run_style_t& format) const { return widgets::growsBy(field_quotes{}, format); }
};
struct chat_draft {
  composer_doc now;
  using ModelOpaque = void;
  friend bool operator==(const chat_draft&, const chat_draft&) = default;
};
struct thread_draft {
  composer_doc now;
  using ModelOpaque = void;
  friend bool operator==(const thread_draft&, const thread_draft&) = default;
};
namespace request {
// The field edited: the draft as it is now.
struct chat_draft_edited {
  composer_doc now;
};
struct thread_draft_edited {
  composer_doc now;
};
}  // namespace request
// What a field bound to its draft tells: each edit, as `Edited`; Enter, as
// `Sent` -- what its arrow asks too.
template <class Edited, class Sent>
struct draft_told {
  static constexpr bool kEveryDocument = true;
  using Answer = spl::variant<Edited, Sent>;
  Answer operator()(widgets::DocumentEdited, const composer_doc& now) const { return Edited{now}; }
  Answer operator()(widgets::DocumentSubmitted, const composer_doc&) const { return Sent{}; }
};

// What is written, as it is sent and kept: each pill as it reads plain, a
// code block it opens closed at its end (as the field showed it).
[[nodiscard]] inline std::string composer_plain(const composer_doc& doc) {
  return with_blocks_closed(widgets::document::plain(doc));
}
// Whether nothing is written.
[[nodiscard]] inline bool composer_empty(const composer_doc& doc) { return doc.text.empty(); }
// What is written, as a draft is kept between chats: plain, as it reads.
[[nodiscard]] std::string draft_text(const composer_doc& doc) { return widgets::document::plain(doc); }
// Who is mentioned in it: its pills, as they are now.
[[nodiscard]] inline std::vector<mention> composer_mentions(const composer_doc& doc) {
  return doc.atoms | std::views::filter([](const widgets::TextAtom& one) { return !one.picture; }) | std::views::transform([](const widgets::TextAtom& one) { return mention{one.plain, one.target}; }) | std::ranges::to<std::vector>();
}
// How what is written is formatted, by offsets in its plain text: its runs,
// and each custom emoji's picture as a run of its own.
[[nodiscard]] inline std::vector<mux::styled_run> composer_styles(const composer_doc& doc) {
  auto runs = widgets::document::plainSpans(doc) | std::views::transform([](const field_quotes::span& one) { return mux::styled_run{one.first, one.last, one.format}; }) | std::ranges::to<std::vector>();
  runs.append_range(doc.atoms | std::views::filter(&widgets::TextAtom::picture) | std::views::transform([&](const widgets::TextAtom& atom) {
                      const std::size_t first = widgets::document::plainOffset(doc, atom.first);
                      return mux::styled_run{first, first + atom.plain.size(), run_style::custom_emoji{atom.target}};
                    }));
  return runs;
}
// What is selected, and the link on it where it has one: what Ctrl+K's box
// starts with.
[[nodiscard]] inline std::pair<std::string, std::string> composer_link_asked(const composer_doc& doc) {
  const std::size_t low = doc.low(), high = doc.high();
  const auto link = std::ranges::find_if(doc.spans, [&](const field_quotes::span& one) {
    return one.first <= low && one.last >= high &&
           spl::visit(spl::overloaded{[](const run_style::link&) { return true; }, [](const auto&) { return false; }}, one.format);
  });
  std::string url = link == doc.spans.end()
                        ? std::string()
                        : spl::visit(spl::overloaded{[](const run_style::link& one) { return one.url; }, [](const auto&) { return std::string(); }},
                                     link->format);
  return {widgets::document::selected(doc), std::move(url)};
}
// A link put on what was selected, as tdesktop's EditLinkBox: its text as
// given (what was selected replaced where it differs), the URL with a scheme
// -- https where it has none; with no URL, the links there taken off.
void put_link(composer_doc& doc, std::string text, std::string url) {
  const std::size_t low = doc.low();
  std::size_t high = doc.high();
  if (text.empty())
    text = url;
  if (text.empty())
    return;
  if (std::string_view(doc.text).substr(low, high - low) != text) {
    widgets::document::insert(doc, text, composer_grows{});
    high = low + text.size();
  }
  if (!url.empty() && !url.contains("://") && !url.starts_with("mailto:"))
    url = "https://" + url;
  const auto link = [](const field_quotes::span& one) {
    return spl::visit(spl::overloaded{[](const run_style::link&) { return true; }, [](const auto&) { return false; }}, one.format);
  };
  auto spans = field_quotes::without(doc.spans, low, high, link);
  if (!url.empty())
    spans.push_back({low, high, run_style::link{std::move(url)}});
  widgets::document::setSpans(doc, std::move(spans));
  widgets::document::select(doc, high, high);
}
// A mention picked from the list, over the @ and what was typed of it (from
// `from` on): a pill, as the message will show it, and a space. Taken apart
// with Backspace: its user's id, not the name it is sent by.
void put_mention(composer_doc& doc, std::size_t from, const std::string& name, const std::string& user) {
  widgets::document::select(doc, from, doc.text.size());
  widgets::document::insertAtom(doc, "  " + name, user, name, false, user, composer_grows{});
  widgets::document::insert(doc, " ", composer_grows{});
}
// What is written from `from` to its end replaced by `text`, the caret after
// it: a suggestion picked over what was typed for it.
void put_over_end(composer_doc& doc, std::size_t from, std::string_view text) {
  widgets::document::select(doc, from, doc.text.size());
  widgets::document::insert(doc, text, composer_grows{});
}
// A custom emoji over what is written from `from` on: its picture in the
// line, sent as its shortcode.
inline void put_picture_over_end(composer_doc& doc, std::size_t from, std::string picture, std::string glyph) {
  widgets::document::select(doc, from, doc.text.size());
  widgets::document::insertAtom(doc, "\u2003", std::move(picture), std::move(glyph), true, {}, composer_grows{});
}
// An emoji put in at the caret: its text, or -- a custom one -- its picture
// in the line, as the message will show it, sent as its shortcode.
void put_emoji(composer_doc& doc, const std::string& text, const std::string& picture) {
  if (picture.empty())
    widgets::document::insert(doc, text, composer_grows{});
  else
    widgets::document::insertAtom(doc, "\u2003", picture, text, true, {}, composer_grows{});
}
// What was selected in a message, quoted at the start of what is written.
void prepend_quote(composer_doc& doc, const scene::ClipboardFragment& selected) {
  widgets::document::select(doc, 0, 0);
  widgets::document::insertFragment(doc, quoted_fragment(selected), composer_grows{});
}
// A draft of this text and its runs, as a message edited had them.
[[nodiscard]] inline composer_doc formatted_draft(std::string text, const std::vector<mux::styled_run>& runs) {
  auto doc = widgets::document::of<run_style_t>(std::move(text));
  widgets::document::setSpans(doc, runs | std::views::transform([](const mux::styled_run& one) {
                                     return field_quotes::span{one.first, one.last, one.style};
                                   }) | std::ranges::to<std::vector>());
  return doc;
}
// A draft of plain text, the caret at its end.
[[nodiscard]] inline composer_doc plain_draft(std::string text) { return widgets::document::of<run_style_t>(std::move(text)); }

// Where a message is written: the paperclip, the field growing with what is
// written in it -- quotes and custom emoji shown as they will be sent -- the
// emoji, the arrow. The chat's composer and a thread's; what each button
// does is said by where it is. What is typed looks as it will be sent: the
// messages' size, as in tdesktop, whose field takes the message font; the
// arrow in the accent.
// The emoji button's answer: the panel toggled, opened beside where the
// button is laid out.
template <class Toggle> struct emoji_beside {
  using Answer = Toggle;
  Toggle operator()(const skia::SkRect& at) const { return {at.fRight, at.fTop}; }
};
// Which field a draft's input is, for the keys.
inline keys::chat keys_target_of(std::type_identity<chat_draft>) { return {}; }
keys::thread keys_target_of(std::type_identity<thread_draft>) { return {}; }
template <class Draft, class Told, class Attach, class Emoji, class Send>
auto message_input(const palette& colours, std::string placeholder, Attach attach_it, Emoji emoji_it, Send send_it) {
  namespace c = skiff::compose;
  auto text = widgets::TextArea<Told, message_pictures, field_quotes>(colours.widgets, std::move(placeholder), Told{});
  text.setFontSize(13.0f);
  // What it shows: the draft, as the program keeps it.
  // What it shows: the draft, as the program keeps it; and the keys, where
  // the program gives them to it.
  using target = decltype(keys_target_of(std::type_identity<Draft>{}));
  auto field = c::focused_by<keys_asked>(keys_on<target>{}, c::projected<Draft>([](const Draft& now) { return now.now; }, std::move(text)));
  return SKIFF_NAMED(message_input, c::row(c::hbox(6.0f, {.fillX = true, .autoSize = scene::axes::kY, .minHeight = 54.0f, .padding = {9.0f, 8.0f, 9.0f, 8.0f}}),
                c::styled({.alignSelf = scene::align::kEnd}, icon_button<Attach>(colours, icon::clip{}, std::move(attach_it))),
                c::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle}, std::move(field)),
                c::styled({.alignSelf = scene::align::kEnd}, icon_button<Emoji>(colours, icon::smile{}, std::move(emoji_it))),
                c::styled({.alignSelf = scene::align::kEnd}, icon_button<Send>(colours, icon::send{}, std::move(send_it), colours.accent))));
}

// The chat screen as the program sets it: the account shown, the chat
// chosen, if one is, and whether its info is open beside it.
struct chat_shown {
  std::optional<conversation_id> chosen;
  std::optional<account_id> current;
  bool info_open = false;
  // The threads' panel open, and the thread open in it, if one is.
  bool threads_open = false;
  std::optional<std::string> thread;
  // An answer being written in it: to which of its messages.
  std::optional<std::string> thread_answer;
  // The space opened as a forum, its rooms listed: by its id.
  std::optional<std::string> forum;
};
// What the chat's field writes, as what is shown holds it: a plain message,
// an answer or an edit, and what the bar over the field says of it.
struct chat_compose {
  compose_t now = compose::plain{};
  std::optional<compose_context> said;
  // Changed whole, as one part.
  using ModelOpaque = void;
};
// What a thread's field answers, as what is shown holds it: the bar over it.
struct thread_compose {
  std::optional<compose_context> said;
  using ModelOpaque = void;
};

// Where what is written goes, as a composer is told it: what its ✕, its
// Enter, its paperclip, its emoji and its arrow do, and what its empty field
// says; and whether it shows what the chat says of writing in it -- may one
// post, was it replaced, who knocks, what was not sent -- read from the
// chat. The chat's own; a thread's is its panel's.
struct in_chat {
  using cancel = sends<::mux::ui::request::cancel_compose>;
  using draft = chat_draft;
  using told = draft_told<request::chat_draft_edited, ::mux::ui::request::send_typed>;
  using attach = sends<::mux::ui::request::attach_files>;
  using emoji = emoji_beside<::mux::ui::request::toggle_emoji>;
  using send = sends<::mux::ui::request::send_typed>;
  static constexpr std::string_view placeholder = "Write a message…";
  using reads_chat = std::true_type;
  using composing = chat_compose;
};

// A bar over the field that says something and offers two ways on: the
// explanation wraps above two equally sized buttons, so both stay visible
// on phones. The second is the main action (Resend all, Approve).
template <class First, class Second>
auto two_choice_bar(const palette& colours, std::string said, skia::SkColor said_colour, std::string first_name, First first,
                    std::string second_name, Second second) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(two_choice_bar, c::column(c::vbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {8.0f, 12.0f, 8.0f, 12.0f}}),
                   c::styled({.fillX = true}, wrapped(nodes::Text(std::move(said), 13.0f, said_colour))),
                   c::row(c::hbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                          c::styled({.height = 36.0f, .grow = scene::axes::kX},
                                    widgets::Button<First>(colours.widgets, std::move(first_name), std::move(first))),
                          c::styled({.height = 36.0f, .grow = scene::axes::kX},
                                    primary(widgets::Button<Second>(colours.widgets, std::move(second_name), std::move(second)))))));
}

// Keep the successor action inside the available width, including a narrow
// pane: the explanation wraps above a separate button.
inline auto replaced_room_bar(const palette& colours) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(replaced_room_bar, c::column(c::vbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY,
      .minHeight = 54.0f, .padding = {12.0f, 12.0f, 12.0f, 12.0f}}),
      wrapped(c::styled({.fillX = true},
          nodes::Text("This room has been replaced and is no longer active.", 13.0f, colours.dim))),
      c::styled({.height = 30.0f}, primary(widgets::Button<sends<request::open_replacement>>(
          colours.widgets, "Join New Room", {})))));
}
// Where the reader may not post: a row as high as the input's, its line in
// the middle -- padded inside it, not by a margin the bar's height leaves
// out.
auto no_post_line(const palette& colours) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(no_post_line, c::row(c::justified(c::hbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY, .minHeight = 54.0f,
                                            .padding = {18.0f, 12.0f, 18.0f, 12.0f}}),
                             nodes::justify::middle{}),
                c::styled({.alignSelf = scene::align::kMiddle},
                          nodes::Text("You don't have permission to post in this chat", 13.0f, colours.dim))));
}

// Those asking to join, for those who may let them in: the first of them --
// who, and why -- with Approve (an invite) and Deny (their knock refused),
// and how many more.
struct approve_knock {
  using Answer = ::mux::ui::request::room_act;
  std::string user;
  ::mux::ui::request::room_act operator()() { return ::mux::ui::request::room_act{room_action::invite{user}}; }
};
struct deny_knock {
  using Answer = ::mux::ui::request::room_act;
  std::string user;
  ::mux::ui::request::room_act operator()() { return ::mux::ui::request::room_act{room_action::kick{user}}; }
};
struct knock_facts {
  std::string user, name, reason;
  std::size_t more = 0;
  friend bool operator==(const knock_facts&, const knock_facts&) = default;
};
[[nodiscard]] inline std::optional<knock_facts> first_knock(const ui_shared& shared, const conversation& chat) {
  if (chat.knocking.empty() || !proto::chat_rights(protocol_state_of(shared, chat.id.account), chat).invite)
    return std::nullopt;
  const knock_request& first = chat.knocking.front();
  return knock_facts{first.id, first.name, first.reason, chat.knocking.size() - 1};
}
inline auto knock_bar(const palette& colours, const knock_facts& one) {
  return two_choice_bar(colours,
                        std::format("{} asks to join{}{}", one.name, one.reason.empty() ? std::string() : ": " + one.reason,
                                    one.more ? std::format(" (and {} more)", one.more) : std::string()),
                        colours.text, "Deny", deny_knock{one.user}, "Approve", approve_knock{one.user});
}
using knock_bar_t = decltype(knock_bar(std::declval<const palette&>(), std::declval<const knock_facts&>()));
// Element's bar over the field while messages here were not sent
// (RoomStatusBar's): a warning, and "Delete all" and "Retry all".
auto unsent_bar(const palette& colours) {
  return two_choice_bar(colours, "Some of your messages have not been sent", colours.error, "Delete all",
                        sends<::mux::ui::request::discard_unsent>{}, "Resend all", sends<::mux::ui::request::retry_unsent>{});
}
// What the chat says of writing in it.
[[nodiscard]] bool any_unsent(const conversation& chat) {
  return std::ranges::any_of(chat.timeline, [](const message& said) {
    return said.outgoing &&
           spl::visit(spl::overloaded{[](const delivery::failed&) { return true; }, [](const auto&) { return false; }}, said.delivery);
  });
}
[[nodiscard]] inline bool replaced_chat(const ui_shared& shared, const conversation& chat) {
  return proto::successor_of(protocol_state_of(shared, chat.id.account), chat).has_value();
}
[[nodiscard]] bool may_post_in(const ui_shared& shared, const conversation& chat) {
  return proto::chat_rights(protocol_state_of(shared, chat.id.account), chat).post && !replaced_chat(shared, chat);
}

// A part of the composer as the chat says: read from the chat, where the
// composer is the chat's own (reads_chat); else as it is made, hidden.
template <class Compute, class Part> auto shown_by_chat(std::true_type, Compute compute, Part part) {
  return SKIFF_NAMED(shown_by_chat, skiff::compose::shown_for<conversation>(std::move(compute), std::move(part)));
}
template <class Compute, class Part> auto shown_by_chat(std::false_type, Compute, Part part) { return part; }
template <class Compute, class Part> auto read_from_chat(std::true_type, Compute compute, Part part) {
  return SKIFF_NAMED(read_from_chat, skiff::compose::projected<conversation>(std::move(compute), std::move(part)))(compute), std::move(part)));
}
template <class Compute, class Part> auto read_from_chat(std::false_type, Compute, Part part) { return part; }
// What is written answers or edits something: as what is shown says (its
// composing part).
template <class Where, class Cancel>
auto context_shown(const palette& colours, Cancel cancel) {
  return context_bar<typename Where::composing>(colours, [](const typename Where::composing& now) { return now.said; },
                                                std::move(cancel));
}
// The chat's own composer: in the chat chosen, as what is shown says it --
// the scope it reads the chat in moved to it.
template <class Column> auto in_chosen_chat(std::true_type, Column column) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(in_chosen_chat, c::rekeyed_by<chat_shown>(
      [](const chat_shown& now) {
        return now.chosen ? std::tuple{now.chosen->account, now.chosen->id} : std::tuple{account_id{}, std::string()};
      },
      c::scoped<conversation>(c::handlers(), std::move(column), account_id{}, std::string())));
}
template <class Column> auto in_chosen_chat(std::false_type, Column column) { return column; }

// Desktop: a line over it, the bars over the field -- who knocks, what was
// not sent, what is answered or edited -- the paperclip on the left, the
// text growing with what is written, and the send arrow on the right; or,
// where the reader may not post, the line saying so; or, upgraded away,
// Element's notice and the way to where it goes on. The chat's, and a
// thread's: one composer, told where it writes.
template <class Where = in_chat>
auto composer_bar(const ui_needs& n, typename Where::cancel cancel = {}, typename Where::attach attach = {},
                  typename Where::emoji emoji = {}, typename Where::send send = {}) {
  namespace c = skiff::compose;
  using reads = typename Where::reads_chat;
  const palette& colours = *n.colours;
  const ui_shared* shared = n.shared;
  return in_chosen_chat(reads{},
      c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY, .background = colours.sidebar}),
                c::styled({.fillX = true, .height = 1.0f}, nodes::Box<>(colours.band)),
                read_from_chat(reads{}, [shared](const conversation& chat) { return first_knock(*shared, chat); },
                               c::mount<knock_bar_t, knock_facts>(
                                   [at = &colours](const knock_facts& one) { return std::tuple{knock_bar(*at, one)}; },
                                   {.fillX = true, .autoSize = scene::axes::kY})),
                shown_by_chat(reads{}, [](const conversation& chat) { return any_unsent(chat); },
                              c::visible(false, unsent_bar(colours))),
                context_shown<Where>(colours, std::move(cancel)),
                shown_by_chat(reads{}, [shared](const conversation& chat) { return may_post_in(*shared, chat); },
                              message_input<typename Where::draft, typename Where::told>(
                                  colours, std::string(Where::placeholder), std::move(attach), std::move(emoji), std::move(send))),
                shown_by_chat(reads{},
                              [shared](const conversation& chat) { return !may_post_in(*shared, chat) && !replaced_chat(*shared, chat); },
                              c::visible(false, no_post_line(colours))),
                shown_by_chat(reads{}, [shared](const conversation& chat) { return replaced_chat(*shared, chat); },
                              c::visible(false, replaced_room_bar(colours)))));
}
template <class Where = in_chat> using composer_bar_t = decltype(composer_bar<Where>(std::declval<const ui_needs&>()));


}  // namespace mux::ui

