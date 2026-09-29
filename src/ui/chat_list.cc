// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:chat_list -- A chat in the list.
export module mux.ui:chat_list;

import std;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes;
import skiff.widgets;
import mux.core;
import mux.config;
export import :html;

export namespace mux::ui {

// One chat in the list, as Telegram Desktop draws it: a round avatar, the
// name, the time of the last message, a line of it, and how many are unread.
template <class Actions>
struct conversation_row : nodes::Stack {
  Actions* actions = nullptr;
  conversation_id id;
  bool chosen = false;
  bool muted = false;
  avatar_mark face;
  // The name and the time over the last message and the unread count.
  struct lines_column : nodes::Stack {
    struct top_line : nodes::Stack {
      nodes::Text name;
      nodes::Text time;
      top_line(std::string shown, bool chosen)
          : name(std::move(shown), 13.0f, chosen ? selected_text_colour : text_colour, true),
            time("", 13.0f, chosen ? selected_text_colour : dim_colour) {
        this->setHorizontal();
        this->setGap(8.0f);
        fState.apply({.fillX = true, .autoSize = scene::axes::kY});
        name.setElided(true);
        name.apply({.grow = scene::axes::kX});
      }
      void forEachChild(auto&& f) {
        f(name);
        f(time);
      }
    } top;
    struct bottom_line : nodes::Stack {
      nodes::Text preview;
      // The chats' unread count, in a pill.
      struct badge : widgets::Pill {
        badge(std::int64_t n, bool is_chosen, bool is_muted)
            : widgets::Pill(std::to_string(n),
                            {.plate = is_chosen ? selected_text_colour : is_muted ? dim_colour : accent_colour,
                             .text = is_chosen ? selected_colour : on_accent_colour,
                             .size = 12.0f,
                             .height = 21.0f,
                             .padX = 7.0f,
                             .bold = true}) {}
      } unread;
      bottom_line(std::int64_t count, bool chosen, bool muted)
          : preview("", 13.0f, chosen ? selected_text_colour : dim_colour), unread(count, chosen, muted) {
        this->setHorizontal();
        this->setGap(8.0f);
        fState.apply({.fillX = true, .autoSize = scene::axes::kY});
        preview.setElided(true);
        preview.apply({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
        unread.setVisible(count > 0);
      }
      void forEachChild(auto&& f) {
        f(preview);
        f(unread);
      }
    } bottom;
    lines_column(std::string shown, std::int64_t count, bool chosen, bool muted)
        : top(std::move(shown), chosen), bottom(count, chosen, muted) {
      this->setGap(6.0f);
      fState.apply({.autoSize = scene::axes::kY, .grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
    }
    void forEachChild(auto&& f) {
      f(top);
      f(bottom);
    }
  } lines;

  static constexpr float kHeight = 62.0f;

  // Declared: the avatar, then the name and time over the last message and
  // how many are unread.
  // What a row shows of its chat: while that is the same, the row is kept.
  struct view {
    std::string name;
    std::optional<message> last;
    std::int64_t unread = 0;
    bool chosen = false, muted = false;
    std::string draft;
    friend bool operator==(const view&, const view&) = default;
  };
  [[nodiscard]] static view view_of(const conversation& one, bool is_chosen, bool is_muted, std::string draft = {}) {
    return {display_name(one), newest(one) ? std::optional<message>(*newest(one)) : std::nullopt,
            one.unread_here(), is_chosen, is_muted, std::move(draft)};
  }
  view shown;

  conversation_row(Actions* a, const conversation& one, bool is_chosen, bool is_muted, std::string draft = {})
      : actions(a), id(one.id), chosen(is_chosen), muted(is_muted), shown(view_of(one, is_chosen, is_muted, draft)),
        face(one.id.id, display_name(one), 46.0f),
        lines(display_name(one), one.unread_here(), is_chosen, is_muted) {
    this->setHorizontal();
    this->setGap(12.0f);
    fState.apply({.fillX = true, .height = kHeight, .padding = {0.0f, 12.0f, 0.0f, 10.0f}, .hoverBackground = chosen_colour, .selectedBackground = selected_colour, .focusBackground = chosen_colour, .selected = chosen});
    if (const message* newest_one = newest(one)) {
      const message& last = *newest_one;
      lines.top.time.setText(clock_of(last.at));
      // What it says, as drawn: an HTML one's text, not its tags.
      std::string text = last.redacted ? "(removed)"
                         : last.body.html ? read_html(*last.body.html).text
                                          : last.body.plain;
      std::ranges::replace(text, '\n', ' ');
      if (last.outgoing)
        text = "You: " + text;
      else if (is_group(one))
        text = sender_name(one, last.sender) + ": " + text;
      lines.bottom.preview.setText(std::move(text));
    }
    // A draft left in it: said instead, as tdesktop says it, in red.
    if (!shown.draft.empty() && !is_chosen) {
      std::string text = shown.draft;
      std::ranges::replace(text, '\n', ' ');
      lines.bottom.preview.setText("Draft: " + text);
      lines.bottom.preview.setColour(error_colour);
    }
  }

  void forEachChild(auto&& f) {
    f(face);
    f(lines);
  }

  [[nodiscard]] bool acceptsInput() const { return true; }
  [[nodiscard]] bool hoverChangesAppearance() const { return true; }
  [[nodiscard]] bool onClick(float, float) {
    actions->choose(id);
    return true;
  }
  [[nodiscard]] scene::Semantics semantics() const {
    scene::Semantics out;
    out.fRole = scene::semantic_role::list_item{};
    out.fLabel = lines.top.name.text();
    out.fSelected = chosen;
    out.fActions = {scene::semantic_action::focus{}, scene::semantic_action::activate{}};
    return out;
  }
};

}  // namespace mux::ui
