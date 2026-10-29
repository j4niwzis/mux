// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.markdown.types -- What the Markdown plugin names, and what it
// says it provides: what is typed read as Markdown (contract::text_markup).
// Light: mux.ui sees it.
export module mux.plugin.markdown.types;

import std;
import mux.core;
import mux.composition;

export namespace mux::plugins::markdown {

struct feature {
  static constexpr std::string_view name = "markdown";
};

consteval bool provides(contract::text_markup, type_tag<feature>) { return true; }

}  // namespace mux::plugins::markdown
