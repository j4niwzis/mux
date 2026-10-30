// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.looks.ui_top -- Bubbles' and panels' looks picked, and a chat's background.
module;
#include "../../inline_in_release.h"
#include "skiff/named.h"
export module mux.plugin.looks.ui_top;

import std;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.compose;
import skiff.nodes.box;
import skiff.nodes.flow;
import skiff.nodes.icon;
import skiff.nodes.image;
import skiff.nodes.scroll;
import skiff.nodes.text;
import skiff.widgets.pill;
import skiff.widgets.button;
import skiff.widgets.sliderbar;
import skiff.widgets.textbox;
import skiff.widgets.textarea;
import mux.core;
import mux.config;
import mux.logic.links;
import mux.protocols;
import mux.ui.base;
import mux.ui.messages;
import mux.ui.cards;

import mux.composition;
import mux.plugin.looks.types;

export namespace mux::plugins::looks {
using namespace ::mux::ui;
// What a chat looks like, chosen at a level -- every chat's, an account's,
// one chat's: its background (as the level over it says, the theme's, a
// plain colour, or a picture of one's own), and how its bubbles and the
// panels round it look (as the level over it, or a kind, how opaque, how
// much Frosted blurs; the bubbles' apart for what else is in a chat). All
// of it read from what the program last said (looks_picked); each choice
// a request, the look worked out by the program from what it holds.

// The look at a level, in effect: every chat's, or the chat's shown.
[[nodiscard]] inline config::bubble_look look_in(const looks_picked& now, const choice_level_t& level, const config::look_part_t& part) {
  const bool everywhere = !has_level_above(level);
  return spl::visit(spl::overloaded{[&](config::look_part::bubbles) { return everywhere ? now.bubbles_everywhere : now.bubbles; },
                                    [&](config::look_part::panels) { return everywhere ? now.panels_everywhere : now.panels; }},
                    part);
}
// What a level holds of a look of its own: none, as the level over it.
[[nodiscard]] inline const std::optional<config::bubble_look>& held_look(const looks_held& held, const config::look_part_t& part) {
  return spl::visit(spl::overloaded{[&](config::look_part::bubbles) -> const std::optional<config::bubble_look>& { return held.bubbles; },
                                    [&](config::look_part::panels) -> const std::optional<config::bubble_look>& { return held.panels; }},
                    part);
}
// The panels' look does something only over a background behind the whole
// window: else it is shown greyed, and nothing done.
[[nodiscard]] inline bool look_usable(const looks_picked& now, const config::look_part_t& part) {
  return spl::visit(spl::overloaded{[](config::look_part::bubbles) { return true; },
                                    [&](config::look_part::panels) { return now.window.behind; }},
                    part);
}
// Whether the level holds a look of its own: as above, what is under the
// choice of kind is the level over it's -- shown greyed, and left alone.
[[nodiscard]] inline bool look_own(const looks_picked& now, const choice_level_t& level, const config::look_part_t& part) {
  return !has_level_above(level) || held_look(now.at(level), part).has_value();
}
[[nodiscard]] inline bool is_bubbles(const config::look_part_t& part) {
  return spl::visit(spl::overloaded{[](config::look_part::bubbles) { return true; }, [](config::look_part::panels) { return false; }}, part);
}

// ---- a choice, worked out ------------------------------------------------------
// The look a choice asks for, from the looks as they are: none where it
// does nothing -- the panels with nothing behind them, a slider under a
// level that is as above.
[[nodiscard]] inline std::optional<config::bubble_look> look_after(const looks_picked& now, const request::change_bubbles& asked,
                                                                   const request::bubbles_change::opacity& slid) {
  if (!look_usable(now, asked.part) || !look_own(now, asked.level, asked.part))
    return std::nullopt;
  config::bubble_look look = look_in(now, asked.level, asked.part);
  // Solid has no opacity: translucent, then.
  look.kind = spl::visit(spl::overloaded{[](config::bubbles::solid) { return config::bubbles_t{config::bubbles::translucent{}}; },
                                         [](const auto& other) { return config::bubbles_t{other}; }},
                         look.kind);
  look.opacity = static_cast<int>(std::lround(10.0f + std::clamp(slid.at, 0.0f, 1.0f) * 90.0f));
  return look;
}
// Frosted's blur where it was let go, as it is: 55.2%, not rounded.
[[nodiscard]] inline std::optional<config::bubble_look> look_after(const looks_picked& now, const request::change_bubbles& asked,
                                                                   const request::bubbles_change::blur& slid) {
  if (!look_usable(now, asked.part) || !look_own(now, asked.level, asked.part))
    return std::nullopt;
  config::bubble_look look = look_in(now, asked.level, asked.part);
  look.blur = static_cast<double>(std::clamp(slid.at, 0.0f, 1.0f)) * 100.0;
  return look;
}
// An element's own, in the units its look keeps: a whole percent of
// opacity, a blur as it is.
[[nodiscard]] inline std::optional<int> element_amount(request::element_opacity_t, std::optional<float> at) {
  return at.transform([](float one) { return static_cast<int>(std::lround(std::clamp(one, 0.0f, 1.0f) * 100.0f)); });
}
[[nodiscard]] inline std::optional<double> element_amount(request::element_blur_t, std::optional<float> at) {
  return at.transform([](float one) { return static_cast<double>(std::clamp(one, 0.0f, 1.0f)) * 100.0; });
}
[[nodiscard]] inline auto& element_place(config::bubble_look& look, request::element_opacity_t which) { return look.elements.*which; }
[[nodiscard]] inline auto& element_place(config::bubble_look& look, request::element_blur_t which) { return look.blurs.*which; }
template <class Which>
[[nodiscard]] std::optional<config::bubble_look> look_after(const looks_picked& now, const request::change_bubbles& asked,
                                                            const request::bubbles_change::element<Which>& slid) {
  if (!look_own(now, asked.level, config::look_part::bubbles{}))
    return std::nullopt;
  config::bubble_look look = look_in(now, asked.level, config::look_part::bubbles{});
  element_place(look, slid.which) = element_amount(slid.which, slid.at);
  return look;
}
// Whose look a change is: the part's -- an element's, the bubbles'.
[[nodiscard]] inline config::look_part_t look_part_of(const config::look_part_t& part, const request::bubbles_change::opacity&) { return part; }
[[nodiscard]] inline config::look_part_t look_part_of(const config::look_part_t& part, const request::bubbles_change::blur&) { return part; }
template <class Which>
[[nodiscard]] config::look_part_t look_part_of(const config::look_part_t&, const request::bubbles_change::element<Which>&) {
  return config::look_part::bubbles{};
}
// What the program sets for a choice: the look at its level -- or, a kind
// of none, as the level over it says.
[[nodiscard]] inline std::optional<request::set_bubbles> bubbles_asked(const looks_picked& now, const request::change_bubbles& asked) {
  return spl::visit(
      spl::overloaded{[&](const request::bubbles_change::kind& picked) -> std::optional<request::set_bubbles> {
                        if (!look_usable(now, asked.part))
                          return std::nullopt;
                        if (!picked.to)
                          return request::set_bubbles{asked.level, std::nullopt, asked.part};
                        config::bubble_look look = look_in(now, asked.level, asked.part);
                        look.kind = *picked.to;
                        return request::set_bubbles{asked.level, look, asked.part};
                      },
                      [&](const auto& slid) {
                        return look_after(now, asked, slid).transform([&](config::bubble_look look) {
                          return request::set_bubbles{asked.level, std::move(look), look_part_of(asked.part, slid)};
                        });
                      }},
      asked.change);
}

// ---- the choices -----------------------------------------------------------------
// The kinds, as the menu lists them: "As above" first where there is a
// level above.
inline const std::array<config::bubbles_t, 4> kBubbleKinds{config::bubbles::solid{}, config::bubbles::translucent{},
                                                          config::bubbles::frosted{}, config::bubbles::glass{}};
[[nodiscard]] inline std::vector<std::string> kind_names(const choice_level_t& level) {
  std::vector<std::string> out;
  if (has_level_above(level))
    out.emplace_back("As above");
  out.append_range(std::array<std::string, 4>{"Solid", "Translucent", "Frosted", "Glass"});
  return out;
}
// The option in use: what the level holds, else as the level over it.
[[nodiscard]] inline std::size_t kind_index(const looks_picked& now, const choice_level_t& level, const config::look_part_t& part) {
  const auto& own = held_look(now.at(level), part);
  return own ? (has_level_above(level) ? 1 : 0) + own->kind.index() : 0;
}
struct pick_kind_at {
  using Answer = std::optional<request::change_bubbles>;
  choice_level_t level;
  config::look_part_t part;
  Answer operator()(std::size_t index) const {
    const std::size_t shift = has_level_above(level) ? 1 : 0;
    if (index < shift)
      return request::change_bubbles{level, part, request::bubbles_change::kind{std::nullopt}};
    if (index - shift >= kBubbleKinds.size())
      return std::nullopt;
    return request::change_bubbles{level, part, request::bubbles_change::kind{kBubbleKinds[index - shift]}};
  }
};
// A slider let go: where, as the change it is.
template <class Change>
struct look_slid {
  using Answer = request::change_bubbles;
  choice_level_t level;
  config::look_part_t part;
  Answer operator()(float at) const { return {level, part, Change{at}}; }
};
template <class Which>
struct element_slid {
  using Answer = request::change_bubbles;
  choice_level_t level;
  Which which;
  Answer operator()(float at) const { return {level, config::look_part::bubbles{}, request::bubbles_change::element<Which>{which, at}}; }
};
template <class Which>
struct element_given_back {
  using Answer = request::change_bubbles;
  choice_level_t level;
  Which which;
  Answer operator()() const { return {level, config::look_part::bubbles{}, request::bubbles_change::element<Which>{which, std::nullopt}}; }
};
// An element's row, as its look has it: its own or the bubbles', where its
// slider is, and whether it has one to give back.
[[nodiscard]] inline std::string element_label(const config::bubble_look& look, const window_look_t&, std::string_view name,
                                               request::element_opacity_t which) {
  return std::format("{}: {}%{}", name, element_opacity_of(look, which), (look.elements.*which) ? "" : " (as bubbles)");
}
[[nodiscard]] inline std::string element_label(const config::bubble_look& look, const window_look_t& window, std::string_view name,
                                               request::element_blur_t which) {
  return std::format("{} blur: {:.1f}%{}", name, element_blur_of(look, which, window) * 100.0f, (look.blurs.*which) ? "" : " (as bubbles)");
}
[[nodiscard]] inline float element_fraction(const config::bubble_look& look, const window_look_t&, request::element_opacity_t which) {
  return static_cast<float>(element_opacity_of(look, which)) / 100.0f;
}
[[nodiscard]] inline float element_fraction(const config::bubble_look& look, const window_look_t& window, request::element_blur_t which) {
  return element_blur_of(look, which, window);
}
[[nodiscard]] inline bool element_own(const config::bubble_look& look, request::element_opacity_t which) { return (look.elements.*which).has_value(); }
[[nodiscard]] inline bool element_own(const config::bubble_look& look, request::element_blur_t which) { return (look.blurs.*which).has_value(); }

// A slider under its line, as the looks have them: where it is, read.
template <class Done>
MUX_INLINE_IN_RELEASE auto look_slider(const palette& colours, auto fraction, Done done, scene::Spec spec) {
  namespace c = skiff::compose;
  using bar_t = widgets::SliderBar<scene::NoAction, Done>;
  return SKIFF_NAMED(look_slider, c::applied<looks_picked>(std::move(fraction), [](bar_t& bar, float at) { bar.setFraction(at); },
                                  c::styled(spec, bar_t(colours.widgets, {}, std::move(done)))));
}
// What else is in a chat -- an element's opacity, or its blur where it is
// drawn frosted -- apart from the bubbles': its line, its way back to
// them where it has its own, and its slider.
template <class Which>
MUX_INLINE_IN_RELEASE auto element_row(const palette& colours, choice_level_t level, std::string_view name, Which which) {
  namespace c = skiff::compose;
  const auto look = [level](const looks_picked& now) { return look_in(now, level, config::look_part::bubbles{}); };
  return SKIFF_NAMED(element_row, c::column(c::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      label_button_row_for<looks_picked>(colours,
          [look, name, which](const looks_picked& now) { return element_label(look(now), now.window, name, which); },
          "As bubbles", element_given_back<Which>{level, which},
          [look, which](const looks_picked& now) { return element_own(look(now), which); }),
      look_slider(colours, [look, which](const looks_picked& now) { return element_fraction(look(now), now.window, which); },
                  element_slid<Which>{level, which}, {.margin = {4.0f, 8.0f, 6.0f, 8.0f}})));
}
template <class Which, std::size_t N>
MUX_INLINE_IN_RELEASE auto element_rows(const palette& colours, choice_level_t level, const std::array<std::string_view, N>& labels,
                         const std::array<std::pair<std::string_view, Which>, N>& elements) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(element_rows, c::many(c::vbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                 std::views::zip(labels, elements) | std::views::transform([&](const auto& one) {
                   return element_row(colours, level, std::get<0>(one), std::get<1>(one).second);
                 }) | std::ranges::to<std::vector>()));
}

