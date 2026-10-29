// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:header -- A chat's head, and finding in it.
module;
#include "skiff/named.h"
export module mux.ui:header;

import std;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.bind;
import skiff.nodes.box;
import skiff.nodes.flow;
import skiff.nodes.text;
import skiff.widgets.button;
import skiff.widgets.model;
import skiff.widgets.textbox;
import skiff.compose;
import mux.core;
import mux.config;
import mux.plugins.types;
import mux.ui.base;
import mux.plugins.ui.top;
import mux.ui.proto;

export namespace mux::ui {

// Connection status is shared by the list and the chat, for every protocol.
[[nodiscard]] inline std::string connection_notice(const connection_t& state) {
  return spl::visit(spl::overloaded{
      [](connection::offline) { return std::string("Offline"); },
      [](const connection::connecting& one) {
        return one.reason ? "Reconnecting: " + *one.reason : std::string("Connecting…");
      },
      [](connection::online) { return std::string(); },
      [](const connection::failed& one) { return "Connection failed: " + one.error; }}, state);
}
// What the connection notices say, as the program has them: of the
// account whose chats are listed, and of the chat's.
struct connection_shown {
  std::string list, chat;
  friend bool operator==(const connection_shown&, const connection_shown&) = default;
};
// A connection's notice -- Offline, Connecting..., Connection failed -- in a
// band over a list or a chat, where `said` makes one of the part Want; none,
// and the band is not there.
template <class Want, class Said>
auto connection_bar_for(const palette& colours, Said said) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(connection_bar_for, c::shown_for<Want>([said](const Want& now) { return !std::invoke(said, now).empty(); },
      c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {8.0f, 12.0f, 8.0f, 12.0f},
                               .background = (colours.error & 0x00FFFFFFu) | (0x22u << 24)}),
                c::text_for<Want>([said](const Want& now) { return std::string(std::invoke(said, now)); }, c::styled({.fillX = true}, wrapped(nodes::Text("", 13.0f, colours.text)))))));
}

