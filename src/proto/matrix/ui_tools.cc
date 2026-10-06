// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui.proto.matrix:tools -- The developer tools.
export module mux.ui.proto.matrix:tools;

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

// ---- the developer tools ----------------------------------------------------------
namespace mux::proto::matrix::tools_detail {
using namespace ::mux::ui;

// The developer tools, as Element's: some JSON to read and copy; a room's
// state, by type, then by key, then the event; an event of any type sent.
template <class Actions>
struct devtools_box : nodes::Stack {
  Actions* actions = nullptr;
  // The colours it is made in, for its parts and the rows it makes later.
  const palette* colours_ = nullptr;
  struct close_it {
    Actions* actions;
    void operator()() const { actions->close_dialog(); }
  };
  struct back_up {
    devtools_box* box;
    void operator()() const { box->go_back(); }
  };
  using header_t = page_header<back_up, close_it>;
  // A line of the state's list: a type, or a key of one; pressed, what is
  // under it, at the next frame -- not from inside the list it is in.
  struct pick {
    devtools_box* box;
    std::string type;
    std::optional<std::string> key;
    void operator()() const {
      box->pending = pick{box, type, key};
      scene::work::mark(box->fState.fId);  // its next frame asked for: nothing else asks
    }
  };
  struct send_press {
    devtools_box* box;
    void operator()() const { box->send(); }
  };
  using entry_row = row_item<pick>;
  using rows_t = nodes::Flow<std::vector<entry_row>>;
  struct form : nodes::Stack {
    struct parts_t {
      field type;
      field key;
      nodes::Text body_caption;
      widgets::TextArea<> body;
      widgets::Button<send_press> send;
    } parts;
    explicit form(devtools_box* box)
        : parts{.type = field(*box->colours_, "Event type", "m.room.message"),
                .key = field(*box->colours_, "State key (for a state event; empty for a timeline one)", ""),
                .body_caption = nodes::Text("Content (a JSON object)", 13.0f, box->colours_->dim),
                .body = widgets::TextArea<>(box->colours_->widgets, "{}"),
                .send = widgets::Button<send_press>(box->colours_->widgets, "Send", {box})} {
      this->setGap(8.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 12.0f, 12.0f, 12.0f}});
      parts.body_caption.apply({.margin = {0.0f, 10.0f, 0.0f, 10.0f}});
      parts.body.apply({.fillX = true, .height = 180.0f, .margin = {0.0f, 10.0f, 0.0f, 10.0f}, .cornerRadius = 6.0f,
                        .background = box->colours_->tile, .border = scene::Border{box->colours_->band, 1.0f}});
      parts.body.setText("{\n  \n}");
      parts.send.setPrimary(true);
      parts.send.apply({.width = 120.0f, .height = 34.0f, .margin = {0.0f, 10.0f, 0.0f, 10.0f}});
    }
  };
  struct parts_t {
    header_t header;
    nodes::ScrollContainer<nodes::Text> reading;
    nodes::ScrollContainer<rows_t> list{rows_t({.spacingY = 0.0f, .wrap = false}, {})};
    std::optional<form> sending;
  } parts;
  // What it shows: some text, or the state -- all its events -- at a level.
  std::vector<proto::matrix::state_entry> state;
  std::optional<std::string> type_shown;
  bool showing_state = false;
  std::optional<pick> pending;

  devtools_box(Actions* a, const palette& colours, std::string title, std::string text)
      : actions(a),
        colours_(&colours),
        parts{.header = header_t(colours, std::move(title), {this}, {a}, false, true), .reading = reading_of(colours)} {
    this->lay_out();
    this->show_text(std::move(text));
  }
  devtools_box(Actions* a, const palette& colours, std::vector<proto::matrix::state_entry> entries)
      : actions(a),
        colours_(&colours),
        parts{.header = header_t(colours, "Room state", {this}, {a}, false, true), .reading = reading_of(colours)},
        state(std::move(entries)) {
    this->lay_out();
    this->show_types();
  }
  struct send_form_t {};
  // Where a text is read, in the colours it is made in.
  [[nodiscard]] static nodes::ScrollContainer<nodes::Text> reading_of(const palette& colours) {
    return nodes::ScrollContainer<nodes::Text>(nodes::Text("", 13.0f, colours.text));
  }
  devtools_box(Actions* a, const palette& colours, send_form_t)
      : actions(a),
        colours_(&colours),
        parts{.header = header_t(colours, "Send custom event", {this}, {a}, false, true), .reading = reading_of(colours)} {
    this->lay_out();
    parts.sending.emplace(this);
    parts.reading.setVisible(false);
    parts.list.setVisible(false);
  }
  void lay_out() {
    fState.apply({.fillX = true, .height = 560.0f});
    for (scene::Node* each : std::initializer_list<scene::Node*>{&parts.reading, &parts.list})
      each->apply({.fillX = true, .grow = scene::axes::kY});
    auto& text = std::get<0>(parts.reading.fChildren);
    text.setWrapped(true);
    text.setSelectable(true);
    // A margin, not padding: a text draws from its own edge.
    text.apply({.fillX = true, .margin = {6.0f, 16.0f, 12.0f, 16.0f}});
    std::get<0>(parts.list.fChildren).apply({.fillX = true, .autoSize = scene::axes::kY});
  }
  void show_text(std::string text) {
    std::get<0>(parts.reading.fChildren).setText(std::move(text));
    parts.reading.setVisible(true);
    parts.list.setVisible(false);
    parts.reading.scrollTo(0.0f);
    this->invalidateLayout();
  }
  void show_rows(std::vector<std::pair<std::string, pick>> rows) {
    auto& all = std::get<0>(std::get<0>(parts.list.fChildren).fChildren);
    all.clear();
    for (auto& [label, what] : rows)
      all.emplace_back(*colours_, std::move(label), std::move(what));
    parts.reading.setVisible(false);
    parts.list.setVisible(true);
    parts.list.scrollTo(0.0f);
    this->invalidateLayout();
  }
  // Every type, and how many events of it.
  void show_types() {
    showing_state = true;
    type_shown.reset();
    parts.header.parts.back.setVisible(false);
    std::map<std::string, std::size_t> counts;
    for (const proto::matrix::state_entry& one : state)
      ++counts[one.type];
    std::vector<std::pair<std::string, pick>> rows;
    for (const auto& [type, count] : counts)
      // Joined, not formatted: clang 23 crashed on this format string made
      // first in this module (see app/network.cc).
      rows.emplace_back(type + "  (" + std::to_string(count) + ")", pick{this, type, std::nullopt});
    this->show_rows(std::move(rows));
  }
  // A type's keys.
  void show_keys(const std::string& type) {
    type_shown = type;
    parts.header.parts.back.setVisible(true);
    std::vector<std::pair<std::string, pick>> rows;
    for (const proto::matrix::state_entry& one : state)
      if (one.type == type)
        rows.emplace_back(one.key.empty() ? std::string("(empty key)") : one.key, pick{this, type, one.key});
    this->show_rows(std::move(rows));
  }
  void go_back() {
    if (!showing_state)
      return;
    if (!parts.reading.visible() && type_shown)
      this->show_types();
    else if (type_shown)
      this->show_keys(*type_shown);
  }
  // The other button over what it reads: copied -- what is selected, or
  // all of it -- and said so in the title for a moment.
  using scene::Node::onPointer;
  void onPointer(scene::phase::bubble, const scene::pointer::down& press, scene::PointerReply& reply) {
    if (press.button != 3 || !parts.reading.visible())
      return;
    const auto& text = std::get<0>(parts.reading.fChildren);
    skiff::scene::setClipboardText(text.hasSelection() ? text.selected() : text.text());
    auto& title = parts.header.parts.title;
    if (!title_before)
      title_before = title.text();
    title.setText(text.hasSelection() ? "Selection copied" : "Copied");
    title_back_at = 0.0;  // counted from the next frame
    reply.handle();
  }
  std::optional<std::string> title_before;
  double title_back_at = 0.0;
  [[nodiscard]] bool wantsTick() const { return pending.has_value() || title_before.has_value(); }
  void update(double now_ms) {
    if (title_before) {
      if (title_back_at == 0.0)
        title_back_at = now_ms + 1200.0;
      else if (now_ms >= title_back_at)
        parts.header.parts.title.setText(*std::exchange(title_before, std::nullopt));
    }
    if (!pending)
      return;
    const pick what = *std::exchange(pending, std::nullopt);
    if (!what.key) {
      this->show_keys(what.type);
      return;
    }
    for (const proto::matrix::state_entry& one : state)
      if (one.type == what.type && one.key == *what.key) {
        parts.header.parts.back.setVisible(true);
        this->show_text(one.json);
        return;
      }
  }
  void send() {
    if (!parts.sending)
      return;
    auto& [type, key, caption, body, button] = parts.sending->parts;
    if (type.text().empty())
      return;
    actions->ask_for(request::send_custom{type.text(), key.text().empty() ? std::nullopt : std::optional<std::string>(key.text()),
                                          body.text()});
  }
};

