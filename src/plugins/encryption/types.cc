// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.encryption.types -- What the encryption plugin names, and what it
// says it provides: the Matrix account, made with end-to-end encryption;
// the emoji verification's dialog; and a part of the program.
// Light: mux.ui sees it.
export module mux.plugin.encryption.types;

import std;
import mux.core;
import mux.composition;
import mux.proto;

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
// Asked from its page, for the account whose pages are open: cross-signing
// set up, or brought back with the recovery key; the identity reset;
// unverified sessions signed out; the key backup made anew or deleted; room
// keys written to a key file or read from one.
struct setup_cross_signing {};
struct restore_cross_signing {};
struct reset_identity {};
struct sign_out_unverified {};
struct reset_backup {};
struct delete_backup {};
struct export_room_keys {};
struct import_room_keys {};

// What it asks a passphrase, or the account's password, for: each for its
// account (contract::passphrases).
namespace passphrase {
// Its room keys: written to a file under a new passphrase, or read from one
// under its own.
struct export_keys {
  account_id by;
  friend bool operator==(const export_keys&, const export_keys&) = default;
};
struct import_keys {
  account_id by;
  friend bool operator==(const import_keys&, const import_keys&) = default;
};
// Cross-signing set up: the account's password, which the server asks for.
struct cross_signing {
  account_id by;
  friend bool operator==(const cross_signing&, const cross_signing&) = default;
};
// Its identity reset (Element's "Reset identity"): new cross-signing keys
// in place of the old, the password asked as for a setting up.
struct reset_identity {
  account_id by;
  friend bool operator==(const reset_identity&, const reset_identity&) = default;
};
// One's own sessions not verified, signed out: the password, as the server
// asks for it.
struct sign_out_unverified {
  account_id by;
  friend bool operator==(const sign_out_unverified&, const sign_out_unverified&) = default;
};
// Cross-signing taken back with the recovery key.
struct recovery {
  account_id by;
  friend bool operator==(const recovery&, const recovery&) = default;
};
constexpr proto::passphrase_words passphrase_text(const export_keys&) {
  return {"Export room keys",
          "This account's room keys are written to your Downloads folder, sealed under a new passphrase: with "
          "them and it, any client reads every encrypted message they open. Keep both safe.",
          "Export", false, true};
}
constexpr proto::passphrase_words passphrase_text(const import_keys&) {
  return {"Import room keys", "Room keys from a key file Element or mux wrote, under its passphrase.", "Import", true, false, true};
}
constexpr proto::passphrase_words passphrase_text(const cross_signing&) {
  return {"Set up cross-signing",
          "Three keys are made for your account and kept on this device: with them it signs your devices and "
          "the people you verify. Your server asks for your account's password to take them.",
          "Set up", true, false};
}
constexpr proto::passphrase_words passphrase_text(const reset_identity&) {
  return {"Reset your identity",
          "New cross-signing keys replace your account's: everyone who verified you, and every session of yours, "
          "must verify again, and messages only your old keys could read stay unreadable here. Only if you have "
          "lost every verified session and the recovery key. Your server asks for your account's password.",
          "Reset", true, false};
}
constexpr proto::passphrase_words passphrase_text(const sign_out_unverified&) {
  return {"Sign out unverified sessions",
          "Every session of yours that is not verified -- not cross-signed, nor verified by emoji here -- is "
          "signed out. Your server asks for your account's password.",
          "Sign out", true, false};
}
constexpr proto::passphrase_words passphrase_text(const recovery&) {
  return {"Restore with the recovery key",
          "The recovery key written down when cross-signing was set up: with it, this device takes your "
          "cross-signing keys back from your server.",
          "Restore", true, false};
}
}  // namespace passphrase
constexpr proto::passphrase_list<passphrase::export_keys, passphrase::import_keys, passphrase::cross_signing,
                                 passphrase::reset_identity, passphrase::sign_out_unverified, passphrase::recovery>
passphrases(contract::passphrases, type_tag<feature>) {
  return {};
}
consteval bool provides(contract::passphrases, type_tag<feature>) { return true; }

// Its page of a Matrix account's: Encryption -- device verification, the key
// backup, room keys.
struct security_page {};
consteval bool provides(contract::account_pages, type_tag<feature>) { return true; }
constexpr composition::page_list<security_page> account_pages(contract::account_pages, type_tag<feature>) { return {}; }

}  // namespace mux::plugins::encryption