// Something not there yet, said in a box over the window.
// A notice's box: as high as what it says, its heading and text wrapped,
// and selectable, to be copied.
// What a notice says: its heading and its text.
struct notice_facts {
  std::string heading;
  std::string text;
  bool delete_selection = false;
};
// A notice's text: wrapped, selectable, as wide as the box.
[[nodiscard]] inline auto notice_line(std::string text, float size, skia::SkColor colour, bool bold = false) {
  return skiff::compose::styled({.fillX = true}, wrapped(nodes::Text(std::move(text), size, colour, bold, true)));
}
inline auto notice_box(const palette& colours, const notice_facts& facts) {
  return SKIFF_NAMED(notice_box, skiff::compose::column(
      skiff::compose::vbox(10.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {20.0f, 22.0f, 20.0f, 22.0f}}),
      notice_line(facts.heading, 17.0f, colours.text, true), notice_line(facts.text, 14.0f, colours.dim),
      skiff::compose::row(skiff::compose::hbox(8.0f, {.autoSize = scene::axes::kBoth, .alignSelf = scene::align::kEnd}),
          skiff::compose::visible(facts.delete_selection,
              skiff::compose::styled({.width = 90.0f, .height = 34.0f},
                  widgets::SendButton<request::selection_delete_confirm>(colours.widgets, "Delete", {}))),
          skiff::compose::styled({.width = 90.0f, .height = 34.0f},
              primary(widgets::SendButton<request::close_notice>(colours.widgets, facts.delete_selection ? "Cancel" : "OK", {}))))));
}
using notice_box_t = decltype(notice_box(std::declval<const palette&>(), std::declval<const notice_facts&>()));
inline dialog_look content_look(std::type_identity<notice_facts>) { return {.size = dialog_size::fitting{440.0f}}; }
template <class Needs>
auto make_content(const Needs& needs, const notice_facts& facts) {
  return notice_box(*needs.colours, facts);
}

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
                                styled({.fillX = true, .alignSelf = scene::align::kMiddle}, wrapped(nodes::Text(std::string(name), 11.0f, colours.dim))));
}
using emoji_cell_t = decltype(emoji_cell(std::declval<const palette&>(), {}, {}));
// The seven to compare wrap in their original order on narrow screens.
[[nodiscard]] inline nodes::Flow<std::vector<emoji_cell_t>> emoji_row(const palette& colours, const verification_step_t& step) {
  auto cells = spl::visit(spl::overloaded{
      [&](const verification_step::compare& shown) {
        return shown.emoji | std::views::transform([&](int index) {
          const auto& [picture, name] = sas_emoji[static_cast<std::size_t>(index & 63)];
          return emoji_cell(colours, picture, name);
        }) | std::ranges::to<std::vector>();
      }, [](const auto&) { return std::vector<emoji_cell_t>{}; }}, step);
  const bool comparing = !cells.empty();
  return skiff::compose::visible(comparing, skiff::compose::styled(
      {.fillX = true, .autoSize = scene::axes::kY},
      nodes::Flow<std::vector<emoji_cell_t>>({.direction = nodes::direction::horizontal{}, .spacingX = 8.0f, .spacingY = 12.0f, .wrap = true}, std::move(cells))));
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
// What the verification says, under who it is with: as its step goes.
[[nodiscard]] inline std::string verification_note(const verification_view& view) {
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
// A button of it: at the end, as wide as said, there where its step has it.
template <class Button>
[[nodiscard]] auto verification_answer(bool shown, float width, Button button) {
  return skiff::compose::visible(shown, skiff::compose::styled({.width = width, .height = 34.0f, .alignSelf = scene::align::kEnd}, std::move(button)));
}
// The box, made from the verification as it is now: made again as its
// step moves on.
inline auto verification_box_of(const palette& colours, const verification_view& view) {
  return SKIFF_NAMED(verification_box_of, skiff::compose::column(
      skiff::compose::vbox(10.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {20.0f, 22.0f, 20.0f, 22.0f}}),
      notice_line("Verify " + view.user, 17.0f, colours.text, true), notice_line(verification_note(view), 14.0f, colours.dim),
      emoji_row(colours, view.step),
      verification_answer(step_asks(view.step), 120.0f, primary(button_for<sends<request::verify_accept_now>>(colours.widgets, "Accept", {}))),
      verification_answer(step_waits(view.step), 120.0f,
          button_for<sends<request::verify_cancel_now>>(colours.widgets, step_asks(view.step) ? "Decline" : "Cancel", {})),
      verification_answer(step_compares(view.step), 160.0f, primary(button_for<sends<request::verify_match>>(colours.widgets, "They match", {}))),
      verification_answer(step_compares(view.step), 160.0f, button_for<sends<request::verify_mismatch>>(colours.widgets, "They don't match", {})),
      verification_answer(step_over(view.step), 90.0f, primary(button_for<sends<request::close_verification>>(colours.widgets, "OK", {})))));
}
using verification_box = decltype(verification_box_of(std::declval<const palette&>(), std::declval<const verification_view&>()));
// The window's dialog of it: as wide as it fits, up to 440; not put away
// by a press off it -- the verification says when it is over.
inline dialog_look content_look(std::type_identity<verification_view>) { return {.size = dialog_size::fitting{440.0f}, .dismissable = false}; }
template <class Needs>
auto make_content(const Needs& needs, const verification_view& view) {
  return verification_box_of(*needs.colours, view);
}

// What a chat says of itself, over its messages: its avatar, name and who
// is in it or how they are, a line under it, and the button that opens its
// info beside it.
// The buttons plugins put in a chat's head (contract::chat_header), after
// Search, in their order: each shown or hidden as its plugin's part keeps
// in the model (a call's: where the chat shown can be called).
//
// Search, in their order: each shown for a chat as its plugin says.
template <class List>
struct header_buttons;
template <class... Features>
struct header_buttons<plugin_list<Features...>> {
  template <class Feature>
  static auto button_of(const palette& colours) -> decltype(extend(contract::chat_header{}, type_tag<Feature>{}, colours)) {
    static_assert(requires { extend(contract::chat_header{}, type_tag<Feature>{}, colours); },
                  composition::broken<contract::chat_header, Feature>());
    return extend(contract::chat_header{}, type_tag<Feature>{}, colours);
  }
  using nodes_t = std::tuple<decltype(button_of<Features>(std::declval<const palette&>()))...>;
  // In a row of their own, in the plugins' order.
  static auto row_of(const palette& colours) {
    return skiff::compose::row(skiff::compose::hbox(12.0f, {.autoSize = scene::axes::kBoth, .alignSelf = scene::align::kMiddle}),
                               button_of<Features>(colours)...);
  }
  static nodes_t made(const palette& colours, bool chat) {
    return [&]<std::size_t... At>(std::index_sequence<At...>) {
      return nodes_t{skiff::compose::visible(chat, button_of<Features>(colours))...};
    }(std::index_sequence_for<Features...>{});
  }
};
using chat_header_buttons = header_buttons<composition::providers<contract::chat_header, enabled_plugins>>;
// What plugins say of a chat in its head (contract::chat_status): the
// first that says something, in their order.
template <class List>
struct status_list;
template <class... Features>
struct status_list<plugin_list<Features...>> {
  template <class Feature>
  static std::optional<std::string> of(const conversation& one) {
    static_assert(requires { extend(contract::chat_status{}, type_tag<Feature>{}, one); },
                  composition::broken<contract::chat_status, Feature>());
    return extend(contract::chat_status{}, type_tag<Feature>{}, one);
  }
  static std::optional<std::string> said(const conversation& one) {
    std::optional<std::string> found;
    ((found = found ? found : of<Features>(one)), ...);
    return found;
  }
};
using chat_status_plugins = status_list<composition::providers<contract::chat_status, enabled_plugins>>;

// What the head shows: the chat's key and name, and how it is -- or no chat
// -- and whether a back arrow goes before it, to the chats (shown one thing
// at a time). The program's, in the shown model.
struct chat_head_shown {
  std::optional<std::string> key;
  std::string title = "Choose a chat";
  std::string status;
  bool back = false;
  // In place of it: the chat's search, or the bar of messages selected.
  bool hidden = false;
  friend bool operator==(const chat_head_shown&, const chat_head_shown&) = default;
};
// Whether there is any account at all, and a chat chosen: the chat's column
// says what to do about it, or to choose one.
struct chats_shown {
  bool none = false;
  bool chosen = false;
  friend bool operator==(const chats_shown&, const chats_shown&) = default;
};
[[nodiscard]] inline chat_head_shown chat_head_of(const ui_shared& shared, const conversation* one, const model& now) {
  if (one == nullptr)
    return {};
  const auto count = std::max<std::int64_t>(static_cast<std::int64_t>(one->members.size()), one->member_count);
  // As its protocol says it, where it does (a channel's subscribers); else
  // its members, or how the other is.
  std::string about = proto::chat_status(protocol_state_of(shared, one->id.account), *one, now)
                          .value_or(is_group(*one) ? std::format("{} member{}", count, count == 1 ? "" : "s")
                                                   : presence_of(shared, now, one->id.account, contact_of(shared, *one)));
  // What plugins say of it now, over that (contract::chat_status): who is
  // typing, for one.
  if (auto said = chat_status_plugins::said(*one))
    about = std::move(*said);
  // What its protocol says of it after (proto::header_badges): Matrix's
  // shield, for one.
  about = std::ranges::fold_left(proto::header_badges(protocol_state_of(shared, one->id.account), *one, now), std::move(about),
                                 [](std::string so_far, const proto::part::badge& badge) {
                                   return so_far.empty() ? badge.text : std::format("{} \u00b7 {}", so_far, badge.text);
                                 });
  return {.key = one->id.id, .title = display_name(*one), .status = std::move(about)};
}
// The head of a chat, over its messages: the back arrow where it is shown
// one thing at a time, the chat's avatar, its name over how it is, and Find,
// the plugins' buttons (a call), its threads and its info. A press on it
// elsewhere asks for its info -- the program says whether that opens it (on
// a phone, as Telegram's). All as the program says (chat_head_shown).
inline auto chat_header_for(const palette& colours) {
  namespace c = skiff::compose;
  const auto chat = [](const chat_head_shown& now) { return now.key.has_value(); };
  const scene::Spec middle{.alignSelf = scene::align::kMiddle};
  return SKIFF_NAMED(chat_header_for, c::shown_for<chat_head_shown>([](const chat_head_shown& now) { return now.key && !now.hidden; },
    c::column(c::vbox(0.0f, {.fillX = true, .height = kChatHeadHeight, .background = colours.sidebar}),
      c::onClick(request::head_pressed{},
          c::row(c::hbox(12.0f, {.fillX = true, .grow = scene::axes::kY, .padding = {0.0f, 16.0f, 0.0f, 22.0f}}),
              c::shown_for<chat_head_shown>([](const chat_head_shown& now) { return now.back && now.key; },
                  c::styled(middle, icon_button(colours, icon::back{}, sends<request::close_chat>{}))),
              c::shown_for<chat_head_shown>(chat,
                  avatar_for<chat_head_shown>(avatar_of::chat{}, 38.0f, [](const chat_head_shown& now) {
                    return avatar_who{now.key.value_or(""), now.title};
                  })),
              c::column(c::vbox(3.0f, {.autoSize = scene::axes::kY, .grow = scene::axes::kX, .alignSelf = scene::align::kMiddle}),
                  c::text_for<chat_head_shown>([](const chat_head_shown& now) { return now.title; },
                      c::styled({.fillX = true}, elided(nodes::Text("", 15.0f, colours.text, true)))),
                  c::shown_for<chat_head_shown>(chat,
                      c::text_for<chat_head_shown>([](const chat_head_shown& now) { return now.status; },
                          c::styled({.fillX = true}, elided(nodes::Text("", 13.0f, colours.dim)))))),
              c::shown_for<chat_head_shown>(chat, chat_header_buttons::row_of(colours)),
              c::shown_for<chat_head_shown>(chat, c::styled(middle, icon_button(colours, icon::info{}, sends<request::toggle_info>{}))))),
      c::styled({.fillX = true, .height = 1.0f}, nodes::Box<>(colours.band)))));
}
using chat_header_t = decltype(chat_header_for(std::declval<const palette&>()));

// What the chat's protocol says over its composer -- Matrix's warning that
// the other is not verified, or was reset -- its button's label, and its
// tone; nothing, and no banner. The program's, dismissed as it says.
struct banner_shown {
  std::string text;
  std::string button;
  proto::part::tone_t tone = proto::part::tone::plain{};
  friend bool operator==(const banner_shown&, const banner_shown&) = default;
};
inline auto composer_banner_for(const palette& colours) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(composer_banner_for, c::shown_for<banner_shown>([](const banner_shown& now) { return !now.text.empty(); },
      c::spec_for<banner_shown>([&colours](const banner_shown& now) {
        return scene::Spec{.background = (tone_colour(colours, now.tone) & 0x00FFFFFFu) | (0x22u << 24)};
      },
          c::row(c::hbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {10.0f, 12.0f, 10.0f, 12.0f}}),
                 c::text_for<banner_shown>([](const banner_shown& now) { return now.text; },
                     c::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle}, wrapped(nodes::Text("", 13.0f, colours.text)))),
                 // The program's button, its label in its middle -- the hand
                 // made box had it at its left -- as the banner says it.
                 c::shown_for<banner_shown>([](const banner_shown& now) { return !now.button.empty(); },
                     c::applied<banner_shown>([](const banner_shown& now) { return now.button; },
                         [](auto& button, const std::string& label) { button.setLabel(label); },
                         c::styled({.width = 112.0f, .height = 36.0f, .alignSelf = scene::align::kMiddle},
                                   c::primary(button_for<sends<request::banner_button>>(colours.widgets, "", {}))))),
                 c::styled({.alignSelf = scene::align::kMiddle}, icon_button(colours, icon::close{}, sends<request::banner_close>{}))))));
}
using composer_banner_t = decltype(composer_banner_for(std::declval<const palette&>()));

