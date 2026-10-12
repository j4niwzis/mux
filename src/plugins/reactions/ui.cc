// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.reactions.ui -- A message's reactions, at the foot of its
// bubble, as Telegram's: a chip for each -- its emoji or picture, how many,
// or the faces of up to three who chose it -- tinted where one's own is
// among them; pressed, one reacts with it too.
export module mux.plugin.reactions.ui;

import std;
import skia;
import skiff.scene;
import skiff.compose;
import skiff.nodes.flow;
import skiff.nodes.text;
import mux.core;
import mux.config;
import mux.ui;
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

// The strip, as a trailer: how far its last line goes is its last chip's
// right edge, for the message's time beside it.
struct strip : reaction_row {
  explicit strip(reaction_row row) : reaction_row(std::move(row)) {}
  [[nodiscard]] float trailing_width() const {
    const auto& chips = reaction_chips(*this);
    return chips.empty() ? 0.0f : chips.back().bounds().fRight - this->bounds().fLeft;
  }
};

// Its trailer, at a message's foot (mux.ui:message): none where no one has
// reacted.
template <class Actions>
std::optional<strip> extend(contract::message_trailers, type_tag<feature>, type_tag<Actions>, const trailer_facts& facts) {
  const message& said = facts.said;
  if (std::ranges::none_of(said.reactions, [](const auto& one) { return !one.second.empty(); }))
    return std::nullopt;
  strip made(reaction_row_of());
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

}  // namespace mux::plugins::reactions
