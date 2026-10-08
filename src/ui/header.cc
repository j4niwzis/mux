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
import skiff.compose;
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

// What a notice says: its heading and its text.
struct notice_facts {
  std::string heading;
  std::string text;
};
// A notice's text: wrapped, selectable, as wide as the box.
[[nodiscard]] inline nodes::Text notice_line(std::string text, float size, skia::SkColor colour, bool bold = false) {
  nodes::Text line(std::move(text), size, colour, bold);
  line.setWrapped(true);
  line.setSelectable(true);
  line.apply({.fillX = true});
  return line;
}

// A notice: its heading and its text over an OK at the end -- as high as
// what it says, no room left empty under its button.
template <class Actions>
using notice_box_of = skiff::compose::Box<nodes::Text, nodes::Text, button_for<sends<::mux::ui::request::close_notice>>>;
template <class Actions>
struct notice_box : notice_box_of<Actions> {
  // The dialog it is shown in.
  [[nodiscard]] static dialog_look look_of_dialog() { return {.size = dialog_size::fitting{440.0f}}; }
  using ok_button = button_for<sends<::mux::ui::request::close_notice>>;

  notice_box(const ui_needs<Actions>& n, const notice_facts& facts) : notice_box(*n.colours, facts.heading, facts.text) {}
  notice_box(const ui_needs<Actions>& n, std::string heading, std::string text)
      : notice_box(*n.colours, std::move(heading), std::move(text)) {}
  notice_box(const palette& colours, std::string heading, std::string text)
      : notice_box_of<Actions>(
            skiff::compose::vbox(10.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {20.0f, 22.0f, 20.0f, 22.0f}}),
            notice_line(std::move(heading), 17.0f, colours.text, true), notice_line(std::move(text), 14.0f, colours.dim),
            skiff::compose::styled({.width = 90.0f, .height = 34.0f, .alignSelf = scene::align::kEnd},
                                   primary(ok_button(colours.widgets, "OK", {})))) {}
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
// One emoji, big, its name under it.
[[nodiscard]] inline auto emoji_cell(const palette& colours, std::string_view picture, std::string_view name) {
  using skiff::compose::styled;
  return skiff::compose::column(skiff::compose::vbox(4.0f, {.width = 52.0f, .autoSize = scene::axes::kY}),
                                styled({.alignSelf = scene::align::kMiddle}, nodes::Text(std::string(picture), 30.0f, colours.text)),
                                styled({.alignSelf = scene::align::kMiddle}, nodes::Text(std::string(name), 11.0f, colours.dim)));
}
using emoji_cell_t = decltype(emoji_cell(std::declval<const palette&>(), {}, {}));
// The seven to compare, in a row: none where nothing is compared.
[[nodiscard]] inline skiff::compose::Many<emoji_cell_t> emoji_row(const palette& colours, const verification_step_t& step) {
  const std::array<int, 7> none{};
  const std::array<int, 7>& indices = spl::visit(spl::overloaded{[](const verification_step::compare& shown) -> const std::array<int, 7>& { return shown.emoji; },
                                                                 [&](const auto&) -> const std::array<int, 7>& { return none; }},
                                                 step);
  const bool comparing = &indices != &none;
  return skiff::compose::visible(
      comparing, skiff::compose::many(skiff::compose::hbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                                      comparing ? indices | std::views::transform([&](int index) {
                                        const auto& [picture, name] = sas_emoji[static_cast<std::size_t>(index & 63)];
                                        return emoji_cell(colours, picture, name);
                                      }) | std::ranges::to<std::vector>()
                                                : std::vector<emoji_cell_t>{}));
}

// Which of a verification's buttons its step shows.
[[nodiscard]] inline bool in_step(const verification_step_t& step, auto shows) { return spl::visit(shows, step); }
[[nodiscard]] inline bool step_asks(const verification_step_t& step) {
  return in_step(step, spl::overloaded{[](verification_step::asked) { return true; }, [](const auto&) { return false; }});
}
[[nodiscard]] inline bool step_waits(const verification_step_t& step) {
  return in_step(step, spl::overloaded{[](verification_step::asked) { return true; }, [](verification_step::waiting) { return true; },
                                       [](const auto&) { return false; }});
}
[[nodiscard]] inline bool step_compares(const verification_step_t& step) {
  return in_step(step, spl::overloaded{[](const verification_step::compare&) { return true; }, [](const auto&) { return false; }});
}
[[nodiscard]] inline bool step_over(const verification_step_t& step) {
  return in_step(step, spl::overloaded{[](verification_step::done) { return true; }, [](const verification_step::cancelled&) { return true; },
                                       [](const auto&) { return false; }});
}