// Messages selected, as what is shown says: how many, and what can be
// done with them.
struct selection_shown {
  std::size_t count = 0;
  // The plugins' actions offered (contract::selection_actions): a bit each,
  // in their order.
  std::uint64_t offered = 0;
  bool deletable = false;
  friend bool operator==(const selection_shown&, const selection_shown&) = default;
};
// The plugins' actions on the selection, each its button where it is offered.
using selection_plugins = composition::providers<contract::selection_actions, enabled_plugins>;
template <class... Features, std::size_t... I, class Make>
auto selection_actions_of(plugin_list<Features...>, std::index_sequence<I...>, Make make) {
  namespace c = skiff::compose;
  return c::row(c::hbox(8.0f, {.autoSize = scene::axes::kBoth, .alignSelf = scene::align::kMiddle}),
      c::shown_if<selection_shown>([](const selection_shown& now) { return ((now.offered >> I) & 1u) != 0; },
          make(::mux::ui::request::selection_asked<Features>{}, std::string(label(contract::selection_actions{}, type_tag<Features>{}))))...);
}
template <class... Features>
auto selection_actions_of(plugin_list<Features...> list, auto make) {
  return selection_actions_of(list, std::index_sequence_for<Features...>{}, std::move(make));
}
// As tdesktop's top bar: the plugins' actions (Forward) and Delete carry the selection count,
// Clear stays at the right. Copy remains in the context menu and Ctrl+C.
// Buttons size from their text instead of relying on Button's zero width.
auto selection_bar(const ui_needs& n) {
  namespace c = skiff::compose;
  const palette& colours = *n.colours;
  const scene::Spec action{
      .height = 32.0f, .autoSize = scene::axes::kX, .alignSelf = scene::align::kMiddle,
      .padding = {0.0f, 10.0f, 0.0f, 10.0f}, .cornerRadius = 6.0f,
      .background = colours.accent, .hoverBackground = skiff::paint::lighten(colours.accent, 0.12f),
      .focusBackground = skiff::paint::lighten(colours.accent, 0.12f)};
  const auto selected_action = [&](auto event, std::string label) {
    const std::string accessible = label + " selected messages";
    return c::onClick(event,
        c::row(c::hbox(0.0f, action),
            c::text_of<selection_shown>(
                [label = std::move(label)](const selection_shown& now) {
                  return std::format("{} {}", label, now.count);
                },
                c::styled({.alignSelf = scene::align::kMiddle},
                          nodes::Text("", 13.0f, colours.on_accent, true)))),
        accessible);
  };
  return SKIFF_NAMED(selection_bar, c::shown_if<selection_shown>(
      [](const selection_shown& now) { return now.count > 0; },
      c::onKey(scene::keys::kEscape, ::mux::ui::request::selection_cancel{},
          c::row(c::hbox(8.0f, {.fillX = true, .height = kChatHeadHeight,
                               .padding = {0.0f, 12.0f, 1.0f, 12.0f}, .background = colours.sidebar}),
              selection_actions_of(selection_plugins{}, selected_action),
              c::spec_for<selection_shown>([](const selection_shown& now) { return scene::Spec{.disabled = !now.deletable}; },
                  selected_action(::mux::ui::request::selection_delete{}, "Delete")),
              c::styled({.grow = scene::axes::kX}, nodes::Box<>(skia::SkColor{0})),
              c::onClick(::mux::ui::request::selection_cancel{},
                  c::row(c::hbox(0.0f, {.height = 32.0f, .autoSize = scene::axes::kX,
                                       .alignSelf = scene::align::kMiddle, .padding = {0.0f, 10.0f, 0.0f, 10.0f},
                                       .cornerRadius = 6.0f, .hoverBackground = colours.chosen,
                                       .focusBackground = colours.chosen}),
                      c::styled({.alignSelf = scene::align::kMiddle}, nodes::Text("Clear", 13.0f, colours.accent, true))),
                  "Clear message selection"),
              c::styled({.place = scene::anchor::kBottomLeft, .fillX = true, .height = 1.0f}, nodes::Box<>(colours.band))))));
}
using selection_bar_t = decltype(selection_bar(std::declval<const ui_needs&>()));


}  // namespace mux::ui

