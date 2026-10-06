// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:header -- A chat's head, and finding in it.
export module mux.ui:header;

import std;
import splice;
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
// A notice's box: as high as what it says, its heading and text wrapped,
// and selectable, to be copied.
inline void lay_out_notice(nodes::Stack& box, nodes::Text& title, nodes::Text& note) {
  box.fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {20.0f, 22.0f, 20.0f, 22.0f}});
  box.setGap(10.0f);
  for (nodes::Text* each : {&title, &note}) {
    each->setWrapped(true);
    each->setSelectable(true);
    each->apply({.fillX = true});
  }
}

template <class Actions>
struct notice_box : nodes::Stack {
  // The dialog it is shown in.
  [[nodiscard]] static dialog_look look_of_dialog() { return {.size = dialog_size::fitting{440.0f}}; }
  using ok_button = widgets::Button<ask<Actions, &Actions::close_notice>>;
  struct parts_t {
    nodes::Text title;
    nodes::Text note;
    ok_button ok;
  } parts;

  notice_box(const ui_needs<Actions>& n, std::string heading, std::string text)
      : notice_box(*n.colours, n.actions, std::move(heading), std::move(text)) {}
  notice_box(const palette& colours, Actions* a, std::string heading, std::string text)
      : parts{.title = nodes::Text(std::move(heading), 17.0f, colours.text, true),
              .note = nodes::Text(std::move(text), 14.0f, colours.dim),
              .ok = ok_button(colours.widgets, "OK", {a})} {
    // As high as what it says: no room left empty under its button.
    lay_out_notice(*this, parts.title, parts.note);
    parts.ok.setPrimary(true);
    parts.ok.apply({.width = 90.0f, .height = 34.0f, .alignSelf = scene::align::kEnd});
  }
};


