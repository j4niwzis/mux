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
    nodes::Icon mark{IconShape{}, accent_colour};  // in the left column
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
      fState.apply({.fillX = true, .height = kHeight, .padding = {0.0f, 8.0f, 0.0f, 0.0f}});
      mark.apply({.width = kSkip - 8.0f, .fillY = true});
      lines.apply({.autoSize = scene::axes::kY, .grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
      cancel.apply({.alignSelf = scene::align::kMiddle});
    }
    void forEachChild(auto&& f) {
      f(mark);
      f(lines);
      f(cancel);
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
      send.set_colour(accent_colour);
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
    context_line.mark.setShape(said ? shape_of(said->mark) : IconShape{});
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
  // A round plate with a chevron down, and over its top the count of what
  // came while the reader was above, on a badge in the accent.
  struct badge_t : nodes::Stack {
    nodes::Text count{"", 11.0f, on_accent_colour, true};
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
      count.apply({.alignSelf = scene::align::kMiddle});
      this->setVisible(false);
    }
    void forEachChild(auto&& f) { f(count); }
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
    parts.badge.count.setText(std::to_string(count));
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

}  // namespace mux::ui