// An emoji verification, as Element shows it: with whom, and where it is --
// asked of you (Accept, Decline), waiting on them (Cancel), the 7 emoji to
// compare (They match, They don't match), over (OK). What shows is what
// its step says.
template <class Actions>
using verification_box_of =
    skiff::compose::Box<nodes::Text, nodes::Text, skiff::compose::Many<emoji_cell_t>, button_for<sends<request::verify_accept_now>>,
                        button_for<sends<request::verify_cancel_now>>, button_for<sends<request::verify_match>>,
                        button_for<sends<request::verify_mismatch>>, button_for<sends<request::close_verification>>>;
template <class Actions>
struct verification_box : verification_box_of<Actions> {
  // The dialog it is shown in.
  [[nodiscard]] static dialog_look look_of_dialog() { return {.size = dialog_size::fitting{440.0f}, .dismissable = false}; }
  using accept_button = button_for<sends<request::verify_accept_now>>;
  using decline_button = button_for<sends<request::verify_cancel_now>>;
  using match_button = button_for<sends<request::verify_match>>;
  using mismatch_button = button_for<sends<request::verify_mismatch>>;
  using close_button = button_for<sends<request::close_verification>>;
  verification_step_t step;

  verification_box(const ui_needs<Actions>& n, const verification_view& view) : verification_box(*n.colours, view) {}
  verification_box(const palette& colours, const verification_view& view)
      : verification_box_of<Actions>(
            skiff::compose::vbox(10.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {20.0f, 22.0f, 20.0f, 22.0f}}),
            notice_line("Verify " + view.user, 17.0f, colours.text, true), notice_line(note_of(view), 14.0f, colours.dim),
            emoji_row(colours, view.step),
            answer(step_asks(view.step), 120.0f, primary(accept_button(colours.widgets, "Accept", {}))),
            answer(step_waits(view.step), 120.0f, decline_button(colours.widgets, step_asks(view.step) ? "Decline" : "Cancel", {})),
            answer(step_compares(view.step), 160.0f, primary(match_button(colours.widgets, "They match", {}))),
            answer(step_compares(view.step), 160.0f, mismatch_button(colours.widgets, "They don't match", {})),
            answer(step_over(view.step), 90.0f, primary(close_button(colours.widgets, "OK", {})))),
        step(view.step) {}
  // A button of it: at the end, as wide as said, there where its step has it.
  template <class Button>
  [[nodiscard]] static Button answer(bool shown, float width, Button button) {
    return skiff::compose::visible(shown, skiff::compose::styled({.width = width, .height = 34.0f, .alignSelf = scene::align::kEnd}, std::move(button)));
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
    // What its handlers ask for, returned.
    using Answer = ::mux::ui::request::toggle_info;
    using find_button = icon_button<sends<::mux::ui::request::open_search>>;
    using info_button = icon_button<sends<::mux::ui::request::toggle_info>>;
    using threads_button = icon_button<sends<::mux::ui::request::toggle_threads>>;
    using call_button = icon_button<sends<::mux::ui::request::call_chosen>>;
    using back_button = icon_button<sends<::mux::ui::request::close_chat>>;
    // Shown one thing at a time, a tap on the chat's name or avatar opens
    // its info, as on Telegram's phones.
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
    head_row(const palette& colours, const view& shown)
        : taps_to_info(shown.back && shown.key.has_value()),
          parts{.back = back_button(colours, icon::back{}, {}),
                .face = avatar_mark(shown.key.value_or(""), shown.title, 38.0f),
                .texts = two_lines(colours, shown.title, shown.status, 15.0f, 3.0f),
                .find = find_button(colours, icon::search{}, {}),
                .call = call_button(colours, icon::phone{}, {}),
                .threads = threads_button(colours, icon::threads{}, {}),
                .info = info_button(colours, icon::info{}, {})} {
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
    std::optional<Answer> onClick(float, float) {
      if (!taps_to_info)
        return std::nullopt;
      return ::mux::ui::request::toggle_info{};
    }
  };
  struct parts_t {
    head_row row;
    nodes::Box<> divider;
  } parts;

  static constexpr float kHeight = 56.0f;

  // Declared: the row over a line dividing it from the messages.
  chat_header(const ui_needs<Actions>& n, const view& shown)
      : parts{.row = head_row(*n.colours, shown), .divider = nodes::Box<>(n.colours->band)} {
    fState.apply({.fill = true, .background = n.colours->sidebar});
    parts.divider.apply({.fillX = true, .height = 1.0f});
  }
};

