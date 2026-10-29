// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.receipts.types -- What the receipts plugin names, and what it
// says it provides: a footer under a message's row. Light: mux.ui sees it.
export module mux.plugin.receipts.types;

import std;
import mux.core;
import mux.composition;

export namespace mux::plugins::receipts {

struct feature {
  static constexpr std::string_view name = "receipts";
};

consteval bool provides(contract::message_footers, type_tag<feature>) { return true; }

}  // namespace mux::plugins::receipts