// A step of interactive auth done in the browser, as Element's: what it is
// for, its page opened there again where it was closed, and Continue once
// it is done there -- or Cancel.
template <class Actions>
struct uia_box : nodes::Stack {
  struct open_again {
    Actions* actions;
    std::string url;
    void operator()() const { actions->open_url(url); }
  };
  struct cancel {
    Actions* actions;
    void operator()() const { actions->ask_for(request::cancel_uia{}); }
  };
  struct go {
    Actions* actions;
    void operator()() const { actions->ask_for(request::continue_uia{}); }
  };
  struct parts_t {
    nodes::Text title;
    nodes::Text about;
    widgets::Button<open_again> again;
    dialog_buttons<cancel, go> buttons;
  } parts;
  uia_box(Actions* a, const palette& colours, std::string what, std::string url)
      : parts{.title = nodes::Text(std::move(what), 17.0f, colours.text, true),
              .about = nodes::Text("Your server asks you to confirm this in your browser: the page is open there. "
                                   "Once you have done what it asks, press Continue.",
                                   14.0f, colours.dim),
              .again = widgets::Button<open_again>(colours.widgets, "Open the page again", {a, std::move(url)}),
              .buttons = dialog_buttons<cancel, go>(colours, "Continue", {a}, {a}, 120.0f)} {
    fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {20.0f, 22.0f, 20.0f, 22.0f}});
    this->setGap(10.0f);
    parts.about.setWrapped(true);
    parts.about.apply({.fillX = true});
    parts.again.apply({.width = 200.0f, .height = 32.0f});
  }
};

}  // namespace mux::proto::matrix::tools_detail

export namespace mux::proto::matrix {
template <class Actions>
using uia_page = tools_detail::uia_box<Actions>;
// For the program, which opens it with what the client says.
template <class Actions>
using devtools_page = tools_detail::devtools_box<Actions>;
}  // namespace mux::proto::matrix

export namespace mux::proto::matrix::tool {
template <class Actions>
constexpr type_tag<tools_detail::devtools_box<Actions>> dialog_type(devtools, type_tag<Actions>) {
  return {};
}
template <class Actions>
constexpr type_tag<tools_detail::uia_box<Actions>> dialog_type(uia, type_tag<Actions>) {
  return {};
}
}  // namespace mux::proto::matrix::tool