// How bubbles -- or the panels -- look, at a level: as the level over it
// (where there is one), or a kind, in a menu with the one in use marked;
// how opaque, on a slider told when it is let go; Frosted's blur; and the
// bubbles' for each element apart. What does nothing greyed: all of it
// where the panels have nothing behind them, what is under the kind where
// the level is as above.
MUX_INLINE_IN_RELEASE auto bubbles_picker(const palette& colours, choice_level_t level, config::look_part_t part) {
  namespace c = skiff::compose;
  const bool bubbles = is_bubbles(part);
  const auto look = [level, part](const looks_picked& now) { return look_in(now, level, part); };
  const auto frosted = [look](const looks_picked& now) { return frosts(look(now)); };
  const auto greyed = [](bool live) { return scene::Spec{.alpha = live ? 1.0f : 0.4f, .disabled = !live}; };
  static constexpr std::array<std::string_view, 4> kElementLabels{"Service lines", "Images", "Avatars", "Reactions"};
  static constexpr std::array<std::string_view, 2> kBlurLabels{"Service lines", "Reactions"};
  return c::column(c::vbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 10.0f, 0.0f, 10.0f}}),
      nodes::Text(bubbles ? "MESSAGE BUBBLES" : "PANELS", 13.0f, colours.dim, true),
      c::visible(!bubbles, c::styled({.fillX = true},
          wrapped(nodes::Text("The chat list, the bars and the side panels: only over a background behind the whole window "
                              "(Appearance → Chat background → Behind the whole window).",
                              12.0f, colours.dim)))),
      c::spec_for<looks_picked>([part](const looks_picked& now) { return scene::Spec{.alpha = look_usable(now, part) ? 1.0f : 0.4f}; },
          c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
              c::projected<looks_picked>([level, part](const looks_picked& now) { return kind_index(now, level, part); },
                  make_choice_menu(colours, "", kind_names(level), 0, pick_kind_at{level, part})))),
      c::spec_for<looks_picked>([level, part, greyed](const looks_picked& now) {
        return greyed(look_usable(now, part) && look_own(now, level, part));
      },
          c::column(c::vbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}),
              c::text_for<looks_picked>([look](const looks_picked& now) { return std::format("Opacity: {}%", look(now).opacity); },
                  nodes::Text("", 13.0f, colours.text)),
              look_slider(colours, [look](const looks_picked& now) { return static_cast<float>(look(now).opacity - 10) / 90.0f; },
                          look_slid<request::bubbles_change::opacity>{level, part}, {.margin = {4.0f, 8.0f, 8.0f, 8.0f}}),
              // Frosted only: how much it blurs.
              c::shown_for<looks_picked>(frosted, c::column(c::vbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                  c::text_for<looks_picked>([look](const looks_picked& now) { return std::format("Blur: {:.1f}%", blur_of(look(now), now.window) * 100.0f); },
                      nodes::Text("", 13.0f, colours.text)),
                  look_slider(colours, [look](const looks_picked& now) { return blur_of(look(now), now.window); },
                              look_slid<request::bubbles_change::blur>{level, part}, {.margin = {4.0f, 8.0f, 8.0f, 8.0f}}))),
              // The bubbles' only: what else is in a chat, each apart where
              // chosen; those drawn frosted where the bubbles are, each its blur.
              c::visible(bubbles, c::column(c::vbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                  nodes::Text("EVERYTHING ELSE IN A CHAT", 12.0f, colours.dim, true),
                  element_rows(colours, level, kElementLabels, config::kElementNames),
                  c::shown_for<looks_picked>(frosted, element_rows(colours, level, kBlurLabels, config::kElementBlurNames)))))));
}
using bubbles_picker_t = decltype(bubbles_picker(std::declval<const palette&>(), choice_level_t{}, config::look_part_t{}));

