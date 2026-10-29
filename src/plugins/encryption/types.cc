// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.encryption.types -- What the encryption plugin names, and what it
// says it provides: the Matrix account, made with end-to-end encryption;
// the emoji verification's dialog; and a part of the program.
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

// An emoji verification, as Element shows it: with whom, and where it is --
// asked of you (Accept, Decline), waiting on them (Cancel), the 7 emoji to
// compare with what their screen shows (They match, They don't match), done,
// or stopped and why (OK).
struct verification_view {
  std::string user;
  std::string device;
  verification_step_t step;
};
// Its answers, asked of the program by the dialog; it is closed by
// ui::request::close_shown<verification_view>, which its part answers as
// its step allows.
struct verify_accept_now {};
struct verify_cancel_now {};
struct verify_match {};
struct verify_mismatch {};

consteval bool provides(contract::window_dialogs, type_tag<feature>) { return true; }
constexpr type_tag<verification_view> facts_type(contract::window_dialogs, type_tag<feature>) { return {}; }
consteval bool provides(contract::program_parts, type_tag<feature>) { return true; }
// Its page of a Matrix account's: Encryption -- device verification, the key
// backup, room keys.
struct security_page {};
consteval bool provides(contract::account_pages, type_tag<feature>) { return true; }
constexpr composition::page_list<security_page> account_pages(contract::account_pages, type_tag<feature>) { return {}; }

}  // namespace mux::plugins::encryption
