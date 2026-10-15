// SPDX-License-Identifier: AGPL-3.0-only
// mux.proto.xmpp.changes -- What an XMPP account says that no other protocol
// does: what the server asks to make the account (XEP-0077) -- its form,
// with a captcha of any kind in it (XEP-0158), or the page to make it on --
// and that it is made. Its change list (changes_type(const state&), by ADL)
// is made part of mux.core's change_t.
export module mux.proto.xmpp.changes;

import std;
import splice;
import mux.core.ids;
import mux.proto.xmpp.kept;

export namespace mux::proto::xmpp {

// How a field of the server's form is shown: typed in, typed in unseen, not
// shown and sent back as it came, or only read.
namespace field_shown {
struct typed {
  friend bool operator==(typed, typed) = default;
};
struct masked {
  friend bool operator==(masked, masked) = default;
};
struct hidden {
  friend bool operator==(hidden, hidden) = default;
};
struct read {
  friend bool operator==(read, read) = default;
};
}  // namespace field_shown
using field_shown_t = spl::variant<field_shown::typed, field_shown::masked, field_shown::hidden, field_shown::read>;

// A field the server asks: what it is called and says, how it is shown, what
// it holds already, and what goes with it -- a picture (a captcha's, sent
// with the form), the addresses of others, the choices it has.
struct registration_field {
  std::string var;
  std::string label;
  std::string desc;
  field_shown_t shown;
  std::vector<std::string> value;
  bool required = false;
  std::vector<std::uint8_t> picture;
  std::vector<std::string> links;
  std::vector<std::string> choices;
  friend bool operator==(const registration_field&, const registration_field&) = default;
};
// What the server asks beyond the address and the password: answered in
// the account's form, and the account connects again with the answers. Or
// only a page of its own to make the account on.
struct registration_asked {
  account_id account;
  std::string instructions;
  std::vector<registration_field> fields;
  std::optional<std::string> page;
  friend bool operator==(const registration_asked&, const registration_asked&) = default;
};
// What XMPP keeps beside an account form's draft: what the server asked to
// register, and the answers to it -- typed, or a hidden field's as it came.
struct registration_form {
  registration_asked asked;
  std::vector<registration_answer> answers;
  friend bool operator==(const registration_form&, const registration_form&) = default;
};
// The answers to what the server asked, as it asks: what each field holds
// first; none for one only read.
[[nodiscard]] inline std::vector<registration_answer> first_answers(const registration_asked& asked) {
  return std::ranges::to<std::vector>(std::views::transform(std::views::filter(asked.fields, [](const registration_field& one) {
           return spl::visit(spl::overloaded{[](field_shown::read) { return false; }, [](const auto&) { return true; }}, one.shown);
         }), [](const registration_field& one) {
           return registration_answer{.var = one.var, .value = one.value.empty() ? std::string() : one.value.front()};
         }));
}
// The account made: from now on, signed in to.
struct registered {
  account_id account;
};

struct muc_settings_loaded { account_id account; std::string room; muc_room settings; };
inline std::string describe(const muc_settings_loaded& one) { return one.room + ": room settings received"; }
struct muc_configuration_failed { account_id account; std::string room, reason; };
inline std::string describe(const muc_configuration_failed& one) { return one.room + ": " + one.reason; }
using changes = change_list<registration_asked, registered, muc_settings_loaded, muc_configuration_failed>;
constexpr type_tag<changes> changes_type(const state&) { return {}; }

// As the command line says them.
inline std::string describe(const registration_asked& one) {
  return one.account.address + ": the server asks " + std::to_string(one.fields.size()) + " field" +
         (one.fields.size() == 1 ? "" : "s") + " to register" + (one.page ? ", on " + *one.page : std::string());
}
inline std::string describe(const registered& one) { return one.account.address + " was registered"; }

}  // namespace mux::proto::xmpp
