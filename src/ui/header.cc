// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:header -- A chat's head, and finding in it.
export module mux.ui:header;

import std;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes.box;
import skiff.nodes.flow;
import skiff.nodes.text;
import skiff.widgets.button;
import skiff.widgets.textbox;
import mux.core;
import mux.config;
import :base;
import :icons;
import :controls;
import :themes;
import :names;

export namespace mux::ui {

// Something not there yet, said in a box over the window.
template <class Actions>
struct notice_box : nodes::Stack {
  using ok_button = widgets::Button<ask<Actions, &Actions::close_notice>>;
  struct parts_t {
    nodes::Text title;
    nodes::Text note;
    ok_button ok;
  } parts;

  notice_box(Actions* a, std::string heading, std::string text)
      : parts{.title = nodes::Text(std::move(heading), 17.0f, text_colour, true),
              .note = nodes::Text(std::move(text), 14.0f, dim_colour),
              .ok = ok_button("OK", {a})} {
    // As high as what it says: no room left empty under its button.
    fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {20.0f, 22.0f, 20.0f, 22.0f}});
    this->setGap(10.0f);
    // What it says can be selected and copied.
    for (nodes::Text* each : {&parts.title, &parts.note}) {
      each->setWrapped(true);
      each->setSelectable(true);
      each->apply({.fillX = true});
    }
    parts.ok.setPrimary(true);
    parts.ok.apply({.width = 90.0f, .height = 34.0f, .alignSelf = scene::align::kEnd});
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
                                       : presence_of(now, one->id.account, contact_of(*one));
    // Who is typing, by their names, as Telegram says it: one, two ("A and
    // B"), three ("A, B and C"); past three, the first two and how many more.
    if (!one->typing.empty()) {
      std::vector<std::string> names;
      for (const std::string& who : one->typing)
        names.push_back(sender_name(*one, who));
      switch (names.size()) {
        case 1: about = names[0] + " is typing…"; break;
        case 2: about = std::format("{} and {} are typing…", names[0], names[1]); break;
        case 3: about = std::format("{}, {} and {} are typing…", names[0], names[1], names[2]); break;
        default:
          about = std::format("{}, {} and {} more are typing…", names[0], names[1], names.size() - 2);
      }
    }
    return {one->id.id, display_name(*one), std::move(about)};
  }

  // The chat's avatar, its name over how it is, and the button to its info.
  struct head_row : nodes::Stack {
    using find_button = icon_button<ask<Actions, &Actions::open_search>>;
    using info_button = icon_button<ask<Actions, &Actions::toggle_info>>;
    using threads_button = icon_button<ask<Actions, &Actions::toggle_threads>>;
    struct parts_t {
      avatar_mark face;
      two_lines texts;
      find_button find;
      // The room's threads, as Element's header has them.
      threads_button threads;
      info_button info;
    } parts;
    head_row(Actions* a, const view& shown)
        : parts{.face = avatar_mark(shown.key.value_or(""), shown.title, 38.0f),
                .texts = two_lines(shown.title, shown.status, 15.0f, 3.0f),
                .find = find_button(icon::search{}, {a}),
                .threads = threads_button(icon::threads{}, {a}),
                .info = info_button(icon::info{}, {a})} {
      this->setHorizontal();
      this->setGap(12.0f);
      fState.apply({.fillX = true, .grow = scene::axes::kY, .padding = {0.0f, 16.0f, 0.0f, 22.0f}});
      parts.find.apply({.alignSelf = scene::align::kMiddle});
      parts.threads.apply({.alignSelf = scene::align::kMiddle});
      parts.threads.setVisible(shown.key.has_value());
      parts.info.apply({.alignSelf = scene::align::kMiddle});
      parts.face.setVisible(shown.key.has_value());
      parts.find.setVisible(shown.key.has_value());
      parts.info.setVisible(shown.key.has_value());
      parts.texts.parts.state.setVisible(shown.key.has_value());
    }
  };
  struct parts_t {
    head_row row;
    nodes::Box<> divider{band_colour};
  } parts;

  static constexpr float kHeight = 56.0f;

  // Declared: the row over a line dividing it from the messages.
  chat_header(Actions* a, const view& shown) : parts{.row = head_row(a, shown)} {
    fState.apply({.fill = true, .background = sidebar_colour});
    parts.divider.apply({.fillX = true, .height = 1.0f});
  }
};

// The pinned message, as tdesktop's bar under a chat's head: a stripe in
// the accent, "Pinned message" -- "#2 of 3" where there are more -- over a
// line of it. A press goes to it, and the bar to the one pinned before it,
// round, as Telegram's.
struct pinned_view {
  std::string id;
  std::string title;
  std::string line;
  friend bool operator==(const pinned_view&, const pinned_view&) = default;
};
template <class Press>
struct pinned_bar : nodes::Stack {
  Press press;
  struct parts_t {
    nodes::Box<> stripe{accent_colour};
    two_lines texts;
    nodes::Box<> divider{band_colour};
  } parts;
  static constexpr float kHeight = 46.0f;
  pinned_bar(Press what, const pinned_view& shown)
      : press(std::move(what)), parts{.texts = two_lines(shown.title, shown.line, 13.0f, 2.0f)} {
    this->setHorizontal();
    this->setGap(10.0f);
    fState.apply({.fill = true, .padding = {6.0f, 16.0f, 7.0f, 18.0f}, .background = sidebar_colour,
                  .hoverBackground = chosen_colour});
    fState.setCursor(scene::cursor::hand{});
    parts.stripe.apply({.fillY = true, .width = 2.0f, .cornerRadius = 1.0f});
    parts.texts.parts.name.setColour(accent_colour);
    parts.divider.apply({.place = scene::anchor::kBottomLeft, .fillX = true, .height = 1.0f});
  }
  [[nodiscard]] bool acceptsInput() const { return true; }
  [[nodiscard]] bool hoverChangesAppearance() const { return true; }
  [[nodiscard]] bool onClick(float, float) {
    press();
    return true;
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
  using field_t = widgets::TextBox<search_typed<Actions>>;
  using step_button = icon_button<search_step<Actions>>;
  using close_button = icon_button<ask<Actions, &Actions::close_search>>;
  struct parts_t {
    field_t field;
    nodes::Text found{"", 13.0f, dim_colour};
    step_button newer, older;
    close_button close;
    nodes::Box<> bottom_line{band_colour};
  } parts;

  explicit search_bar(Actions* a)
      : actions(a), parts{.field = field_t("Search", {a}),
                          .newer = step_button(icon::up{}, {a, false}),
                          .older = step_button(icon::down{}, {a, true}),
                          .close = close_button(icon::close{}, {a})} {
    auto& [field, found, newer, older, close, bottom_line] = parts;
    this->setHorizontal();
    this->setGap(4.0f);
    fState.apply({.fillX = true, .height = chat_header<Actions>::kHeight, .padding = {0.0f, 16.0f, 1.0f, 22.0f},
                  .background = sidebar_colour});
    bottom_line.apply({.place = scene::anchor::kBottomLeft, .fillX = true, .height = 1.0f});
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
    parts.found.setText(!asked ? std::string() : of == 0 ? std::string("No results") : std::format("{} of {}", at.value_or(0) + 1, of));
    this->invalidateLayout();
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
};

}  // namespace mux::ui
