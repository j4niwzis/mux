// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.reactions.ui -- Reacting from a message's menu: its quick
// reactions, and every emoji unrolled under them. A message's reactions, at the foot of its
// bubble, as Telegram's: a chip for each -- its emoji or picture, how many,
// or the faces of up to three who chose it -- tinted where one's own is
// among them; pressed, one reacts with it too. And who reacted, listed as
// the events they are, from the message's menu.
export module mux.plugin.reactions.ui;

import std;
import skia;
import skiff.scene;
import skiff.compose;
import skiff.nodes.flow;
import skiff.nodes.text;
import skiff.nodes.scroll;
import skiff.nodes.icon;
import skiff.nodes.box;
import splice;
import mux.composition;
import mux.core;
import mux.config;
import mux.ui.base;
import mux.plugin.reactions.types;

export namespace mux::plugins::reactions {
// What a message's parts are made of: the window's own.
using namespace ::mux::ui;

inline auto reaction_chip(const palette& colours, const looks_shown& looks,
                          std::string message, std::string key, std::size_t count, bool own,
                          const std::vector<emote>& emotes,
                          const std::vector<std::pair<std::string, std::string>>& people = {},
                          std::optional<std::string> shortcode = std::nullopt) {
  namespace c = skiff::compose;
  const auto colour = own ? colours.on_accent : colours.text;
  auto words = emoji_text_of(key, emotes);
  if (shortcode && words.spans.size() == 1 && words.spans.front().first == 0 && words.spans.front().last == words.text.size())
    words.spans.front().plain = *shortcode;
  const bool faces = !people.empty() && people.size() <= 3;
  if (!faces) words.text += " " + std::to_string(count);
  auto label = nodes::BasicText<reaction_pictures>(words.text, 13.0f, colour);
  label.setLinks(std::move(words.spans), colour);
  label.setSelectable(false);
  label.setMaxWidth(240.0f);
  auto avatars = std::ranges::to<std::vector>(std::views::transform(
      std::views::enumerate(std::views::take(people, faces ? people.size() : 0)), [&](const auto& item) {
        const auto& [index, person] = item;
        // Overlap faces with one another, never with the reaction label.
        return c::styled({.margin = {0.0f, 0.0f, 0.0f, index == 0 ? 0.0f : -6.0f},
                          .border = scene::Border{own ? colours.accent : colours.tile, 1.5f}},
                         avatar_mark(avatar_of::person{}, person.first, person.second, 20.0f));
      }));
  const auto tint = at_opacity(own ? colours.accent : colours.tile,
      element_opacity_of(looks.bubbles, &config::element_opacity::reactions));
  std::optional<frost_pane> frost;
  if (frosts(looks.bubbles)) {
    frost.emplace(frost_source{}, element_blur_of(looks.bubbles, &config::element_blur::reactions, looks.window));
    frost->apply({.place = scene::anchor::kTopLeft, .fill = true, .cornerRadius = 13.0f});
    frost->setTint(tint);
  }
  return c::onClick(request::react{std::move(message), std::move(key)}, c::column(
      c::vbox(0.0f, {.height = 26.0f, .autoSize = scene::axes::kX, .minWidth = 26.0f,
                    .cornerRadius = 13.0f, .background = frost ? skia::SkColor{0} : tint}),
      std::move(frost), c::row(c::hbox(4.0f, {.height = 26.0f, .autoSize = scene::axes::kX,
                        .padding = {0.0f, 9.0f, 0.0f, 9.0f}}),
        c::styled({.alignSelf = scene::align::kMiddle}, elided(std::move(label))),
        c::visible(faces, c::many(c::hbox(0.0f, {.height = 20.0f, .autoSize = scene::axes::kX,
                                               .alignSelf = scene::align::kMiddle}), std::move(avatars))))));
}
using reaction_chip_t = decltype(reaction_chip(std::declval<const palette&>(), std::declval<const looks_shown&>(),
    std::string{}, std::string{}, std::size_t{}, bool{}, std::declval<const std::vector<emote>&>()));
using reaction_row = nodes::Flow<std::vector<reaction_chip_t>>;
inline auto reaction_row_of() {
  auto row = reaction_row({.direction = nodes::direction::horizontal{}, .spacingX = 4.0f, .spacingY = 4.0f}, {});
  row.apply({.autoSize = scene::axes::kBoth, .maxWidth = 430.0f, .margin = {4.0f, 0.0f, 2.0f, 0.0f}});
  return row;
}
inline auto& reaction_chips(reaction_row& row) { return std::get<0>(row.fChildren); }
inline const auto& reaction_chips(const reaction_row& row) { return std::get<0>(row.fChildren); }

// How far the strip's last line goes, for the message's time beside it: its
// last chip's right edge.
inline float trailing_width(contract::message_trailers, type_tag<feature>, const reaction_row& row) {
  const auto& chips = reaction_chips(row);
  return chips.empty() ? 0.0f : chips.back().bounds().fRight - row.bounds().fLeft;
}

// Its trailer, at a message's foot (mux.ui:message): none where no one has
// reacted.
std::optional<reaction_row> extend(contract::message_trailers, type_tag<feature>, const trailer_facts& facts) {
  const message& said = facts.said;
  if (std::ranges::none_of(said.reactions, [](const auto& one) { return !one.second.empty(); }))
    return std::nullopt;
  auto made = reaction_row_of();
  std::ranges::for_each(said.reactions | std::views::filter([](const auto& one) { return !one.second.empty(); }), [&](const auto& one) {
    const auto& [key, who] = one;
    const auto shortcode = std::ranges::find_if(said.reaction_events, [&](const auto& event) {
      return event.key == key && event.shortcode.has_value();
    });
    reaction_chips(made).push_back(reaction_chip(
        facts.colours, facts.looks, facts.message_id, key, who.size(), who.contains(said.in.account.address), facts.in.emotes,
        who | std::views::transform([&](const std::string& person) { return std::pair{person, sender_name(facts.in, person)}; }) |
            std::ranges::to<std::vector>(),
        shortcode == said.reaction_events.end() ? std::nullopt : shortcode->shortcode));
  });
  return made;
}

// Reactions, in a message's menu: how many, and who, listed.
inline auto extend(contract::message_menu, type_tag<feature>, const palette& colours, const emoji_kept&,
            const menu_facts& facts) {
  using row = row_item<sends<show>>;
  return skiff::compose::visible(
      !facts.id.empty() && (facts.can.react || facts.reaction_count > 0),
      row(colours,
          facts.reaction_count == 0   ? std::string("Reactions")
          : facts.reaction_count == 1 ? std::string("1 reaction")
                                      : std::format("{} reactions", facts.reaction_count),
          sends<show>{show{facts.id}}, icon::people{}, std::nullopt, menu_row_look));
}


// The quick reactions at the top of a message's menu, as Telegram's, and the
// arrow that unrolls every emoji under them.
inline auto quick_reaction(const palette& colours, std::string key) {
  namespace c = skiff::compose;
  const auto request = ::mux::ui::request::menu_react{key};
  return c::onClick(request, c::column(c::justified(
      c::vbox(0.0f, {.width = 36.0f, .height = 32.0f, .cornerRadius = 16.0f,
          .hoverBackground = colours.chosen}), nodes::justify::middle{}),
      c::styled({.alignSelf = scene::align::kMiddle}, nodes::Text(std::move(key), 22.0f, colours.text))));
}
template <class Expand>
auto quick_reactions(const palette& colours, Expand expand) {
  namespace c = skiff::compose;
  const std::array<std::string_view, 6> keys{"👍", "❤️", "😂", "😮", "😢", "🙏"};
  auto each = std::ranges::to<std::vector>(std::views::transform(keys, [&](auto key) {
    return quick_reaction(colours, std::string(key));
  }));
  return c::row(c::hbox(0.0f, {.autoSize = scene::axes::kBoth, .padding = {2.0f, 6.0f, 4.0f, 6.0f}}),
      c::many(c::hbox(0.0f, {.autoSize = scene::axes::kBoth}), std::move(each)),
      c::onPress(std::move(expand), c::styled({.width = 28.0f, .height = 32.0f, .cornerRadius = 14.0f,
          .hoverBackground = colours.chosen}, nodes::Icon(shape_of(icon::down{}), colours.text)), "All emoji"));
}

// What the menu's emoji do: react with it.
struct react_with {
  using Answer = ::mux::ui::request::menu_react;
  // What is typed in its search, a reaction too: Matrix takes any text.
  [[nodiscard]] static constexpr bool takes_text() { return true; }
  Answer operator()(const std::string& text, const std::string& key) const {
    return ::mux::ui::request::menu_react{key, key == text ? std::nullopt : std::optional(text)};
  }
};

// Its head, over a message's menu (mux.ui:context_menu): where the account
// reacts, the quick reactions and the band under them.
template <class Unroll>
auto extend(contract::message_menu_top, type_tag<feature>, const palette& colours, const menu_facts& facts,
            Unroll unroll) {
  namespace c = skiff::compose;
  return c::visible(facts.can.react,
      c::column(c::vbox(0.0f, {.autoSize = scene::axes::kBoth}), quick_reactions(colours, std::move(unroll)),
          c::styled({.fillX = true, .height = 1.0f, .margin = {0.0f, 0.0f, 4.0f, 0.0f}}, nodes::Box<>(colours.band))));
}
// What it unrolls: every emoji, a press reacting with it.
inline auto unrolled(contract::message_menu_top, type_tag<feature>, const palette& colours, emoji_kept& kept) {
  return emoji_panel(colours, kept, react_with{});
}
// Unrolled: its arrow put away.
template <class Head>
void unrolled_head(contract::message_menu_top, type_tag<feature>, Head& head) {
  std::get<1>(std::get<0>(head.fParts).fParts).setVisible(false);
}

}  // namespace mux::plugins::reactions
