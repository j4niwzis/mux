// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:header -- A chat's head, and finding in it.
export module mux.ui:header;

import std;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes;
import skiff.widgets;
import mux.core;
import mux.config;
export import :message;

export namespace mux::ui {

// Something not there yet, said in a box over the window.
template <class Actions>
struct notice_box : nodes::Stack {
  nodes::Text title;
  nodes::Text note;
  widgets::Button<ask<Actions, &Actions::close_notice>> ok;

  notice_box(Actions* a, std::string heading, std::string text)
      : title(std::move(heading), 17.0f, text_colour, true), note(std::move(text), 14.0f, dim_colour), ok("OK", {a}) {
    fState.apply({.fill = true, .padding = {20.0f, 22.0f, 20.0f, 22.0f}});
    this->setGap(10.0f);
    title.setWrapped(true);
    title.apply({.fillX = true});
    note.setWrapped(true);
    note.apply({.fillX = true});
    ok.setPrimary(true);
    ok.apply({.width = 90.0f, .height = 34.0f, .alignSelf = scene::align::kEnd});
  }
  void forEachChild(auto&& f) {
    f(title);
    f(note);
    f(ok);
  }
};


// What a chat says of itself, over its messages: its avatar, name and who
// is in it or how they are, a line under it, and the button that opens its
// info beside it.
template <class Actions>
struct chat_header : nodes::Stack {
  // What the head shows: the chat's key and name, and how it is -- or no
  // chat. The head is made from it, nothing set in it afterwards.
  struct view {
    std::optional<std::string> key;
    std::string title = "Choose a chat";
    std::string status;
    friend bool operator==(const view&, const view&) = default;
  };
  [[nodiscard]] static view view_of(const conversation* one, const model& now) {
    if (one == nullptr)
      return {};
    const auto count = std::max<std::int64_t>(static_cast<std::int64_t>(one->members.size()), one->member_count);
    std::string about = is_group(*one) ? std::format("{} member{}", count, count == 1 ? "" : "s")
                                       : presence_of(now, one->id.account, one->id.id);
    if (!one->typing.empty())
      about = one->typing.size() == 1 ? sender_name(*one, one->typing.front()) + " is typing…"
                                      : std::format("{} are typing…", one->typing.size());
    return {one->id.id, display_name(*one), std::move(about)};
  }

  // The chat's avatar, its name over how it is, and the button to its info.
  struct head_row : nodes::Stack {
    avatar_mark face;
    two_lines texts;
    icon_button<ask<Actions, &Actions::open_search>> find;
    icon_button<ask<Actions, &Actions::toggle_info>> info;
    head_row(Actions* a, const view& shown)
        : face(shown.key.value_or(""), shown.title, 38.0f), texts(shown.title, shown.status, 15.0f, 3.0f),
          find(icon::search{}, {a}), info(icon::info{}, {a}) {
      this->setHorizontal();
      this->setGap(12.0f);
      fState.apply({.fillX = true, .grow = scene::axes::kY, .padding = {0.0f, 10.0f, 0.0f, 14.0f}});
      find.apply({.alignSelf = scene::align::kMiddle});
      info.apply({.alignSelf = scene::align::kMiddle});
      face.setVisible(shown.key.has_value());
      find.setVisible(shown.key.has_value());
      info.setVisible(shown.key.has_value());
      texts.state.setVisible(shown.key.has_value());
    }
    void forEachChild(auto&& f) {
      f(face);
      f(texts);
      f(find);
      f(info);
    }
  } row;
  nodes::Box<> divider{band_colour};

  static constexpr float kHeight = 56.0f;

  // Declared: the row over a line dividing it from the messages.
  chat_header(Actions* a, const view& shown) : row(a, shown) {
    fState.apply({.fill = true, .background = sidebar_colour});
    divider.apply({.fillX = true, .height = 1.0f});
  }

  void forEachChild(auto&& f) {
    f(row);
    f(divider);
  }
};

// Finding in a chat, as tdesktop's search in a chat: in place of the head,
// a field with the magnifier, how many are found and which is shown ("3 of
// 12"), the arrows to the newer and the older, and ✕. Enter goes to the
// older one, Shift+Enter to the newer, Esc closes.
template <class Actions>
struct search_typed {
  Actions* actions = nullptr;
  void operator()(std::string_view text) const { actions->search_typed(std::string(text)); }
};
template <class Actions>
struct search_step {
  Actions* actions = nullptr;
  bool older = true;
  void operator()() const { actions->search_step(older); }
};
template <class Actions>
struct search_bar : nodes::Stack {
  Actions* actions;
  widgets::TextBox<search_typed<Actions>> field;
  nodes::Text found{"", 13.0f, dim_colour};
  icon_button<search_step<Actions>> newer, older;
  icon_button<ask<Actions, &Actions::close_search>> close;

  explicit search_bar(Actions* a)
      : actions(a), field("Search", {a}), newer(icon::up{}, {a, false}), older(icon::down{}, {a, true}),
        close(icon::close{}, {a}) {
    this->setHorizontal();
    this->setGap(4.0f);
    fState.apply({.fillX = true, .height = chat_header<Actions>::kHeight, .padding = {0.0f, 10.0f, 1.0f, 14.0f}});
    field.setSearchIcon(true);
    field.apply({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
    found.apply({.alignSelf = scene::align::kMiddle});
    newer.apply({.alignSelf = scene::align::kMiddle});
    older.apply({.alignSelf = scene::align::kMiddle});
    close.apply({.alignSelf = scene::align::kMiddle});
    this->setVisible(false);
  }
  // Where the finding is: the one shown of how many, or none found.
  void show_found(std::optional<std::size_t> at, std::size_t of, bool asked) {
    found.setText(!asked ? std::string() : of == 0 ? std::string("No results") : std::format("{} of {}", at.value_or(0) + 1, of));
    this->invalidateLayout();
  }
  void forEachChild(auto&& f) {
    f(field);
    f(found);
    f(newer);
    f(older);
    f(close);
  }
  using Node::onKey;
  void onKey(scene::phase::bubble, const scene::key::down& press, scene::Reply& reply) {
    if (press.key == scene::keys::kEnter) {
      actions->search_step(!press.modifiers.template has<scene::modifier::shift>());
      reply.handle();
    } else if (press.key == scene::keys::kEscape) {
      actions->close_search();
      reply.handle();
    } else if (press.key == scene::keys::kUp) {
      actions->search_step(false);
      reply.handle();
    } else if (press.key == scene::keys::kDown) {
      actions->search_step(true);
      reply.handle();
    }
  }
  void drawSelf(skia::SkCanvas* canvas, float alpha) {
    if (skia::SkFont* font = skiff::paint::defaultFont()) {
      const skiff::paint::Painter p(canvas, *font);
      p.fillRounded(fState.fBounds, 0.0f, sidebar_colour, alpha);
      const skia::SkRect& at = fState.fBounds;
      p.fillRounded(skia::SkRect::MakeLTRB(at.left(), at.bottom() - 1.0f, at.right(), at.bottom()), 0.0f, band_colour,
                    alpha);
    }
  }
};

}  // namespace mux::ui
