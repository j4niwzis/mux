// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:composer -- Where a message is written.
export module mux.ui:composer;

import std;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes;
import skiff.widgets;
import mux.core;
import mux.config;
export import :info;

export namespace mux::ui {

// An edge between two parts of the window, to drag: the pointer turns into
// a resize arrow over it, and a drag asks `on_drag(x)` for the edge to be at
// x. It draws a thin line where `with_line`.
template <class OnDrag>
struct drag_edge : scene::Node {
  OnDrag on_drag;
  bool with_line = true;
  bool dragging = false;

  explicit drag_edge(OnDrag what, bool line = true) : on_drag(std::move(what)), with_line(line) {
    fState.setCursor(scene::cursor::resize_horizontal{});
  }

  void drawSelf(skia::SkCanvas* canvas, float alpha) {
    skia::SkFont* font = skiff::paint::defaultFont();
    if (font == nullptr || !with_line)
      return;
    const skia::SkRect& box = fState.fBounds;
    skiff::paint::Painter(canvas, *font)
        .fillRounded(skia::SkRect::MakeXYWH(box.centerX() - 0.5f, box.fTop, 1.0f, box.height()), 0.0f, band_colour,
                     alpha);
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
// Desktop: a line over it, a paperclip on the left, the text growing with
// what is written, and the send arrow on the right.
template <class Actions>
struct composer_bar : nodes::Stack {
  nodes::Box<> divider{band_colour};
  // What is written answers or edits, as tdesktop's FieldHeader shows it:
  // its icon in the left column (historyReplySkip wide), then two lines --
  // "Reply to <name>" or "Edit message" in the accent, semibold, over a
  // line of the message -- and ✕ on the right to go back to a plain one.
  struct context_row : nodes::Stack {
    static constexpr float kHeight = 49.0f;  // historyReplyHeight
    static constexpr float kSkip = 51.0f;    // historyReplySkip
    icon_t mark = icon::none{};
    struct lines_column : nodes::Stack {
      nodes::Text title{"", 13.0f, accent_colour, true};
      nodes::Text line{"", 13.0f, text_colour};
      lines_column() {
        this->setGap(2.0f);
        title.setElided(true);
        line.setElided(true);
        title.apply({.fillX = true});
        line.apply({.fillX = true});
      }
      void forEachChild(auto&& f) {
        f(title);
        f(line);
      }
    } lines;
    icon_button<ask<Actions, &Actions::cancel_compose>> cancel;
    explicit context_row(Actions* a) : cancel(icon::close{}, {a}) {
      this->setHorizontal();
      this->setGap(8.0f);
      fState.apply({.fillX = true, .height = kHeight, .padding = {0.0f, 8.0f, 0.0f, kSkip}});
      lines.apply({.autoSize = scene::axes::kY, .grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
      cancel.apply({.alignSelf = scene::align::kMiddle});
    }
    void forEachChild(auto&& f) {
      f(lines);
      f(cancel);
    }
    void drawSelf(skia::SkCanvas* canvas, float alpha) {
      const skia::SkRect& at = fState.fBounds;
      draw_icon(canvas, mark, skia::SkRect::MakeLTRB(at.left(), at.top(), at.left() + kSkip, at.bottom()), accent_colour,
                alpha);
    }
  } context_line;
  // The paperclip, the field growing with what is written in it, the arrow.
  struct input_row : nodes::Stack {
    icon_button<ask<Actions, &Actions::attach_files>> attach;
    widgets::TextArea<submit_message<Actions>> field;
    icon_button<ask<Actions, &Actions::send_typed>> send;
    explicit input_row(Actions* a)
        : attach(icon::clip{}, {a}), field("Write a message…", {a}), send(icon::send{}, {a}) {
      this->setHorizontal();
      this->setGap(6.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY, .minHeight = 54.0f, .padding = {9.0f, 8.0f, 9.0f, 8.0f}});
      attach.apply({.alignSelf = scene::align::kEnd});
      send.apply({.alignSelf = scene::align::kEnd});
      field.apply({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
      send.colour = accent_colour;
    }
    void forEachChild(auto&& f) {
      f(attach);
      f(field);
      f(send);
    }
  } input;
  // The old names, for what reads them.
  widgets::TextArea<submit_message<Actions>>& field = input.field;

  // Declared: the divider, the answer's line where there is one, the row.
  explicit composer_bar(Actions* a) : context_line(a), input(a) {
    context_line.setVisible(false);
    fState.apply({.fillX = true, .autoSize = scene::axes::kY, .background = sidebar_colour});
    divider.apply({.fillX = true, .height = 1.0f});
  }

  [[nodiscard]] const std::string& text() const { return input.field.text(); }
  // What is written answers or edits something, shown; or nothing.
  void show_context(std::optional<compose_context> said) {
    context_line.setVisible(said.has_value());
    context_line.mark = said ? said->mark : icon_t{icon::none{}};
    context_line.lines.title.setText(said ? said->title : std::string());
    context_line.lines.line.setText(said ? said->line : std::string());
    context_line.markDamaged();
    this->invalidateLayout();
  }
  void set_text(std::string text) { input.field.setText(std::move(text)); }
  void clear() { input.field.setText({}); }

  void forEachChild(auto&& f) {
    f(divider);
    f(context_line);
    f(input);
  }
};

// "↓": back to the newest, with how many came while one read above them.
template <class Actions>
struct jump_button : scene::Node {
  Actions* actions = nullptr;
  int unseen = 0;
  explicit jump_button(Actions* a) : actions(a) {
    fState.apply({.place = scene::anchor::kBottomRight, .x = -18.0f, .y = -12.0f, .width = 42.0f, .height = 42.0f});
  }
  void set_unseen(int count) {
    unseen = count;
    this->markDamaged();
  }
  void drawSelf(skia::SkCanvas* canvas, float alpha) {
    skia::SkFont* font = skiff::paint::defaultFont();
    if (font == nullptr)
      return;
    const skiff::paint::Painter p(canvas, *font);
    const skia::SkRect& box = fState.fBounds;
    p.fillRounded(box, box.width() * 0.5f, fState.fHovered ? chosen_colour : sidebar_colour, alpha);
    p.strokeRounded(box, box.width() * 0.5f, band_colour, 1.0f, alpha);
    const float x = box.centerX(), y = box.centerY() + 2.0f;
    const auto line = pen(text_colour, alpha, 2.0f);
    canvas->drawLine(x - 7.0f, y - 4.0f, x, y + 3.0f, line);
    canvas->drawLine(x, y + 3.0f, x + 7.0f, y - 4.0f, line);
    if (unseen > 0) {
      const std::string count = std::to_string(unseen);
      const float width = std::max(20.0f, p.measure(count, 11.0f, true) + 10.0f);
      const skia::SkRect badge = skia::SkRect::MakeXYWH(x - width * 0.5f, box.fTop - 10.0f, width, 18.0f);
      p.fillRounded(badge, 9.0f, accent_colour, alpha);
      p.textIn(badge, count, 11.0f, on_accent_colour, alpha, true, (width - p.measure(count, 11.0f, true)) * 0.5f);
    }
  }
  [[nodiscard]] bool acceptsInput() const { return true; }
  [[nodiscard]] bool hoverChangesAppearance() const { return true; }
  [[nodiscard]] bool onClick(float, float) {
    actions->jump_to_end();
    return true;
  }
};

}  // namespace mux::ui