// A chat's background, at a level: the menu's names -- the picture's by its
// file's, where one is chosen there -- and the one in use.
struct background_shown {
  std::vector<std::string> names;
  std::size_t at = 0;
  friend bool operator==(const background_shown&, const background_shown&) = default;
};
[[nodiscard]] inline background_shown background_of(const looks_picked& now, const choice_level_t& level) {
  const auto& own = now.at(level).wallpaper;
  std::vector<std::string> names;
  if (has_level_above(level))
    names.emplace_back("As above");
  names.append_range(std::array<std::string, 2>{"Theme default", "Plain colour"});
  names.push_back(spl::visit(spl::overloaded{[](const config::wallpaper::picture& at) {
                                               return std::format("Image: {}", std::filesystem::path(at.path).filename().string());
                                             },
                                             [](const auto&) { return std::string("Image…"); }},
                             own.value_or(config::wallpaper_t{config::wallpaper::theme{}})));
  return {std::move(names), own ? (has_level_above(level) ? 1 : 0) + own->index() : 0};
}
struct pick_wallpaper_at {
  using Answer = std::optional<request::set_wallpaper>;
  choice_level_t level;
  Answer operator()(std::size_t index) const {
    static const std::array<config::wallpaper_pick_t, 3> picks{config::wallpaper_pick::theme{}, config::wallpaper_pick::plain{},
                                                               config::wallpaper_pick::picture{}};
    const std::size_t shift = has_level_above(level) ? 1 : 0;
    if (index < shift)
      return request::set_wallpaper{level, config::wallpaper_pick::inherit{}};
    if (index - shift >= picks.size())
      return std::nullopt;
    return request::set_wallpaper{level, picks[index - shift]};
  }
};
using background_menu_t = choice_menu<pick_wallpaper_at>;
[[nodiscard]] inline std::string background_note(const looks_picked& now, const choice_level_t& level) {
  const std::string where = now.window.behind ? "Behind the whole window" : "Behind the messages";
  return spl::visit(spl::overloaded{[&](choice_level::everywhere) { return where + ", in every chat."; },
                                    [&](choice_level::account) { return where + ", in this account's chats."; },
                                    [&](choice_level::chat) { return where + ", in this chat."; }},
                    level);
}