// The head as the chats model has it: the chosen chat's, read by the
// window's binding as the chats move, and made again only where what it
// shows did.
template <class Actions>
struct chat_head : nodes::Memo<typename chat_header<Actions>::view, chat_header<Actions>> {
  ui_needs<Actions> needs;
  const model* now = nullptr;
  std::optional<conversation_id> chosen;
  bool back = false;
  // The chat chosen, or the window gone single or not: shown as it is now.
  void choose(const model& with, std::optional<conversation_id> one, bool single) {
    now = &with;
    chosen = std::move(one);
    back = single;
    this->redo();
  }
  void go_back(bool single) {
    back = single;
    this->redo();
  }
  void refresh(const chats_model&) { this->redo(); }

 private:
  void redo() {
    if (now == nullptr)
      return;
    const conversation* one = chosen ? now->find(*chosen) : nullptr;
    auto shown = chat_header<Actions>::view_of(*needs.shared, one, *now);
    shown.back = back;
    this->show(shown, [this](const auto& made) { return chat_header<Actions>(needs, made); });
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
struct pinned_bar : skiff::compose::Stacked {
  Press press;
  struct parts_t {
    nodes::Box<> stripe;
    two_lines texts;
    nodes::Box<> divider;
  } parts;
  static constexpr float kHeight = 46.0f;
  pinned_bar(const palette& colours, Press what, const pinned_view& shown)
      : Stacked(skiff::compose::hbox(10.0f, {.fill = true, .padding = {6.0f, 16.0f, 7.0f, 18.0f}, .background = colours.sidebar,
                                             .hoverBackground = colours.chosen})),
        press(std::move(what)),
        parts{.stripe = skiff::compose::styled({.fillY = true, .width = 2.0f, .cornerRadius = 1.0f}, nodes::Box<>(colours.accent)),
              .texts = accent_named(colours, two_lines(colours, shown.title, shown.line, 13.0f, 2.0f)),
              .divider = skiff::compose::styled({.place = scene::anchor::kBottomLeft, .fillX = true, .height = 1.0f}, nodes::Box<>(colours.band))} {
    fState.setCursor(scene::cursor::hand{});
  }
  // Its title in the accent, as tdesktop's pinned bar.
  [[nodiscard]] static two_lines accent_named(const palette& colours, two_lines texts) {
    texts.parts.name.setColour(colours.accent);
    return texts;
  }
  [[nodiscard]] bool acceptsInput() const { return true; }
  [[nodiscard]] bool hoverChangesAppearance() const { return true; }
  [[nodiscard]] bool onClick(float, float) {
    act_on(fState, press);
    return true;
  }
  auto onPress()
    requires skiff::scene::Answering<Press>
  {
    return press();
  }
};

// Messages selected, as what is shown says: how many, and what can be
// done with them.
struct selection_shown {
  std::size_t count = 0;
  bool forwardable = false;
  bool deletable = false;
};
// Messages selected, as tdesktop's: in place of the head, how many, and
// what can be done with them -- Forward, Copy, Delete -- and Cancel. Esc
// cancels too. All of it read from what is shown: nothing set by hand.
template <class Actions>
auto selection_bar(const ui_needs<Actions>& n) {
  namespace c = skiff::compose;
  const palette& colours = *n.colours;
  const scene::Spec button{.height = 32.0f, .alignSelf = scene::align::kMiddle};
  return c::shown_if<selection_shown>(
      [](const selection_shown& now) { return now.count > 0; },
      c::onKey(scene::keys::kEscape, ::mux::ui::request::selection_cancel{},
               c::row(c::hbox(8.0f, {.fillX = true, .height = chat_header<Actions>::kHeight, .padding = {0.0f, 16.0f, 1.0f, 22.0f},
                                     .background = colours.sidebar}),
                      c::text_of<selection_shown>([](const selection_shown& now) { return std::format("{} selected", now.count); },
                                                  c::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle},
                                                            nodes::Text("", 15.0f, colours.text, true))),
                      c::shown_if<selection_shown>([](const selection_shown& now) { return now.forwardable; },
                                                   c::styled(button, button_for<sends<::mux::ui::request::selection_forward>>(colours.widgets, "Forward", {}))),
                      c::styled(button, button_for<sends<::mux::ui::request::selection_copy>>(colours.widgets, "Copy", {})),
                      c::shown_if<selection_shown>([](const selection_shown& now) { return now.deletable; },
                                                   c::styled(button, button_for<sends<::mux::ui::request::selection_delete>>(colours.widgets, "Delete", {}))),
                      c::styled(button, button_for<sends<::mux::ui::request::selection_cancel>>(colours.widgets, "Cancel", {})),
                      c::styled({.place = scene::anchor::kBottomLeft, .fillX = true, .height = 1.0f}, nodes::Box<>(colours.band)))));
}
template <class Actions>
using selection_bar_t = decltype(selection_bar(std::declval<const ui_needs<Actions>&>()));