// An emoji verification, as Element shows it: with whom, and where it is --
// asked of you (Accept, Decline), waiting on them (Cancel), the 7 emoji to
// compare with what their screen shows (They match, They don't match), done,
// or stopped and why (OK).
struct verification_view {
  std::string user;
  std::string device;
  verification_step_t step;
};
template <class Actions>
struct verification_box : nodes::Stack {
  // The dialog it is shown in.
  [[nodiscard]] static dialog_look look_of_dialog() { return {.size = dialog_size::fitting{440.0f}, .dismissable = false}; }
  template <auto Member>
  struct press {
    Actions* actions;
    void operator()() const { (actions->*Member)(); }
  };
  using accept_button = widgets::Button<press<&Actions::verify_accept_now>>;
  using decline_button = widgets::Button<press<&Actions::verify_cancel_now>>;
  using match_button = widgets::Button<press<&Actions::verify_match>>;
  using mismatch_button = widgets::Button<press<&Actions::verify_mismatch>>;
  using close_button = widgets::Button<press<&Actions::close_verification>>;
  // One emoji, big, its name under it.
  struct emoji_cell : nodes::Stack {
    struct parts_t {
      nodes::Text picture;
      nodes::Text name;
    } parts;
    emoji_cell(const palette& colours, std::string_view picture, std::string_view name)
        : parts{.picture = nodes::Text(std::string(picture), 30.0f, colours.text),
                .name = nodes::Text(std::string(name), 11.0f, colours.dim)} {
      this->setGap(4.0f);
      fState.apply({.width = 52.0f, .autoSize = scene::axes::kY});
      for (nodes::Text* each : {&parts.picture, &parts.name})
        each->apply({.alignSelf = scene::align::kMiddle});
    }
  };
  struct emoji_row : nodes::Stack {
    struct parts_t {
      std::vector<emoji_cell> cells;
    } parts;
    emoji_row(const palette& colours, const std::array<int, 7>& indices) {
      this->setHorizontal();
      this->setGap(4.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY});
      parts.cells.reserve(indices.size());
      for (const int index : indices)
        parts.cells.emplace_back(colours, sas_emoji[static_cast<std::size_t>(index & 63)].first,
                                 sas_emoji[static_cast<std::size_t>(index & 63)].second);
    }
  };
  struct parts_t {
    nodes::Text title;
    nodes::Text note;
    std::optional<emoji_row> emoji;
    accept_button accept;
    decline_button decline;
    match_button match;
    mismatch_button mismatch;
    close_button close;
  } parts;

  verification_box(const ui_needs<Actions>& n, const verification_view& view)
      : verification_box(*n.colours, n.actions, view) {}
  verification_box(const palette& colours, Actions* a, const verification_view& view)
      : parts{.title = nodes::Text("Verify " + view.user, 17.0f, colours.text, true),
              .note = nodes::Text(note_of(view), 14.0f, colours.dim),
              .accept = accept_button(colours.widgets, "Accept", {a}),
              .decline = decline_button(colours.widgets, declines(view.step) ? "Decline" : "Cancel", {a}),
              .match = match_button(colours.widgets, "They match", {a}),
              .mismatch = mismatch_button(colours.widgets, "They don't match", {a}),
              .close = close_button(colours.widgets, "OK", {a})} {
    lay_out_notice(*this, parts.title, parts.note);
    spl::visit(spl::overloaded{[&](const verification_step::compare& shown) { parts.emoji.emplace(colours, shown.emoji); },
                                     [](const auto&) {}},
                  view.step);
    const auto shown_in = [&](auto in_step) { return spl::visit(in_step, view.step); };
    parts.accept.setVisible(shown_in(spl::overloaded{[](verification_step::asked) { return true; }, [](const auto&) { return false; }}));
    parts.decline.setVisible(shown_in(spl::overloaded{[](verification_step::asked) { return true; },
                                                         [](verification_step::waiting) { return true; },
                                                         [](const auto&) { return false; }}));
    const bool comparing = shown_in(spl::overloaded{[](const verification_step::compare&) { return true; }, [](const auto&) { return false; }});
    parts.match.setVisible(comparing);
    parts.mismatch.setVisible(comparing);
    parts.close.setVisible(shown_in(spl::overloaded{[](verification_step::done) { return true; },
                                                       [](const verification_step::cancelled&) { return true; },
                                                       [](const auto&) { return false; }}));
    parts.accept.setPrimary(true);
    parts.match.setPrimary(true);
    parts.close.setPrimary(true);
    parts.accept.apply({.width = 120.0f, .height = 34.0f, .alignSelf = scene::align::kEnd});
    parts.decline.apply({.width = 120.0f, .height = 34.0f, .alignSelf = scene::align::kEnd});
    parts.match.apply({.width = 160.0f, .height = 34.0f, .alignSelf = scene::align::kEnd});
    parts.mismatch.apply({.width = 160.0f, .height = 34.0f, .alignSelf = scene::align::kEnd});
    parts.close.apply({.width = 90.0f, .height = 34.0f, .alignSelf = scene::align::kEnd});
  }
  [[nodiscard]] static bool declines(const verification_step_t& step) {
    return spl::visit(spl::overloaded{[](verification_step::asked) { return true; }, [](const auto&) { return false; }}, step);
  }
  [[nodiscard]] static std::string note_of(const verification_view& view) {
    const std::string device = view.device.empty() ? std::string("one of their devices") : "device " + view.device;
    return spl::visit(
        spl::overloaded{
            [&](verification_step::asked) { return std::format("{} ({}) asks to verify with emoji.", view.user, device); },
            [&](verification_step::waiting) { return std::format("Waiting for {} ({})\u2026", view.user, device); },
            [&](const verification_step::compare&) {
              return std::string("Compare these emoji with the ones on the other screen, in the same order. Only if all of "
                                 "them match, say so.");
            },
            [&](verification_step::done) { return std::format("Verified: {} ({}).", view.user, device); },
            [&](const verification_step::cancelled& why) { return "Not verified: " + why.reason; }},
        view.step);
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
    // A back arrow before it, to the chats: shown one thing at a time.
    bool back = false;
    // A call can be made in it: its protocol calls, and it is two.
    bool callable = false;
    friend bool operator==(const view&, const view&) = default;
  };
  [[nodiscard]] static view view_of(const ui_shared& shared, const conversation* one, const model& now) {
    if (one == nullptr)
      return {};
    const auto count = std::max<std::int64_t>(static_cast<std::int64_t>(one->members.size()), one->member_count);
    // As its protocol says it, where it does (a channel's subscribers);
    // else its members, or how the other is.
    std::string about = proto::chat_status(protocol_state_of(shared, one->id.account), *one, now)
                            .value_or(is_group(*one) ? std::format("{} member{}", count, count == 1 ? "" : "s")
                                                     : presence_of(shared, now, one->id.account, contact_of(shared, *one)));
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
    // What its protocol says of it after (proto::header_badges): Matrix's
    // shield, for one.
    about = std::ranges::fold_left(proto::header_badges(protocol_state_of(shared, one->id.account), *one, now), std::move(about),
                                   [](std::string so_far, const proto::part::badge& badge) {
                                     return so_far.empty() ? badge.text : std::format("{} \u00b7 {}", so_far, badge.text);
                                   });
    return {.key = one->id.id,
            .title = display_name(*one),
            .status = std::move(about),
            .callable = ops_of(shared, one->id.account).calls && count <= 2};
  }

  // The chat's avatar, its name over how it is, and the button to its info.
  struct head_row : nodes::Stack {
    using find_button = icon_button<ask<Actions, &Actions::open_search>>;
    using info_button = icon_button<ask<Actions, &Actions::toggle_info>>;
    using threads_button = icon_button<ask<Actions, &Actions::toggle_threads>>;
    using call_button = icon_button<ask<Actions, &Actions::call_chosen>>;
    using back_button = icon_button<ask<Actions, &Actions::close_chat>>;
    // Shown one thing at a time, a tap on the chat's name or avatar opens
    // its info, as on Telegram's phones.
    Actions* actions = nullptr;
    bool taps_to_info = false;
    struct parts_t {
      back_button back;
      avatar_mark face;
      two_lines texts;
      find_button find;
      // A call to the other, where the chat is two and its protocol calls.
      call_button call;
      // The room's threads, as Element's header has them.
      threads_button threads;
      info_button info;
    } parts;
    head_row(const palette& colours, Actions* a, const view& shown)
        : actions(a), taps_to_info(shown.back && shown.key.has_value()),
          parts{.back = back_button(colours, icon::back{}, {a}),
                .face = avatar_mark(shown.key.value_or(""), shown.title, 38.0f),
                .texts = two_lines(colours, shown.title, shown.status, 15.0f, 3.0f),
                .find = find_button(colours, icon::search{}, {a}),
                .call = call_button(colours, icon::phone{}, {a}),
                .threads = threads_button(colours, icon::threads{}, {a}),
                .info = info_button(colours, icon::info{}, {a})} {
      this->setHorizontal();
      this->setGap(12.0f);
      fState.apply({.fillX = true, .grow = scene::axes::kY, .padding = {0.0f, 16.0f, 0.0f, 22.0f}});
      parts.find.apply({.alignSelf = scene::align::kMiddle});
      parts.threads.apply({.alignSelf = scene::align::kMiddle});
      parts.threads.setVisible(shown.key.has_value());
      parts.call.apply({.alignSelf = scene::align::kMiddle});
      parts.call.setVisible(shown.key.has_value() && shown.callable);
      parts.info.apply({.alignSelf = scene::align::kMiddle});
      parts.back.apply({.alignSelf = scene::align::kMiddle});
      parts.back.setVisible(shown.back && shown.key.has_value());
      parts.face.setVisible(shown.key.has_value());
      parts.find.setVisible(shown.key.has_value());
      parts.info.setVisible(shown.key.has_value());
      parts.texts.parts.state.setVisible(shown.key.has_value());
    }
    [[nodiscard]] bool acceptsInput() const { return taps_to_info; }
    [[nodiscard]] bool onClick(float, float) {
      if (!taps_to_info)
        return false;
      actions->toggle_info();
      return true;
    }
  };
  struct parts_t {
    head_row row;
    nodes::Box<> divider;
  } parts;

  static constexpr float kHeight = 56.0f;

  // Declared: the row over a line dividing it from the messages.
  chat_header(const ui_needs<Actions>& n, const view& shown)
      : parts{.row = head_row(*n.colours, n.actions, shown), .divider = nodes::Box<>(n.colours->band)} {
    fState.apply({.fill = true, .background = n.colours->sidebar});
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
    nodes::Box<> stripe;
    two_lines texts;
    nodes::Box<> divider;
  } parts;
  static constexpr float kHeight = 46.0f;
  pinned_bar(const palette& colours, Press what, const pinned_view& shown)
      : press(std::move(what)),
        parts{.stripe = nodes::Box<>(colours.accent),
              .texts = two_lines(colours, shown.title, shown.line, 13.0f, 2.0f),
              .divider = nodes::Box<>(colours.band)} {
    this->setHorizontal();
    this->setGap(10.0f);
    fState.apply({.fill = true, .padding = {6.0f, 16.0f, 7.0f, 18.0f}, .background = colours.sidebar,
                  .hoverBackground = colours.chosen});
    fState.setCursor(scene::cursor::hand{});
    parts.stripe.apply({.fillY = true, .width = 2.0f, .cornerRadius = 1.0f});
    parts.texts.parts.name.setColour(colours.accent);
    parts.divider.apply({.place = scene::anchor::kBottomLeft, .fillX = true, .height = 1.0f});
  }
  [[nodiscard]] bool acceptsInput() const { return true; }
  [[nodiscard]] bool hoverChangesAppearance() const { return true; }
  [[nodiscard]] bool onClick(float, float) {
    press();
    return true;
  }
};