// What a chat looks like, at a level: its background, its bubbles, and the
// panels round it -- in Appearance (every chat's), an account's Chats page
// (its own), Manage's Appearance (a room's) and the background dialog.
MUX_INLINE_IN_RELEASE auto look_choices(const palette& colours, choice_level_t level) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(look_choices, c::column(c::vbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      c::styled({.margin = {0.0f, 10.0f, 0.0f, 10.0f}}, nodes::Text("BACKGROUND", 13.0f, colours.dim, true)),
      c::text_for<looks_picked>([level](const looks_picked& now) { return background_note(now, level); },
          c::styled({.fillX = true, .margin = {0.0f, 10.0f, 4.0f, 10.0f}}, wrapped(nodes::Text("", 13.0f, colours.dim)))),
      // Its names move with the picture chosen: made again where they do.
      c::projected<looks_picked>([level](const looks_picked& now) { return std::optional(background_of(now, level)); },
          c::mount<background_menu_t, background_shown>([at = &colours, level](const background_shown& shown) {
            return std::tuple{make_choice_menu(*at, "", shown.names, shown.at, pick_wallpaper_at{level})};
          },
              {.fillX = true, .autoSize = scene::axes::kY, .margin = {0.0f, 10.0f, 0.0f, 10.0f}})),
      c::styled({.margin = {10.0f, 0.0f, 0.0f, 0.0f}}, bubbles_picker(colours, level, config::look_part::bubbles{})),
      c::styled({.margin = {10.0f, 0.0f, 0.0f, 0.0f}}, bubbles_picker(colours, level, config::look_part::panels{}))));
}
using look_choices_t = decltype(look_choices(std::declval<const palette&>(), choice_level_t{}));



// Its pickers on the pages a level's looks are chosen on: Appearance (every
// chat's), an account's page (its own), a room's Appearance tab (the
// room's) -- mux.ui:appearance, :accounts, :room_settings.
MUX_INLINE_IN_RELEASE auto extend(contract::level_sections, type_tag<feature>, const palette& colours, choice_level_t level, const ui_shared&) {
  return look_choices(colours, level);
}

}  // namespace mux::plugins::looks
