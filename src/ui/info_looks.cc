// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:info_looks -- Bubbles' and panels' looks picked, and a chat's background.
export module mux.ui:info_looks;

import std;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
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
import :base;
import :icons;
import :avatars;
import :controls;
import :themes;
import :names;
import :forms;
import :composer;
import :message;
import :html;
import :timeline;  // a message's menu, for the reactions list's bubbles
import :info_common;
import :info_cards;
import :info_reactions;
import :info_new_chats;

export namespace mux::ui {
// A chat's background, chosen: for every chat, an account's, or one chat
// -- as the level over it says, the theme's (its gradient and Telegram's
// pattern), a plain colour, or a picture of one's own.
// How bubbles -- or the panels -- look, at a level: as the level over it
// (where there is one), or a kind -- the one in use marked -- and how
// opaque, on a slider told when it is let go: made again once, not at each
// step of a drag. The panels' only over the background behind the whole
// window: else shown greyed, and nothing done.
// A level, with the looks it is shown from: what the choices read.
struct look_level {
  choice_level_t level;
  const looks_shown* looks = nullptr;
  // And the colours the choices are made in.
  const palette* colours = nullptr;
};
template <class Actions>
struct bubbles_picker : nodes::Stack {
  // The look at the level, as the UI knows it: every chat's, or the chat's
  // shown.
  [[nodiscard]] static config::bubble_look current(const look_level& level, const config::look_part_t& part) {
    const bool everywhere = !has_level_above(level.level);
    return spl::visit(spl::overloaded{[&](config::look_part::bubbles) {
                                              return everywhere ? level.looks->bubbles_everywhere : level.looks->bubbles;
                                            },
                                            [&](config::look_part::panels) {
                                              return everywhere ? level.looks->panels_everywhere : level.looks->panels;
                                            }},
                         part);
  }
  [[nodiscard]] static bool usable(const look_level& level, const config::look_part_t& part) {
    return spl::visit(spl::overloaded{[](config::look_part::bubbles) { return true; },
                                            [&](config::look_part::panels) { return level.looks->window.behind; }},
                         part);
  }
  struct inherit_it {
    Actions* actions;
    look_level level;
    config::look_part_t part;
    void operator()() const {
      if (usable(level, part))
        actions->set_bubbles(level.level, std::nullopt, part);
    }
  };
  struct pick_kind {
    Actions* actions;
    look_level level;
    config::look_part_t part;
    config::bubbles_t kind;
    void operator()() const {
      if (usable(level, part))
        actions->set_bubbles(level.level, config::bubble_look{kind, current(level, part).opacity}, part);
    }
  };
  // An opacity let go at: of the kind in use -- solid has none, so
  // translucent.
  struct opacity_done {
    Actions* actions;
    look_level level;
    config::look_part_t part;
    void operator()(float fraction) const {
      if (!usable(level, part) || !own_here(level, part))
        return;
      config::bubble_look look = current(level, part);
      look.kind = spl::visit(spl::overloaded{[](config::bubbles::solid) { return config::bubbles_t{config::bubbles::translucent{}}; },
                                                   [](const auto& other) { return config::bubbles_t{other}; }},
                                look.kind);
      look.opacity = static_cast<int>(std::lround(10.0f + std::clamp(fraction, 0.0f, 1.0f) * 90.0f));
      actions->set_bubbles(level.level, look, part);
    }
  };
  // An element's opacity, apart from the bubbles': let go at, or given back
  // to them.
  using element_t = std::optional<int> config::element_opacity::*;
  struct element_done {
    Actions* actions;
    look_level level;
    element_t which;
    void operator()(float fraction) const {
      if (!own_here(level, config::look_part::bubbles{}))
        return;
      config::bubble_look look = current(level, config::look_part::bubbles{});
      look.elements.*which = static_cast<int>(std::lround(std::clamp(fraction, 0.0f, 1.0f) * 100.0f));
      actions->set_bubbles(level.level, look, config::look_part::bubbles{});
    }
  };
  struct element_reset {
    Actions* actions;
    look_level level;
    element_t which;
    void operator()() const {
      if (!own_here(level, config::look_part::bubbles{}))
        return;
      config::bubble_look look = current(level, config::look_part::bubbles{});
      look.elements.*which = std::nullopt;
      actions->set_bubbles(level.level, look, config::look_part::bubbles{});
    }
  };
  struct element_row : nodes::Stack {
    struct head_t : label_button_row<element_reset> {
      head_t(const palette& colours, std::string label, element_reset reset, bool own)
          : label_button_row<element_reset>(colours, std::move(label), "As bubbles", reset, own) {}
    };
    struct parts_t {
      head_t head;
      widgets::SliderBar<scene::NoAction, element_done> bar;
    } parts;
    element_row(Actions* a, const look_level& level, std::string_view name, element_t which)
        : parts{.head = head_t(*level.colours, std::format("{}: {}%{}", name, element_opacity_of(current(level, config::look_part::bubbles{}), which),
                                           (current(level, config::look_part::bubbles{}).elements.*which) ? "" : " (as bubbles)"),
                               element_reset{a, level, which},
                               (current(level, config::look_part::bubbles{}).elements.*which).has_value()),
                .bar = widgets::SliderBar<scene::NoAction, element_done>(level.colours->widgets, {}, element_done{a, level, which})} {
      this->setGap(4.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY});
      parts.bar.setFraction(static_cast<float>(element_opacity_of(current(level, config::look_part::bubbles{}), which)) / 100.0f);
      parts.bar.apply({.margin = {4.0f, 8.0f, 6.0f, 8.0f}});
    }
  };
  [[nodiscard]] static std::vector<std::string> kind_names(const look_level& level) {
    std::vector<std::string> out;
    if (has_level_above(level.level))
      out.emplace_back(spl::visit(spl::overloaded{[](choice_level::chat) { return "As above"; },
                                                        [](const auto&) { return "As above"; }},
                                     level.level));
    for (const char* name : {"Solid", "Translucent", "Frosted", "Glass"})
      out.emplace_back(name);
    return out;
  }
  // The option in use: what the level holds, else as the level over it.
  [[nodiscard]] static std::size_t kind_index(const look_level& level, const config::look_part_t& part) {
    const looks_held& held = level.looks->at(level.level);
    const std::optional<config::bubble_look>& own =
        spl::visit(spl::overloaded{[&](config::look_part::bubbles) -> const std::optional<config::bubble_look>& { return held.bubbles; },
                                         [&](config::look_part::panels) -> const std::optional<config::bubble_look>& { return held.panels; }},
                      part);
    const std::size_t shift = has_level_above(level.level) ? 1 : 0;
    if (!own)
      return 0;
    return shift + own->kind.index();
  }
  // Whether the level holds a look of its own: as above, what is under the
  // choice of kind is the level over it's -- shown greyed, and left alone.
  [[nodiscard]] static bool own_here(const look_level& level, const config::look_part_t& part) {
    if (!has_level_above(level.level))
      return true;
    const looks_held& held = level.looks->at(level.level);
    return spl::visit(spl::overloaded{[&](config::look_part::bubbles) { return held.bubbles.has_value(); },
                                            [&](config::look_part::panels) { return held.panels.has_value(); }},
                         part);
  }
  struct pick_kind_at {
    Actions* actions;
    look_level level;
    config::look_part_t part;
    bool inherit;
    void operator()(std::size_t index) const {
      if (!usable(level, part))
        return;
      if (inherit && index == 0) {
        actions->set_bubbles(level.level, std::nullopt, part);
        return;
      }
      static const std::array<config::bubbles_t, 4> kinds{config::bubbles::solid{}, config::bubbles::translucent{},
                                                         config::bubbles::frosted{}, config::bubbles::glass{}};
      const std::size_t at = index - (inherit ? 1 : 0);
      if (at >= kinds.size())
        return;
      config::bubble_look look = current(level, part);
      look.kind = kinds[at];
      actions->set_bubbles(level.level, look, part);
    }
  };
  // Frosted's blur, let go at: the look's own -- the bubbles' apart from the
  // panels' -- where it was let go, as it is: 55.2%, not rounded.
  struct blur_done {
    Actions* actions;
    look_level level;
    config::look_part_t part;
    void operator()(float fraction) const {
      if (!usable(level, part) || !own_here(level, part))
        return;
      config::bubble_look look = current(level, part);
      look.blur = static_cast<double>(std::clamp(fraction, 0.0f, 1.0f)) * 100.0;
      actions->set_bubbles(level.level, look, part);
    }
  };
  // An element's blur, apart from the bubbles': let go at, or given back.
  using element_blur_t = std::optional<double> config::element_blur::*;
  struct element_blur_done {
    Actions* actions;
    look_level level;
    element_blur_t which;
    void operator()(float fraction) const {
      if (!own_here(level, config::look_part::bubbles{}))
        return;
      config::bubble_look look = current(level, config::look_part::bubbles{});
      look.blurs.*which = static_cast<double>(std::clamp(fraction, 0.0f, 1.0f)) * 100.0;
      actions->set_bubbles(level.level, look, config::look_part::bubbles{});
    }
  };
  struct element_blur_reset {
    Actions* actions;
    look_level level;
    element_blur_t which;
    void operator()() const {
      if (!own_here(level, config::look_part::bubbles{}))
        return;
      config::bubble_look look = current(level, config::look_part::bubbles{});
      look.blurs.*which = std::nullopt;
      actions->set_bubbles(level.level, look, config::look_part::bubbles{});
    }
  };
  // An element drawn frosted: its blur, and a way back to the bubbles'.
  struct element_blur_row : nodes::Stack {
    struct head_t : label_button_row<element_blur_reset> {
      head_t(const palette& colours, std::string label, element_blur_reset reset, bool own)
          : label_button_row<element_blur_reset>(colours, std::move(label), "As bubbles", reset, own) {}
    };
    struct parts_t {
      head_t head;
      widgets::SliderBar<scene::NoAction, element_blur_done> bar;
    } parts;
    element_blur_row(Actions* a, const look_level& level, std::string_view name, element_blur_t which)
        : parts{.head = head_t(*level.colours, std::format("{} blur: {:.1f}%{}", name,
                                           element_blur_of(current(level, config::look_part::bubbles{}), which, level.looks->window) * 100.0f,
                                           (current(level, config::look_part::bubbles{}).blurs.*which) ? "" : " (as bubbles)"),
                               element_blur_reset{a, level, which},
                               (current(level, config::look_part::bubbles{}).blurs.*which).has_value()),
                .bar = widgets::SliderBar<scene::NoAction, element_blur_done>(level.colours->widgets, {}, element_blur_done{a, level, which})} {
      this->setGap(4.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY});
      parts.bar.setFraction(element_blur_of(current(level, config::look_part::bubbles{}), which, level.looks->window));
      parts.bar.apply({.margin = {4.0f, 8.0f, 6.0f, 8.0f}});
    }
  };
  struct kinds_row : nodes::Stack {
    struct parts_t {
      widgets::Button<pick_kind> solid, translucent, frosted, glass;
    } parts;
    kinds_row(Actions* a, const look_level& level, const config::look_part_t& part)
        : parts{.solid = widgets::Button<pick_kind>(level.colours->widgets, "Solid", {a, level, part, config::bubbles::solid{}}),
                .translucent = widgets::Button<pick_kind>(level.colours->widgets, "Translucent", {a, level, part, config::bubbles::translucent{}}),
                .frosted = widgets::Button<pick_kind>(level.colours->widgets, "Frosted", {a, level, part, config::bubbles::frosted{}}),
                .glass = widgets::Button<pick_kind>(level.colours->widgets, "Glass", {a, level, part, config::bubbles::glass{}})} {
      this->setHorizontal();
      this->setGap(6.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY});
      const config::bubbles_t now = current(level, part).kind;
      for (widgets::Button<pick_kind>* each : {&parts.solid, &parts.translucent, &parts.frosted, &parts.glass})
        each->apply({.height = 32.0f, .grow = scene::axes::kX, .disabled = !usable(level, part)});
      parts.solid.setPrimary(now == config::bubbles_t{config::bubbles::solid{}});
      parts.translucent.setPrimary(now == config::bubbles_t{config::bubbles::translucent{}});
      parts.frosted.setPrimary(now == config::bubbles_t{config::bubbles::frosted{}});
      parts.glass.setPrimary(now == config::bubbles_t{config::bubbles::glass{}});
    }
  };
  struct parts_t {
    nodes::Text title;
    nodes::Text why;
    choice_menu<pick_kind_at> kinds;
    nodes::Text opacity_label;
    widgets::SliderBar<scene::NoAction, opacity_done> opacity;
    // Frosted only: how much it blurs.
    nodes::Text blur_label;
    widgets::SliderBar<scene::NoAction, blur_done> blur;
    // The bubbles' only: what else is in a chat, each apart where chosen.
    nodes::Text elements_title;
    std::vector<element_row> elements;
    // Those drawn frosted, where the bubbles are: each its blur.
    std::vector<element_blur_row> element_blurs;
  } parts;
  bubbles_picker(Actions* a, const look_level& level, const config::look_part_t& part = config::look_part::bubbles{})
      : parts{.title = nodes::Text(spl::visit(spl::overloaded{[](config::look_part::bubbles) { return "MESSAGE BUBBLES"; },
                                                                    [](config::look_part::panels) { return "PANELS"; }},
                                                 part),
                                   13.0f, level.colours->dim, true),
              .why = nodes::Text("The chat list, the bars and the side panels: only over a background behind the whole "
                                 "window (Appearance \u2192 Chat background \u2192 Behind the whole window).",
                                 12.0f, level.colours->dim),
              .kinds = choice_menu<pick_kind_at>(*level.colours, "", kind_names(level), kind_index(level, part),
                                                 pick_kind_at{a, level, part, has_level_above(level.level)}),
              .opacity_label = nodes::Text("Opacity", 13.0f, level.colours->text),
              .opacity = widgets::SliderBar<scene::NoAction, opacity_done>(level.colours->widgets, {}, opacity_done{a, level, part}),
              .blur_label = nodes::Text(std::format("Blur: {:.1f}%", blur_of(current(level, part), level.looks->window) * 100.0f), 13.0f, level.colours->text),
              .blur = widgets::SliderBar<scene::NoAction, blur_done>(level.colours->widgets, {}, blur_done{a, level, part}),
              .elements_title = nodes::Text("EVERYTHING ELSE IN A CHAT", 12.0f, level.colours->dim, true)} {
    this->setGap(8.0f);
    fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 10.0f, 0.0f, 10.0f}});
    parts.why.setWrapped(true);
    parts.why.apply({.fillX = true});
    parts.why.setVisible(spl::visit(spl::overloaded{[](config::look_part::panels) { return true; },
                                                          [](const auto&) { return false; }},
                                       part));
    const int opacity = current(level, part).opacity;
    parts.opacity_label.setText(std::format("Opacity: {}%", opacity));
    parts.opacity.setFraction(static_cast<float>(opacity - 10) / 90.0f);
    parts.opacity.apply({.margin = {4.0f, 8.0f, 8.0f, 8.0f}});
    const bool frosted = spl::visit(spl::overloaded{[](config::bubbles::frosted) { return true; }, [](const auto&) { return false; }},
                                       current(level, part).kind);
    parts.blur_label.setVisible(frosted);
    parts.blur.setVisible(frosted);
    parts.blur.setFraction(blur_of(current(level, part), level.looks->window));
    parts.blur.apply({.margin = {4.0f, 8.0f, 8.0f, 8.0f}});
    const bool bubbles = spl::visit(spl::overloaded{[](config::look_part::bubbles) { return true; },
                                                          [](const auto&) { return false; }},
                                       part);
    parts.elements_title.setVisible(bubbles);
    if (bubbles) {
      static constexpr std::array<std::string_view, 4> kLabels{"Service lines", "Images", "Avatars", "Reactions"};
      parts.elements.reserve(config::kElementNames.size());
      for (std::size_t i = 0; i < config::kElementNames.size(); ++i)
        parts.elements.emplace_back(a, level, kLabels[i], config::kElementNames[i].second);
      // Frosted: what else is drawn so, each its blur.
      if (frosted) {
        static constexpr std::array<std::string_view, 2> kBlurLabels{"Service lines", "Reactions"};
        parts.element_blurs.reserve(config::kElementBlurNames.size());
        for (std::size_t i = 0; i < config::kElementBlurNames.size(); ++i)
          parts.element_blurs.emplace_back(a, level, kBlurLabels[i], config::kElementBlurNames[i].second);
      }
    }
    // Greyed where it does nothing: all of it where the panels have nothing
    // behind them; under the kind, where the level is as above.
    if (!usable(level, part))
      parts.kinds.apply({.alpha = 0.4f});
    if (!usable(level, part) || !own_here(level, part)) {
      for (scene::Node* each : std::initializer_list<scene::Node*>{&parts.opacity_label, &parts.opacity, &parts.elements_title})
        each->apply({.alpha = 0.4f, .disabled = true});
      for (element_row& each : parts.elements)
        each.apply({.alpha = 0.4f, .disabled = true});
    }
  }
};

