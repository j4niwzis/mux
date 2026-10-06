// SPDX-License-Identifier: AGPL-3.0-only
// mux.logic.text: text as the program reads it, with nothing else --
// folded for finding in any case, and an address's account.
export module mux.logic.text;

import std;
import alef.utf;
import alef.casing;
import mux.core;
import mux.config;
import mux.protocols;

export namespace mux::logic {

// A text with its case folded, for finding words in any case: Unicode's
// full case folding (alef).
inline constexpr auto folded = [](std::string_view text) {
  return std::ranges::to<std::string>(std::views::transform(text | alef::as_folded | alef::as_utf8, [](char8_t unit) { return std::bit_cast<char>(unit); }));
};

// The protocol an address speaks -- the one that owns it, asked of each --
// and the account it names.
[[nodiscard]] inline protocol_t protocol_of(std::string_view address) { return proto::protocol_of(address); }
[[nodiscard]] inline account_id account_of(std::string_view address) {
  return account_id{protocol_of(address), std::string(address)};
}

}  // namespace mux::logic
