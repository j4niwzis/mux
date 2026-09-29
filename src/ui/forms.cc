// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:forms -- Form rows, and the account forms.
export module mux.ui:forms;

import std;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes;
import skiff.widgets;
import mux.core;
import mux.config;
export import :conversations;

export namespace mux::ui {

// ---- a form row: a caption and a field -----------------------------------

struct field : nodes::Stack {
  nodes::Text caption;
  widgets::TextArea<> box;

  // Declared: the caption over the field, which sits on a plate.
  field(std::string label, std::string placeholder, std::string text = {})
      : caption(std::move(label), 13.0f, dim_colour), box(std::move(placeholder)) {
    this->setGap(4.0f);
    fState.apply({.fillX = true, .height = 64.0f});
    box.setSingleLine(true);
    box.apply({.fillX = true, .margin = {0.0f, 10.0f, 0.0f, 10.0f}});
    box.setText(std::move(text));
  }
  void forEachChild(auto&& f) {
    f(caption);
    f(box);
  }
  // The plate is where the field is, out to the row's edges.
  void drawSelf(skia::SkCanvas* canvas, float alpha) {
    skia::SkFont* font = skiff::paint::defaultFont();
    const skia::SkRect& at = box.bounds();
    if (font == nullptr || at.isEmpty())
      return;
    const skia::SkRect plate = skia::SkRect::MakeLTRB(fState.fBounds.fLeft, at.fTop, fState.fBounds.fRight, at.fBottom);
    const skiff::paint::Painter p(canvas, *font);
    p.fillRounded(plate, 6.0f, tile_colour, alpha);
    p.strokeRounded(plate, 6.0f, box.focused() ? accent_colour : band_colour, 1.0f, alpha);
  }
};

// ---- the account forms ---------------------------------------------------------

// Buttons side by side, as a form ends.
template <class... Buttons>
struct button_row : nodes::Stack {
  std::tuple<Buttons...> buttons;
  explicit button_row(Buttons... all) : buttons(std::move(all)...) {
    this->setHorizontal();
    this->setGap(10.0f);
    fState.apply({.autoSize = scene::axes::kBoth});
  }
  void forEachChild(auto&& f) {
    std::apply([&](auto&... each) { (f(each), ...); }, buttons);
  }
};

// What every account form ends with: what went wrong or what is happening,
// and its buttons. Enter in any of the form's fields submits it.
template <class Actions>
struct form_end : nodes::Stack {
  nodes::Text message{"", 13.0f, error_colour};
  button_row<widgets::Button<ask<Actions, &Actions::submit_login>>, widgets::Button<ask<Actions, &Actions::pop_panel>>>
      buttons;

  form_end(Actions* a, bool editing)
      : buttons(widgets::Button<ask<Actions, &Actions::submit_login>>(editing ? "Save" : "Log in", {a}),
                widgets::Button<ask<Actions, &Actions::pop_panel>>("Close", {a})) {
    fState.apply({.fillX = true, .autoSize = scene::axes::kY});
    this->setGap(12.0f);
    auto& [submit, close] = buttons.buttons;
    submit.setPrimary(true);
    submit.apply({.width = 120.0f, .height = 36.0f});
    close.apply({.width = 120.0f, .height = 36.0f});
    close.setVisible(editing);
    message.setWrapped(true);
    message.apply({.fillX = true});
  }

  void forEachChild(auto&& f) {
    f(message);
    f(buttons);
  }

