// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.stickers.types -- What the stickers plugin names, and what it
// says it provides: a page of the media panel, a row of a message's menu,
// and a part of the program. Light: mux.ui sees it.
export module mux.plugin.stickers.types;

import std;
import mux.core;
import mux.composition;

export namespace mux::plugins::stickers {

struct feature {
  static constexpr std::string_view name = "stickers";
};

// Its page of the media panel. What it shows is the chat's packs and one's
// own, kept with the emoji (emoji_kept): nothing of its own in the facts.
struct page {
  friend bool operator==(page, page) = default;
};
struct shown {
  friend bool operator==(shown, shown) = default;
};
// Asked of the program: a sticker made a favourite, or no longer one.
struct favourite {
  emote sticker;
};

consteval bool provides(contract::media_pages, type_tag<feature>) { return true; }
constexpr type_tag<page> page_type(contract::media_pages, type_tag<feature>) { return {}; }
constexpr type_tag<shown> shown_type(contract::media_pages, type_tag<feature>) { return {}; }
constexpr std::string_view tab_name(page) { return "Stickers"; }
// Offered where the account sends stickers.
constexpr bool shown_where(page, const auto& facts) { return facts.stickers; }

consteval bool provides(contract::message_menu, type_tag<feature>) { return true; }
consteval bool provides(contract::program_parts, type_tag<feature>) { return true; }

}  // namespace mux::plugins::stickers