// What a chat looks like, at a level: its background, its bubbles, and the
// panels round it -- in the dialog (every chat's, an account's) and in a
// room's Manage (its own).
template <class Actions>
struct look_choices : nodes::Stack {
  struct pick_wallpaper_at {
    Actions* actions;
    choice_level_t level;
    bool inherit;
    void operator()(std::size_t index) const {
      if (inherit && index == 0) {
        actions->set_wallpaper(level, config::wallpaper_pick::inherit{});
        return;
      }
      static const std::array<config::wallpaper_pick_t, 3> picks{config::wallpaper_pick::theme{}, config::wallpaper_pick::plain{},
                                                                config::wallpaper_pick::picture{}};
      if (const std::size_t at = index - (inherit ? 1 : 0); at < picks.size())
        actions->set_wallpaper(level, picks[at]);
    }
  };
  [[nodiscard]] static std::vector<std::string> background_names(const looks_shown& looks, const choice_level_t& level) {
    std::vector<std::string> out;
    if (has_level_above(level))
      out.emplace_back(spl::visit(spl::overloaded{[](choice_level::chat) { return "As above"; },
                                                        [](const auto&) { return "As above"; }},
                                     level));
    for (const char* name : {"Theme default", "Plain colour"})
      out.emplace_back(name);
    // The picture's: by its file's name, where one is chosen here.
    out.emplace_back(spl::visit(
        spl::overloaded{[](const config::wallpaper::picture& at) {
                             return std::format("Image: {}", std::filesystem::path(at.path).filename().string());
                           },
                           [](const auto&) { return std::string("Image\u2026"); }},
        looks.at(level).wallpaper.value_or(config::wallpaper_t{config::wallpaper::theme{}})));
    return out;
  }
  [[nodiscard]] static std::size_t background_index(const looks_shown& looks, const choice_level_t& level) {
    const auto& own = looks.at(level).wallpaper;
    if (!own)
      return 0;
    return (has_level_above(level) ? 1 : 0) + own->index();
  }
  struct parts_t {
    nodes::Text background_title;
    nodes::Text note;
    choice_menu<pick_wallpaper_at> background;
    bubbles_picker<Actions> bubbles;
    bubbles_picker<Actions> panels;
  } parts;
  [[nodiscard]] static std::string note_of(const looks_shown& looks, const choice_level_t& level) {
    const std::string where = looks.window.behind ? "Behind the whole window" : "Behind the messages";
    return spl::visit(spl::overloaded{[&](choice_level::everywhere) { return where + ", in every chat."; },
                                            [&](choice_level::account) { return where + ", in this account's chats."; },
                                            [&](choice_level::chat) { return where + ", in this chat."; }},
                         level);
  }
  look_choices(Actions* a, const palette& colours, const looks_shown& looks, choice_level_t level)
      : parts{.background_title = nodes::Text("BACKGROUND", 13.0f, colours.dim, true),
              .note = nodes::Text(note_of(looks, level), 13.0f, colours.dim),
              .background = choice_menu<pick_wallpaper_at>(colours, "", background_names(looks, level), background_index(looks, level),
                                                           pick_wallpaper_at{a, level, has_level_above(level)}),
              .bubbles = bubbles_picker<Actions>(a, look_level{level, &looks, &colours}, config::look_part::bubbles{}),
              .panels = bubbles_picker<Actions>(a, look_level{level, &looks, &colours}, config::look_part::panels{})} {
    this->setGap(8.0f);
    fState.apply({.fillX = true, .autoSize = scene::axes::kY});
    parts.background_title.apply({.margin = {0.0f, 10.0f, 0.0f, 10.0f}});
    parts.note.setWrapped(true);
    parts.note.apply({.fillX = true, .margin = {0.0f, 10.0f, 4.0f, 10.0f}});
    parts.background.apply({.margin = {0.0f, 10.0f, 0.0f, 10.0f}});
    parts.bubbles.apply({.margin = {10.0f, 0.0f, 0.0f, 0.0f}});
    parts.panels.apply({.margin = {10.0f, 0.0f, 0.0f, 0.0f}});
  }
};

// A chat background and its looks chosen, at a level, in a dialog.
template <class Actions>
struct wallpaper_box : nodes::Stack {
  // The dialog it is shown in.
  [[nodiscard]] static dialog_look look_of_dialog() { return {.size = dialog_size::fitting{380.0f}}; }
  struct close_it {
    Actions* actions;
    void operator()() const { actions->close_wallpaper(); }
  };
  using header_t = page_header<no_back, close_it>;
  struct parts_t {
    header_t header;
    look_choices<Actions> choices;
  } parts;
  wallpaper_box(Actions* a, const palette& colours, const looks_shown& looks, choice_level_t level)
      : parts{.header = header_t(colours, "Chat background and looks", {}, {a}, false, true),
              .choices = look_choices<Actions>(a, colours, looks, level)} {
    this->setGap(8.0f);
    fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 12.0f, 18.0f, 12.0f}});
  }
};

}  // namespace mux::ui