  void say(std::string text, bool error) {
    message.setText(std::move(text));
    message.setColour(error ? error_colour : dim_colour);
  }
};

// Text typed into an optional: nothing when the field is empty.
[[nodiscard]] inline std::optional<std::string> typed_or_nothing(const std::string& text) {
  if (text.empty())
    return std::nullopt;
  return text;
}

// What "Advanced" folds out on an XMPP form: the resource, where to connect,
// and PLAIN without TLS. Its height is what its last layout took.
template <class Actions>
struct xmpp_advanced : nodes::Stack {
  field resource{"Device name (resource)", "mux", "mux"};
  field host{"Host", "from the domain's SRV records"};
  field port{"Port", "5222"};
  // The switch and what it says, side by side.
  struct plain_row : nodes::Stack {
    widgets::Toggle<ask<Actions, &Actions::toggle_plain>> plain;
    nodes::Text label{"Allow PLAIN without TLS. Only for a test server on this machine: never over a network.",
                      13.0f, error_colour};
    explicit plain_row(Actions* a) : plain({a}) {
      this->setHorizontal();
      this->setGap(10.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY});
      label.setWrapped(true);
      label.apply({.grow = scene::axes::kX});
    }
    void forEachChild(auto&& f) {
      f(plain);
      f(label);
    }
  } row;
  widgets::Toggle<ask<Actions, &Actions::toggle_plain>>& plain = row.plain;

  explicit xmpp_advanced(Actions* a) : row(a) {
    fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 0.0f, 12.0f, 0.0f}});
    this->setGap(8.0f);
  }
  void forEachChild(auto&& f) {
    f(resource);
    f(host);
    f(port);
    f(row);
  }
};

// An XMPP account's settings: its JID and password, and "Advanced" folds out
// the rest. It fills the column it is given.
template <class Actions>
struct xmpp_form : nodes::Stack {
  Actions* actions = nullptr;
  // The address the account was saved under, when this is an edit of one.
  std::optional<std::string> editing;
  bool advanced = false;

  field address{"Address (JID)", "user@example.com"};
  field password{"Password", "Password"};
  widgets::Button<ask<Actions, &Actions::toggle_advanced>> advanced_button;
  widgets::Collapsible<xmpp_advanced<Actions>> more;
  form_end<Actions> end;

  xmpp_form(Actions* a, const std::optional<config::xmpp_account>& from)
      : actions(a), advanced_button("Advanced", {a}), more(a), end(a, from.has_value()) {
    fState.apply({.fillX = true, .autoSize = scene::axes::kY});
    this->setGap(12.0f);
    password.box.setMasked(true);
    advanced_button.apply({.width = 120.0f, .height = 36.0f});
    if (from) {
      editing = from->address;
      address.box.setText(from->address);
      password.box.setText(from->password);
      more.child().resource.box.setText(from->resource);
      if (from->host)
        more.child().host.box.setText(*from->host);
      if (from->port)
        more.child().port.box.setText(std::to_string(*from->port));
      more.child().plain.setOn(from->plain_without_tls);
      advanced = from->resource != "mux" || from->host || from->port || from->plain_without_tls;
    }
    more.setOpenNow(advanced);
  }

  void forEachChild(auto&& f) {
    f(address);
    f(password);
    f(advanced_button);
    f(more);
    f(end);
  }

  // Folded out or away: smoothly, where things move.
  void show_advanced(bool shown) {
    advanced = shown;
    more.setOpen(shown);
  }

  void flip_plain() { more.child().plain.setOn(!more.child().plain.on()); }

  // The account as typed, or what is wrong with it. What is folded away is
  // kept as it is: folding is not clearing.
  [[nodiscard]] std::expected<config::xmpp_account, std::string> account() const {
    config::xmpp_account out{.address = address.box.text(),
                             .password = password.box.text(),
                             .resource = more.child().resource.box.text(),
                             .host = typed_or_nothing(more.child().host.box.text()),
                             .plain_without_tls = more.child().plain.on()};
    if (const std::string& text = more.child().port.box.text(); !text.empty()) {
      std::int64_t number = 0;
      const auto [last, failed] = std::from_chars(text.data(), text.data() + text.size(), number);
      if (failed != std::errc{} || last != text.data() + text.size())
        return std::unexpected("A port is a number from 1 to 65535");
      out.port = number;
    }
    if (auto wrong = config::check(out))
      return std::unexpected(*wrong);
    return out;
  }

  void say(std::string text, bool error) { end.say(std::move(text), error); }

