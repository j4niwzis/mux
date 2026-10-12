// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.gifs.types -- What the GIFs plugin names, and what it says it
// provides: a page of the media panel, and a part of the program. Light:
// mux.ui sees it, to know the page and what is shown on it.
export module mux.plugin.gifs.types;

import std;
import mux.composition;

export namespace mux::plugins::gifs {

struct feature {
  static constexpr std::string_view name = "GIFs";
};

// Its page of the media panel, and what is shown on it: the GIFs saved,
// newest first, by where each is kept.
struct page {
  friend bool operator==(page, page) = default;
};
struct shown {
  std::vector<std::string> paths;
  friend bool operator==(const shown&, const shown&) = default;
};
// Asked of the program: the saved GIFs, as its page is opened.
struct show {};

consteval bool provides(contract::media_pages, type_tag<feature>) { return true; }
constexpr type_tag<page> page_type(contract::media_pages, type_tag<feature>) { return {}; }
constexpr type_tag<shown> shown_type(contract::media_pages, type_tag<feature>) { return {}; }
constexpr std::string_view tab_name(page) { return "GIFs"; }
// Offered where files may be sent in the chat.
constexpr bool shown_where(page, const auto& facts) { return facts.files; }
constexpr show opened(page) { return {}; }

consteval bool provides(contract::program_parts, type_tag<feature>) { return true; }

}  // namespace mux::plugins::gifs
