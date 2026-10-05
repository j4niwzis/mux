// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui.proto.matrix:form -- Matrix's account form.
export module mux.ui.proto.matrix:form;

import std;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes.flow;
import skiff.nodes.text;
import skiff.nodes.scroll;
import skiff.widgets.button;
import skiff.widgets.motion;
import skiff.widgets.sliderbar;
import skiff.widgets.textarea;
import skiff.widgets.textbox;
import mux.core;
import mux.config;
import mux.proto.kept;
import mux.proto.matrix;
import mux.proto.matrix.requests;
import mux.ui;

namespace mux::proto::matrix::form_detail {
using namespace ::mux::ui;

// A Matrix account's settings: its user ID and password, the homeserver
// (found through the server's .well-known when left empty) and what this
// device is called.
template <class Actions>
struct matrix_form : nodes::Stack {
  // What the add-account pane says of the protocol.
  static constexpr std::string_view note = "A user ID like @user:example.org, on a homeserver such as Synapse.";
  Actions* actions = nullptr;
  std::optional<std::string> editing;
  // A new account, to be registered on the server first; and the user's
  // word that they agree to its terms, where it has some.
  bool creating = false;
  bool agrees = false;
  // Signed in in the browser, on the server's own page (OAuth 2.0), rather
  // than with a password typed here.
  bool in_browser = false;
  struct pick_way {
    matrix_form* form;
    void operator()(std::size_t index) const { form->set_in_browser(index == 1); }
  };
  struct pick_mode {
    matrix_form* form;
    void operator()(std::size_t index) const { form->set_creating(index == 1); }
  };
  struct pick_terms {
    matrix_form* form;
    void operator()(std::size_t index) const { form->agrees = index == 1; }
  };

  struct parts_t {
    field user_id;
    choice_menu<pick_way> way;
    field password;
    field homeserver;
    field device_name;
    choice_menu<pick_mode> mode;
    field token;
    choice_menu<pick_terms> terms;
    form_end<Actions> end;
  } parts;

  matrix_form(Actions* a, const palette& colours, const std::optional<::mux::proto::matrix::kept>& from)
      : actions(a), parts{.user_id = field(colours, "User ID", "@user:example.org"),
              .way = choice_menu<pick_way>(colours, "Sign in", {"With a password", "In the browser, on the server's page"},
                                           from && from->oauth.value_or(false) ? 1 : 0, pick_way{this}),
              .password = field(colours, "Password", "Password"),
              .homeserver = field(colours, "Homeserver", "found through the server's .well-known"),
              .device_name = field(colours, "Device name", "mux", "mux"),
              .mode = choice_menu<pick_mode>(colours, "Account", {"Sign in to an account", "Create a new account"}, 0, pick_mode{this}),
              .token = field(colours, "Registration token", "where the server registers by invitation"),
              .terms = choice_menu<pick_terms>(colours, "The server's terms", {"Not agreed to", "I agree to the server's terms"}, 0,
                                               pick_terms{this}),
              .end = form_end<Actions>(colours, a, from.has_value())} {
    auto& [user_id, way, password, homeserver, device_name, mode, token, terms, end] = parts;
    in_browser = from && from->oauth.value_or(false);
    password.setVisible(!in_browser);
    // Editing an account kept: it is one already.
    mode.setVisible(!from.has_value());
    token.setVisible(false);
    terms.setVisible(false);
    fState.apply({.fillX = true, .autoSize = scene::axes::kY});
    this->setGap(12.0f);
    password.parts.box.setMasked(true);
    if (from) {
      editing = from->user_id;
      user_id.parts.box.setText(from->user_id);
      password.parts.box.setText(from->password);
      if (from->homeserver)
        homeserver.parts.box.setText(*from->homeserver);
      device_name.parts.box.setText(from->device_name);
    }
  }

  // A new account or one there is: the registration's fields shown with it
  // -- not where the server's own page registers it.
  void set_creating(bool on) {
    creating = on;
    parts.token.setVisible(on && !in_browser);
    parts.terms.setVisible(on && !in_browser);
    this->invalidateLayout();
  }
  // Signed in in the browser: no password here, nor the registration's
  // fields -- the server's page asks what it needs.
  void set_in_browser(bool on) {
    in_browser = on;
    parts.password.setVisible(!on);
    this->set_creating(creating);
  }

  [[nodiscard]] std::expected<::mux::proto::matrix::kept, std::string> account() const {
    ::mux::proto::matrix::kept out{.user_id = parts.user_id.text(),
                               .password = parts.password.text(),
                               .homeserver = typed_or_nothing(parts.homeserver.text()),
                               .device_name = parts.device_name.text()};
    if (in_browser) {
      out.oauth = true;
      out.password.clear();
    }
    if (creating) {
      out.create = true;
      out.registration_token = typed_or_nothing(parts.token.text());
      out.accept_terms = agrees;
      if (out.password.empty() && !in_browser)
        return std::unexpected(std::string("A new account needs a password"));
    }
    if (auto wrong = check(out))  // the protocol's own check, by ADL
      return std::unexpected(*wrong);
    return out;
  }

  void say(std::string text, bool error) { parts.end.say(std::move(text), error); }

  using Node::onKey;
  void onKey(scene::phase::bubble, const scene::key::down& press, scene::Reply& reply) {
    if (press.key == scene::keys::kEnter) {
      actions->submit_login();
      reply.handle();
    }
  }

};


}  // namespace mux::proto::matrix::form_detail

export namespace mux::proto::matrix {

// Its account form, for the panels: by the protocol's tag, and by what an
// account of it keeps.
template <class Actions>
using form = form_detail::matrix_form<Actions>;
template <class Actions>
constexpr type_tag<form<Actions>> form_type(const state&, type_tag<Actions>) {
  return {};
}
template <class Actions>
constexpr type_tag<form<Actions>> form_type_for(const kept&, type_tag<Actions>) {
  return {};
}

}  // namespace mux::proto::matrix
