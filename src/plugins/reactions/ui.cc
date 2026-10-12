// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.reactions.ui -- A message's reactions, at the foot of its
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
import splice;
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

// How far the strip's last line goes, for the message's time beside it: its
// last chip's right edge.
inline float trailing_width(contract::message_trailers, type_tag<feature>, const reaction_row& row) {
  const auto& chips = reaction_chips(row);
  return chips.empty() ? 0.0f : chips.back().bounds().fRight - row.bounds().fLeft;
}

// Its trailer, at a message's foot (mux.ui:message): none where no one has
// reacted.
template <class Actions>
std::optional<reaction_row> extend(contract::message_trailers, type_tag<feature>, type_tag<Actions>, const trailer_facts& facts) {
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

// A message's reactions as events, as Matrix has them: a box in the middle,
// a row for each -- the person's avatar and name over what they reacted
// with and when. A press on one answers it: the reaction is an event, and a
// message can reply to it.
// Panel rows use the actual reaction event as their menu target. Inherit its
// nested reactions when known; the parent event is only the reply reference.
[[nodiscard]] inline message reaction_message_of(const conversation& in, const entry& one) {
  message out;
  if (const message* held = held_message(in, one.event)) out = *held;
  out.in = in.id;
  out.id = one.event;
  out.sender = one.who;
  out.at = one.at;
  out.body = reaction_body_of(one.key, in.emotes, one.shortcode);
  out.outgoing = one.mine;
  out.service = false;
  out.reaction = true;
  out.reaction_key = one.key;
  out.replies_to = one.to.empty() ? std::nullopt : std::optional(one.to);
  return out;
}
using reaction_row_answer = std::variant<menu_facts, request::open_url,
    std::tuple<std::optional<request::reply_to>, request::close_shown<shown>>>;

template <class Actions>
auto reaction_event_row(const ui_needs<Actions>& n, const conversation& in, const entry& one,
                        bool first, bool last, const model* known) {
  namespace c = skiff::compose;
  auto bubble = message_bubble<Actions>(spl::remapped<typename message_bubble<Actions>::needs>(n),
                                        in, reaction_message_of(in, one), first, last, known);
  auto row = c::onClick(std::tuple{one.event.empty() ? std::optional<request::reply_to>{}
        : std::optional(request::reply_to{one.event, std::format("{} reacted {}", one.name, one.key)}), request::close_shown<shown>{}},
      c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY,
          .padding = {0.0f, 12.0f, 0.0f, 12.0f}, .hoverBackground = n.colours->chosen}), std::move(bubble)), "Reply to reaction");
  return c::onPointer<reaction_row_answer>([in_id = in.id, known](auto& row, const scene::pointer::down& press,
                                                                scene::PointerReply& reply) -> std::optional<reaction_row_answer> {
    if (press.button != 3) return std::nullopt;
    const auto& bubble = std::get<0>(row.fParts);
    const auto* chat = known ? known->find(in_id) : nullptr;
    auto facts = facts_of_bubble(bubble, chat, press.x, press.y);
    // Legacy cached reactions without an event ID cannot be targeted.
    if (facts.id.empty()) { facts.can.react = false; facts.deletable = false; }
    reply.handle();
    return reaction_row_answer{std::move(facts)};
  }, std::move(row));
}
template <class Actions>
auto panel(const ui_needs<Actions>& n, const shown& facts) {
  namespace c = skiff::compose;
  const auto& chat = chat_or_none(facts.now, facts.in);
  const auto& entries = facts.entries;
  auto rows = std::ranges::to<std::vector>(std::views::transform(std::views::iota(std::size_t{0}, entries.size()),
      [&](std::size_t i) { return reaction_event_row(n, chat, entries[i],
          i == 0 || entries[i - 1].who != entries[i].who,
          i + 1 == entries.size() || entries[i + 1].who != entries[i].who, facts.now); }));
  return c::column(list_dialog(420.0f),
      page_header<sends<back>, sends<request::close_shown<shown>>>(*n.colours, "Reactions", {}, {}, facts.back, true),
      dialog_list(c::many(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), std::move(rows))));
}
template <class Actions>
using box = decltype(panel(std::declval<const ui_needs<Actions>&>(), std::declval<const shown&>()));
template <class Actions>
auto make_content(std::type_identity<box<Actions>>, const ui_needs<Actions>& n, const shown& facts) {
  return panel(n, facts);
}
template <class Content>
inline dialog_look content_look(std::type_identity<Content>, std::type_identity<shown>) {
  return {.sheet = sheet::chat{}, .size = dialog_size::fixed{392.0f, 420.0f}};
}


// Its dialog, among the window's (mux.ui:window).
template <class Actions>
constexpr type_tag<box<Actions>> extend(contract::window_dialogs, type_tag<feature>, type_tag<Actions>) { return {}; }

// Reactions, in a message's menu: how many, and who, listed.
template <class Actions>
auto extend(contract::message_menu, type_tag<feature>, type_tag<Actions>, const palette& colours, const emoji_kept&,
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

}  // namespace mux::plugins::reactions