  using Node::onKey;
  void onKey(scene::phase::bubble, const scene::key::down& press, scene::Reply& reply) {
    if (press.key == scene::keys::kEnter) {
      actions->submit_login();
      reply.handle();
    }
  }

};

// A Matrix account's settings: its user ID and password, the homeserver
// (found through the server's .well-known when left empty) and what this
// device is called.
template <class Actions>
struct matrix_form : nodes::Stack {
  Actions* actions = nullptr;
  std::optional<std::string> editing;

  field user_id{"User ID", "@user:example.org"};
  field password{"Password", "Password"};
  field homeserver{"Homeserver", "found through the server's .well-known"};
  field device_name{"Device name", "mux", "mux"};
  form_end<Actions> end;

  matrix_form(Actions* a, const std::optional<config::matrix_account>& from)
      : actions(a), end(a, from.has_value()) {
    fState.apply({.fillX = true, .autoSize = scene::axes::kY});
    this->setGap(12.0f);
    password.box.setMasked(true);
    if (from) {
      editing = from->user_id;
      user_id.box.setText(from->user_id);
      password.box.setText(from->password);
      if (from->homeserver)
        homeserver.box.setText(*from->homeserver);
      device_name.box.setText(from->device_name);
    }
  }

  void forEachChild(auto&& f) {
    f(user_id);
    f(password);
    f(homeserver);
    f(device_name);
    f(end);
  }

  [[nodiscard]] std::expected<config::matrix_account, std::string> account() const {
    config::matrix_account out{.user_id = user_id.box.text(),
                               .password = password.box.text(),
                               .homeserver = typed_or_nothing(homeserver.box.text()),
                               .device_name = device_name.box.text()};
    if (auto wrong = config::check(out))
      return std::unexpected(*wrong);
    return out;
  }

  void say(std::string text, bool error) { end.say(std::move(text), error); }

  using Node::onKey;
  void onKey(scene::phase::bubble, const scene::key::down& press, scene::Reply& reply) {
    if (press.key == scene::keys::kEnter) {
      actions->submit_login();
      reply.handle();
    }
  }

};

// Either form, as the panels hold them.
template <class Actions>
using account_form = std::variant<xmpp_form<Actions>, matrix_form<Actions>>;

// The form of an account's own protocol, filled in from it.
template <class Actions>
[[nodiscard]] account_form<Actions> form_of(Actions* a, const config::account_t& saved) {
  return std::visit(overloaded{[a](const config::xmpp_account& one) {
                                 return account_form<Actions>(std::in_place_index<0>, a, one);
                               },
                               [a](const config::matrix_account& one) {
                                 return account_form<Actions>(std::in_place_index<1>, a, one);
                               }},
                    saved);
}

// The XMPP form among them, when that is the one up.
template <class Actions>
[[nodiscard]] xmpp_form<Actions>* xmpp_form_in(account_form<Actions>& form) {
  return std::visit(overloaded{[](xmpp_form<Actions>& one) { return &one; },
                               [](matrix_form<Actions>&) -> xmpp_form<Actions>* { return nullptr; }},
                    form);
}

// A form laid out in the column under `top`.
template <class Actions>
void place_form(account_form<Actions>& form, const skia::SkRect& column, float top) {
  std::visit(
      [&](auto& one) {
        one.fState.arrange(0.0f, 0.0f);
        scene::layout(one, skia::SkRect::MakeLTRB(column.fLeft, column.fTop + top, column.fRight, column.fBottom));
      },
      form);
}

// Esc closes a panel: back to what is under it.
template <class Actions>
struct closes_on_escape : nodes::Stack {
  Actions* actions = nullptr;
  explicit closes_on_escape(Actions* a) : actions(a) {}

  using Node::onKey;
  void onKey(scene::phase::bubble, const scene::key::down& press, scene::Reply& reply) {
    if (press.key == scene::keys::kEscape) {
      actions->pop_panel();
      reply.handle();
    }
  }
};

}  // namespace mux::ui