// Messages selected, as tdesktop's: in place of the head, how many, and
// what can be done with them -- Forward, Copy, Delete -- and Cancel. Esc
// cancels too.
template <class Actions>
struct selection_bar : nodes::Stack {
  Actions* actions;
  using forward_button = widgets::Button<ask<Actions, &Actions::selection_forward>>;
  using copy_button = widgets::Button<ask<Actions, &Actions::selection_copy>>;
  using delete_button = widgets::Button<ask<Actions, &Actions::selection_delete>>;
  using cancel_button = widgets::Button<ask<Actions, &Actions::selection_cancel>>;
  struct parts_t {
    nodes::Text count;
    forward_button forward;
    copy_button copy;
    delete_button remove;
    cancel_button cancel;
    nodes::Box<> bottom_line;
  } parts;
  explicit selection_bar(const ui_needs<Actions>& n)
      : actions(n.actions),
        parts{.count = nodes::Text("", 15.0f, n.colours->text, true),
              .forward = forward_button(n.colours->widgets, "Forward", {n.actions}),
              .copy = copy_button(n.colours->widgets, "Copy", {n.actions}),
              .remove = delete_button(n.colours->widgets, "Delete", {n.actions}),
              .cancel = cancel_button(n.colours->widgets, "Cancel", {n.actions}),
              .bottom_line = nodes::Box<>(n.colours->band)} {
    auto& [count, forward, copy, remove, cancel, bottom_line] = parts;
    this->setHorizontal();
    this->setGap(8.0f);
    fState.apply({.fillX = true, .height = chat_header<Actions>::kHeight, .padding = {0.0f, 16.0f, 1.0f, 22.0f},
                  .background = n.colours->sidebar});
    bottom_line.apply({.place = scene::anchor::kBottomLeft, .fillX = true, .height = 1.0f});
    count.apply({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
    for (scene::Node* each : std::initializer_list<scene::Node*>{&forward, &copy, &remove, &cancel})
      each->apply({.height = 32.0f, .alignSelf = scene::align::kMiddle});
    this->setVisible(false);
  }
  // How many are selected, and which of what can be done with them is.
  void show(std::size_t selected, bool forwardable, bool deletable) {
    parts.count.setText(std::format("{} selected", selected));
    parts.forward.setVisible(forwardable);
    parts.remove.setVisible(deletable);
    this->invalidateLayout();
  }
  using Node::onKey;
  void onKey(scene::phase::bubble, const scene::key::down& press, scene::Reply& reply) {
    if (press.key == scene::keys::kEscape) {
      actions->selection_cancel();
      reply.handle();
    }
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
    nodes::Text found;
    step_button newer, older;
    close_button close;
    nodes::Box<> bottom_line;
  } parts;

  explicit search_bar(const ui_needs<Actions>& n) : search_bar(*n.colours, n.actions) {}
  search_bar(const palette& colours, Actions* a)
      : actions(a), parts{.field = field_t(colours.widgets, "Search", {a}),
                          .found = nodes::Text("", 13.0f, colours.dim),
                          .newer = step_button(colours, icon::up{}, {a, false}),
                          .older = step_button(colours, icon::down{}, {a, true}),
                          .close = close_button(colours, icon::close{}, {a}),
                          .bottom_line = nodes::Box<>(colours.band)} {
    auto& [field, found, newer, older, close, bottom_line] = parts;
    this->setHorizontal();
    this->setGap(4.0f);
    fState.apply({.fillX = true, .height = chat_header<Actions>::kHeight, .padding = {0.0f, 16.0f, 1.0f, 22.0f},
                  .background = colours.sidebar});
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