// Finding in a chat, as tdesktop's search in a chat: in place of the head,
// a field with the magnifier, how many are found and which is shown ("3 of
// 12"), the arrows to the newer and the older, and ✕. Enter goes to the
// older one, Shift+Enter to the newer, Esc closes.
template <class Actions>
struct search_typed {
  using Answer = std::optional<::mux::ui::request::search_typed>;
  std::optional<::mux::ui::request::search_typed> operator()(std::string_view text) { return ::mux::ui::request::search_typed{std::string(text)}; }
};
template <class Actions>
struct search_step {
  using Answer = ::mux::ui::request::search_step;
  bool older = true;
  ::mux::ui::request::search_step operator()() { return ::mux::ui::request::search_step{older}; }
};
template <class Actions>
struct search_bar : skiff::compose::Stacked {
  // What its handlers ask for, returned.
  using Answer = std::variant<::mux::ui::request::search_step, ::mux::ui::request::close_search>;
  using field_t = widgets::TextBox<search_typed<Actions>>;
  using step_button = icon_button<search_step<Actions>>;
  using close_button = icon_button<sends<::mux::ui::request::close_search>>;
  struct parts_t {
    field_t field;
    nodes::Text found;
    step_button newer, older;
    close_button close;
    nodes::Box<> bottom_line;
  } parts;

  explicit search_bar(const ui_needs<Actions>& n) : search_bar(*n.colours) {}
  [[nodiscard]] static scene::Spec middle() { return {.alignSelf = scene::align::kMiddle}; }
  // The field, with the magnifier in it.
  [[nodiscard]] static field_t searching(field_t field) {
    field.setSearchIcon(true);
    return field;
  }
  search_bar(const palette& colours)
      : Stacked(skiff::compose::hbox(4.0f, {.fillX = true, .height = chat_header<Actions>::kHeight, .padding = {0.0f, 16.0f, 1.0f, 22.0f},
                                            .background = colours.sidebar})),
        parts{.field = skiff::compose::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle},
                                              searching(field_t(colours.widgets, "Search", {}))),
              .found = skiff::compose::styled(middle(), nodes::Text("", 13.0f, colours.dim)),
              .newer = skiff::compose::styled(middle(), step_button(colours, icon::up{}, {false})),
              .older = skiff::compose::styled(middle(), step_button(colours, icon::down{}, {true})),
              .close = skiff::compose::styled(middle(), close_button(colours, icon::close{}, {})),
              .bottom_line = skiff::compose::styled({.place = scene::anchor::kBottomLeft, .fillX = true, .height = 1.0f},
                                                    nodes::Box<>(colours.band))} {
    this->setVisible(false);
  }
  // Where the finding is: the one shown of how many, or none found.
  void show_found(std::optional<std::size_t> at, std::size_t of, bool asked) {
    parts.found.setText(!asked ? std::string() : of == 0 ? std::string("No results") : std::format("{} of {}", at.value_or(0) + 1, of));
    this->invalidateLayout();
  }
  using Node::onKey;
  std::optional<Answer> onKey(scene::phase::bubble, const scene::key::down& press, scene::Reply& reply) {
    std::optional<Answer> answer;
    if (press.key == scene::keys::kEnter) {
      answer = ::mux::ui::request::search_step{!press.modifiers.template has<scene::modifier::shift>()};
      reply.handle();
    } else if (press.key == scene::keys::kEscape) {
      answer = ::mux::ui::request::close_search{};
      reply.handle();
    } else if (press.key == scene::keys::kUp) {
      answer = ::mux::ui::request::search_step{false};
      reply.handle();
    } else if (press.key == scene::keys::kDown) {
      answer = ::mux::ui::request::search_step{true};
      reply.handle();
    }
    return answer;
  }
};

}  // namespace mux::ui
