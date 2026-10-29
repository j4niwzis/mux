// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.encryption.types -- What the encryption plugin names, and what it
// says it provides: the Matrix account, made with end-to-end encryption.
// Light: mux.ui sees it.
export module mux.plugin.encryption.types;

import std;
import mux.core;
import mux.composition;

export namespace mux::plugins::encryption {

struct feature {
  static constexpr std::string_view name = "encryption";
};

consteval bool provides(contract::matrix_account, type_tag<feature>) { return true; }

}  // namespace mux::plugins::encryption
