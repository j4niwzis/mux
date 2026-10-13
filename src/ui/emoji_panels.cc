// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:emoji_panels -- The emoji panel, and what pickers share.
export module mux.ui.base:emoji_panels;

import std;
import mux.logic.text;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.compose;
import skiff.nodes.box;
import skiff.nodes.flow;
import skiff.nodes.image;
import skiff.nodes.scroll;
import skiff.nodes.text;
import skiff.widgets.textbox;
import skiff.widgets.model;
import skiff.model;
import mux.core;
import mux.config;
import mux.logic.emoji;
import mux.logic.reactions;
import :base;
import :icons;
import :avatars;
import :controls;
import :themes;
import :names;
import :forms;
import :emoji_kept;

export namespace mux::ui {
// A picker's layout, as tdesktop's emoji and sticker panels share it: the
// search field at the top, the list scrolling under it, the tabs' footer at
// the bottom.
[[nodiscard]] inline skiff::compose::Look picker_look() {
  return skiff::compose::vbox(4.0f, {.padding = {7.0f, 0.0f, 4.0f, 7.0f}});
}
// Group by source and state key, not the displayed title. Old cached and
// protocol-neutral packs without a source retain their named grouping.
inline auto emote_pack_identity(const emote& one) {
  return std::tuple{one.pack_room, one.pack_key, one.pack_room || !one.pack_key.empty() ? std::string() : one.pack};
}
inline auto grouped_emotes(const std::vector<emote>& all, std::string fallback) {
  return std::ranges::to<std::vector>(std::views::transform(
      std::views::filter(std::views::iota(std::size_t{0}, all.size()), [&](std::size_t i) {
        return std::ranges::find(all, emote_pack_identity(all[i]), emote_pack_identity) == all.begin() + static_cast<std::ptrdiff_t>(i);
      }), [&](std::size_t i) {
        return std::pair{all[i].pack.empty() ? fallback : all[i].pack,
            std::ranges::to<std::vector>(std::views::filter(all, [&](const auto& one) { return emote_pack_identity(one) == emote_pack_identity(all[i]); }))};
      }));
}
inline std::optional<emote_pack> source_pack(const std::vector<emote>& images, std::string name) {
  if (images.empty() || !images.front().pack_room || !std::ranges::all_of(images, [&](const auto& one) {
        return emote_pack_identity(one) == emote_pack_identity(images.front());
      }))
    return std::nullopt;
  return emote_pack{.chat = images.front().pack_room, .key = images.front().pack_key, .name = std::move(name)};
}
// What the emoji panel keeps of its own: what is typed in its search -- the
// pictures pasted into it with the text -- and the emoji whose tones are
// shown, where one's are.
struct emoji_search {
  std::string text;
  std::vector<scene::ClipboardAtom> atoms;
};
// An emoji's tones shown: which, and where in the window -- over its cell.
struct tones_shown {
  const alef::emoji* base = nullptr;
  skia::SkPoint at{};
  friend bool operator==(const tones_shown&, const tones_shown&) = default;
};
struct emoji_draft {
  emoji_search query;
  std::optional<tones_shown> tones;
};
// Asked of the panel by its parts: an emoji's tones shown, put away; what
// is typed, picked as a reaction; a tab pressed while a search is shown,
// the search left.
struct show_tones {
  tones_shown shown;
};
struct close_tones {};
struct pick_typed {};
struct leave_emoji_search {};

// What is typed, as a reaction: its custom emoji -- the chat's, the
// favourites, those pasted in -- read as theirs, the rest as it is.
inline logic::reaction_text typed_reaction(const emoji_kept& kept, const emoji_search& query) {
  std::vector<emote> available = kept.custom_emoji ? kept.chat_emotes.get() : std::vector<emote>{};
  // The favourites not among them, gathered first: the filter reads what
  // is there, which must not grow under it.
  auto favourites = kept.favourite_emoji |
                    std::views::take(kept.custom_emoji ? kept.favourite_emoji.size() : std::size_t{0}) |
                    std::views::filter([&](const emote& one) {
                      return std::ranges::none_of(available, [&](const emote& other) { return other.url == one.url; });
                    }) |
                    std::ranges::to<std::vector>();
  available.append_range(std::move(favourites));
  // Rich clipboard atoms keep the original source even where an emoji of
  // this room has the same name.
  const auto pictures = kept.custom_emoji ? query.atoms | std::views::filter(&scene::ClipboardAtom::picture) | std::ranges::to<std::vector>()
                                          : std::vector<scene::ClipboardAtom>{};
  const auto images = pictures | std::views::transform([](const scene::ClipboardAtom& atom) {
                        return logic::reaction_image{atom.first, atom.last, atom.target, atom.plain};
                      }) |
                      std::ranges::to<std::vector>();
  available.append_range(pictures | std::views::filter([&](const scene::ClipboardAtom& atom) {
                           return std::ranges::none_of(available, [&](const emote& one) { return one.url == atom.target; });
                         }) |
                         std::views::transform([](const scene::ClipboardAtom& atom) {
                           std::string code = atom.plain;
                           if (code.starts_with(':') && code.ends_with(':') && code.size() > 2)
                             code = code.substr(1, code.size() - 2);
                           return emote{.shortcode = std::move(code), .url = atom.target, .body = atom.plain};
                         }));
  auto resolved = logic::reaction_text_of(query.text, available, images);
  const auto trimmed = [](std::string text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    return first == std::string::npos ? std::string() : text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
  };
  resolved.key = trimmed(std::move(resolved.key));
  resolved.label = trimmed(std::move(resolved.label));
  return resolved;
}

// The panel's own handlers: tones shown and put away; what is typed picked.
template <class Pick> struct emoji_events {
  Pick pick;
  const emoji_kept* kept = nullptr;
  auto on(show_tones one) const {
    return skiff::model::over<skiff::model::Field<&emoji_draft::tones>>(skiff::model::setTo(std::optional(one.shown)));
  }
  auto on(close_tones) const {
    return skiff::model::over<skiff::model::Field<&emoji_draft::tones>>(skiff::model::setTo(std::optional<tones_shown>()));
  }
  auto on(leave_emoji_search) const {
    return skiff::model::over<skiff::model::Field<&emoji_draft::query>>(skiff::model::setTo(emoji_search{}));
  }
  auto on(pick_typed, const emoji_draft& now) const {
    const auto typed = typed_reaction(*kept, now.query);
    return skiff::model::Up{pick(typed.label, typed.key)};
  }
};

// One emoji: its glyph, or a custom one's picture; where it is one of the
// table's, its tones found through it.
struct emoji_item {
  std::string glyph;
  const alef::emoji* source = nullptr;
  std::optional<std::string> picture;
};
inline emoji_item item_of(const alef::emoji* one) { return {logic::emoji_text(*one), one, std::nullopt}; }
inline emoji_item item_of(const std::string& one) { return {one, nullptr, std::nullopt}; }
inline emoji_item item_of(const emote& one) { return {":" + one.shortcode + ":", nullptr, one.url}; }
// A group of them, as the panel lists it: its name, its emoji, its tab -- a
// picture or a mark -- and the pack it is, where it is one.
struct emoji_group {
  std::string name;
  std::vector<emoji_item> items;
  std::optional<std::string> picture;
  std::string mark;
  std::optional<emote_pack> pack;
};
inline auto emoji_items(const auto& all) { return all | std::views::transform([](const auto& one) { return item_of(one); }) | std::ranges::to<std::vector>(); }

// The groups shown: what was picked lately first, then its default list
// (lib_ui's GetDefaultRecent) up to the section's number, the favourites,
// each custom pack, then every group of the table, as tdesktop's. Searched:
// what is found, and the custom emoji whose names have what is typed.
inline std::vector<emoji_group> emoji_groups_of(const emoji_kept& kept, const emoji_search& query) {
  const std::vector<emote> chat = kept.custom_emoji ? kept.chat_emotes.get() : std::vector<emote>{};
  if (!query.text.empty()) {
    std::vector<emoji_group> out{{"Search results", emoji_items(logic::emoji_found(query.text)), std::nullopt, "\u23F2", std::nullopt}};
    const auto typed = typed_reaction(kept, query);
    std::string_view label = typed.label;
    if (label.starts_with(':')) label.remove_prefix(1);
    if (label.ends_with(':')) label.remove_suffix(1);
    const std::string wanted = logic::folded(label);
    auto available = chat;
    available.append_range(kept.favourite_emoji |
                           std::views::take(kept.custom_emoji ? kept.favourite_emoji.size() : std::size_t{0}) |
                           std::views::filter([&](const emote& one) {
                             return std::ranges::none_of(chat, [&](const emote& other) { return other.url == one.url; });
                           }));
    const auto custom = available | std::views::filter([&](const emote& one) {
                          return logic::folded(one.shortcode).contains(wanted) || logic::folded(one.body).contains(wanted) ||
                                 logic::folded(one.pack).contains(wanted);
                        }) |
                        std::ranges::to<std::vector>();
    out.append_range(grouped_emotes(custom, "Custom emoji") | std::views::transform([](const auto& pack) {
                       return emoji_group{pack.first, emoji_items(pack.second), std::nullopt, "\u23F2", source_pack(pack.second, pack.first)};
                     }));
    return out;
  }
  static constexpr std::array<std::string_view, 34> kDefaultRecent{
      "😂", "😘", "❤️", "😍", "😊", "😁", "👍", "☺️", "😔", "😄", "😭", "💋", "😒", "😳", "😜", "🙈", "😉",
      "😃", "😢", "😝", "😱", "😡", "😏", "😞", "😅", "😚", "🙊", "😌", "😀", "😋", "😆", "👌", "😐", "😕"};
  std::vector<std::string> recent = kept.recent_emoji;
  recent.append_range(kDefaultRecent | std::views::transform([](std::string_view one) { return std::string(one); }) |
                      std::views::filter([&](const std::string& one) { return !std::ranges::contains(kept.recent_emoji, one); }));
  recent.resize(std::min<std::size_t>(recent.size(), std::max<std::size_t>(42, kept.recent_emoji.size())));
  std::vector<emoji_group> out{{"Recently used", emoji_items(recent), std::nullopt, "\u23F2", std::nullopt}};
  if (kept.custom_emoji && !kept.favourite_emoji.empty())
    out.push_back({"Favorites", emoji_items(kept.favourite_emoji), kept.favourite_emoji.front().url, "\u2605",
                   source_pack(kept.favourite_emoji, "Favorites")});
  out.append_range(grouped_emotes(chat, "Custom emoji") | std::views::transform([](const auto& pack) {
                     const emote& first = pack.second.front();
                     return emoji_group{pack.first, emoji_items(pack.second), first.pack_avatar.value_or(first.url), "\u23F2",
                                        source_pack(pack.second, pack.first)};
                   }));
  out.append_range(std::views::iota(std::size_t{0}, logic::emoji_group_count()) | std::views::transform([](std::size_t g) {
                     return emoji_group{std::string(logic::emoji_group_name(g)), emoji_items(logic::emoji_of_group(g)), std::nullopt,
                                        logic::emoji_text(logic::emoji_group_face(g)), std::nullopt};
                   }));
  return out;
}

inline constexpr float kEmojiCell = 37.0f;
// One emoji of the list: pressed, picked -- a glyph remembered among the
// recent -- and any tones shown put away; the other button, its tones shown,
// where it has any; the pointer resting on it, it shown large.
template <class Pick>
auto emoji_cell(const palette& colours, emoji_kept& kept, const Pick& pick, const emoji_item& one) {
  namespace c = skiff::compose;
  std::optional<nodes::Image<from_emotes>> picture;
  if (one.picture) {
    picture.emplace(from_emotes{*one.picture});
    picture->apply({.width = 26.0f, .height = 26.0f, .alignSelf = scene::align::kMiddle});
    picture->keepBox();  // fixed: its coming repaints, lays nothing out
  }
  const previewed shown = one.picture ? previewed{*one.picture, one.glyph, true} : previewed{one.glyph, std::string(), false};
  auto face = c::row(c::justified(c::hbox(0.0f, {.width = kEmojiCell, .height = kEmojiCell, .cornerRadius = 6.0f,
                                                 .hoverBackground = colours.chosen}),
                                  nodes::justify::middle{}),
      std::move(picture),
      c::visible(!one.picture, c::styled({.alignSelf = scene::align::kMiddle}, nodes::Text(one.glyph, 22.0f, colours.text))));
  auto picked = c::onPress([pick, at = &kept, glyph = one.glyph, key = one.picture.value_or(one.glyph), remember = !one.picture] {
    if (remember)
      at->remember_emoji(glyph);
    return std::tuple{pick(glyph, key), close_tones{}};
  }, std::move(face), one.glyph);
  // Its tones, over it as tdesktop puts them: a strip whose first cell is
  // over it, a cell and a gap above it, kept inside the panel.
  auto toned = c::onPointer<show_tones>([source = one.source](auto& cell, const scene::pointer::down& press, scene::PointerReply& reply)
                                            -> std::optional<show_tones> {
    if (press.button != 3 || source == nullptr || logic::tones_of(*source).empty())
      return std::nullopt;
    reply.handle();
    const auto shift = skiff::scene::work::drawnOffset(cell.fState.fId);
    const skia::SkRect drawn = cell.bounds().makeOffset(shift.x, shift.y);
    return show_tones{{source, {drawn.fLeft - 4.0f, drawn.fTop - kEmojiCell - 12.0f}}};
  }, std::move(picked));
  return c::resting(kPreviewAfterMs, [at = &kept, shown](bool on) {
    if (on)
      at->previewed_now = shown;
    else if (at->previewed_now == shown)
      at->previewed_now.reset();
  }, std::move(toned));
}
// A group's tab: its picture, or its mark; lit while its group is at the top.
inline auto emoji_tab(const palette& colours, const emoji_group& one) {
  namespace c = skiff::compose;
  std::optional<nodes::Image<from_emotes>> image;
  if (one.picture) {
    image.emplace(from_emotes{*one.picture});
    image->apply({.width = 24.0f, .height = 24.0f, .alignSelf = scene::align::kMiddle});
    image->keepBox();
  }
  return c::row(c::justified(c::hbox(0.0f, {.width = 30.0f, .height = 30.0f, .minWidth = 24.0f, .alignSelf = scene::align::kMiddle,
                                            .cornerRadius = 6.0f, .hoverBackground = colours.chosen, .selectedBackground = colours.tile}),
                             nodes::justify::middle{}),
      std::move(image), c::visible(!one.picture, c::styled({.alignSelf = scene::align::kMiddle}, nodes::Text(one.mark, 16.0f, colours.text))));
}

// Every emoji, as tdesktop's panel lists them (chat_helpers.style): a search
// at its top; an emoji's tones under it, while they are asked for; where the
// panel picks reactions, what is typed, as one; the groups one under another
// in one list that scrolls, each a semibold header over its emoji, 37
// across; and a strip of the groups' tabs that brings each into view and is
// lit for the one in view. A press on an emoji gives it to Pick: a reaction,
// from a message's menu; text in the input, from the input's own button.
// Functions and combinators: its state a Local, its groups projected from it.
template <class Pick>
auto emoji_panel(const palette& colours, emoji_kept& kept, Pick pick) {
  namespace c = skiff::compose;
  using query = skiff::model::Field<&emoji_draft::query>;
  auto field = widgets::PicturedTextField<emoji_search>(colours.widgets, "Search emoji");
  field.setSearchIcon(true);
  // The footer, as tdesktop's: the categories in a row of their own, 36
  // high, each tab sharing it; under it the packs -- the favourites, each
  // custom pack -- a page of them at a time between arrows.
  const auto arrow = [&colours](bool next) {
    return c::styled({.width = 30.0f, .height = 30.0f, .alignSelf = scene::align::kMiddle, .cornerRadius = 6.0f,
                      .hoverBackground = colours.chosen},
                     nodes::Text(next ? "›" : "‹", 20.0f, colours.text));
  };
  auto footer = c::split<emoji_group>(c::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      [](const emoji_group& one) { return one.picture.has_value(); },
      c::in_row<emoji_group>(c::hbox(0.0f, {.fillX = true, .height = 36.0f})),
      c::paged<emoji_group>(c::hbox(0.0f, {.fillX = true, .height = 30.0f}),
                            c::justified(c::hbox(0.0f, {.height = 30.0f, .grow = scene::axes::kX}), nodes::justify::middle{}), 30.0f, 60.0f,
                            arrow, "Previous emoji packs", "Next emoji packs"));
  auto groups = c::sections<emoji_group>(c::vbox(4.0f, {.fill = true}), c::vbox(0.0f),
      [&colours](const emoji_group& one) {
        auto made = emoji_tab(colours, one);
        // A category's tab shares its row with the others'.
        if (!one.picture)
          made.apply({.grow = scene::axes::kX, .shrink = scene::axes::kX, .minWidth = 0.0f});
        return made;
      },
      [&colours, &kept, pick](const emoji_group& one) {
        return cell_section(colours, one.name,
                            one.items | std::views::transform([&](const emoji_item& each) { return emoji_cell(colours, kept, pick, each); }) |
                                std::ranges::to<std::vector>(),
                            one.pack, kept.pack_account);
      },
      footer, [](std::size_t) { return leave_emoji_search{}; });
  using tones_field = skiff::model::Field<&emoji_draft::tones>;
  auto tones = c::placed_at<tones_field>([](const std::optional<tones_shown>& now) { return now.transform([](const tones_shown& one) { return one.at; }); },
      c::projected<tones_field>([](const std::optional<tones_shown>& now) {
        return !now ? std::vector<emoji_item>{}
                    : std::views::concat(std::views::single(now->base), logic::tones_of(*now->base)) |
                          std::views::transform([](const alef::emoji* one) { return item_of(one); }) | std::ranges::to<std::vector>();
      }, c::listed<emoji_item>(c::hbox(0.0f, {.autoSize = scene::axes::kBoth, .padding = {4.0f, 4.0f, 4.0f, 4.0f}, .cornerRadius = 8.0f,
                                              .background = colours.sidebar, .border = scene::Border{colours.band, 1.0f},
                                              .shadow = scene::Shadow{skia::colorSetARGB(70, 0, 0, 0), 3.0f}}),
                               [&colours, &kept, pick](const emoji_item& one) { return emoji_cell(colours, kept, pick, one); })));
  auto typed = c::shown_for<query>([at = &kept](const emoji_search& now) { return Pick::takes_text() && !typed_reaction(*at, now).key.empty(); },
      c::onClick(pick_typed{}, c::row(c::hbox(0.0f, {.fillX = true, .height = 34.0f, .margin = {4.0f, 7.0f, 2.0f, 0.0f},
                                                     .padding = {0.0f, 12.0f, 0.0f, 12.0f}, .cornerRadius = 17.0f,
                                                     .background = colours.tile, .hoverBackground = colours.chosen}),
          c::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle},
              c::text_for<query>([at = &kept](const emoji_search& now) { return std::format("React with \u201C{}\u201D", typed_reaction(*at, now).label); },
                                 elided(nodes::Text("", 13.0f, colours.text))))), "React with search text"));
  return with_preview(colours, kept,
      c::local<emoji_draft>(emoji_events<Pick>{pick, &kept},
          c::column(picker_look(),
              c::styled({.fillX = true, .height = 32.0f, .margin = {0.0f, 7.0f, 0.0f, 0.0f}}, c::bound<query>(std::move(field))),
              std::move(typed),
              c::styled({.fillX = true, .grow = scene::axes::kY},
                  c::projected<query>([at = &kept](const emoji_search& now) {
                    // A search's results, every group's tab still there, none lit.
                    return c::SectionsShown<emoji_group>{emoji_groups_of(*at, now), emoji_groups_of(*at, emoji_search{}), now.text.empty()};
                  }, std::move(groups))),
              // Over the rest: an emoji's tones, while they are asked for.
              std::move(tones))));
}
template <class Pick>
using emoji_panel_t = decltype(emoji_panel(std::declval<const palette&>(), std::declval<emoji_kept&>(), std::declval<Pick>()));

}  // namespace mux::ui
