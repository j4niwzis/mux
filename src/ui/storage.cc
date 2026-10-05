// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:storage -- Settings: storage and files.
export module mux.ui:storage;

import std;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes.flow;
import skiff.nodes.scroll;
import skiff.nodes.text;
import mux.core;
import mux.config;
import :base;
import :icons;
import :controls;
import :accounts;
import :proxies;

export namespace mux::ui {

// A limit changed by a step: halved or doubled.
template <class Actions>
struct step_limit {
  Actions* actions = nullptr;
  config::limit_t which;
  bool more = true;
  void operator()() const { actions->change_limit(which, more); }
};

// Settings' Storage page: how much is kept in memory and on disk, each a
// line with its number and a step down and up; a way to clear what is kept
// on disk; and whether deleted messages are kept.
template <class Actions>
struct storage_page : nodes::Stack {
  using header_t = page_header<ask<Actions, &Actions::settings_home>, ask<Actions, &Actions::close_settings>>;
  struct stepper : nodes::Stack {
    using step_button = icon_button<step_limit<Actions>>;
    struct parts_t {
      nodes::Text label;
      nodes::Text value;
      step_button less;
      step_button more;
    } parts;
    stepper(const palette& colours, Actions* a, std::string what, config::limit_t which)
        : parts{.label = nodes::Text(std::move(what), 15.0f, colours.text),
                .value = nodes::Text("", 14.0f, colours.accent, true),
                .less = step_button(colours, icon::minus{}, {a, which, false}),
                .more = step_button(colours, icon::plus{}, {a, which, true})} {
      this->setHorizontal();
      this->setGap(6.0f);
      fState.apply({.fillX = true, .height = 50.0f, .padding = {0.0f, 12.0f, 0.0f, 20.0f}});
      parts.label.setElided(true);
      parts.label.apply({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
      for (scene::Node* middle : std::initializer_list<scene::Node*>{&parts.value, &parts.less, &parts.more})
        middle->apply({.alignSelf = scene::align::kMiddle});
    }
  };
  using clear_row = row_item<ask<Actions, &Actions::clear_stored>>;
  using keep_row = switch_row<ask<Actions, &Actions::flip_show_deleted>>;
  using seal_row = switch_row<ask<Actions, &Actions::flip_local_encryption>>;
  using change_row = row_item<ask<Actions, &Actions::change_passphrase>>;
  // What is under the header: it scrolls where the dialog is too low for it.
  struct body : nodes::Stack {
    struct parts_t {
      nodes::Text seal_title;
      seal_row seal;
      change_row change;
      nodes::Text seal_note;
      nodes::Text memory_title;
      stepper messages_in_memory;
      stepper pictures_in_memory;
      nodes::Text disk_title;
      stepper messages_on_disk;
      stepper pictures_on_disk;
      clear_row clear;
      nodes::Text note;
      nodes::Text history_title;
      keep_row show_deleted;
      stepper deleted_on_disk;
      nodes::Text events_title;
      chat_choices<Actions> chats;
      typing_choice<Actions> typing;
      nodes::Text history_note;
    } parts;
    body(const palette& colours, Actions* a, const config::history_settings& history, bool sealed)
        : parts{.seal_title = section_title(colours, "ENCRYPTION"),
                .seal = seal_row(colours, "Encrypt local data", {a}),
                .change = change_row(colours, "Change the passphrase", {a}),
                .seal_note = note_text(colours, "Off by default. On, everything mux keeps on disk is sealed under a passphrase asked for at "
                            "every start: settings with passwords and tokens, chats, drafts, encryption keys. Pictures "
                            "are not kept on disk then."),
                .memory_title = section_title(colours, "IN MEMORY"),
                .messages_in_memory = stepper(colours, a, "Messages", config::limit::messages_in_memory{}),
                .pictures_in_memory = stepper(colours, a, "Pictures", config::limit::pictures_in_memory{}),
                .disk_title = section_title(colours, "ON DISK"),
                .messages_on_disk = stepper(colours, a, "Messages", config::limit::messages_on_disk{}),
                .pictures_on_disk = stepper(colours, a, "Pictures", config::limit::pictures_on_disk{}),
                .clear = clear_row(colours, "Clear stored messages and pictures", {a}, icon::close{}),
                .note = note_text(colours, "Memory holds the newest of the chats read lately; the disk holds the rest, and what is scrolled "
                       "back to comes from there before the server. Past a limit, what was used longest ago goes first."),
                .history_title = section_title(colours, "DELETED MESSAGES"),
                .show_deleted = keep_row(colours, "Show deleted messages", {a}),
                .deleted_on_disk = stepper(colours, a, "On disk", config::limit::deleted_on_disk{}),
                .events_title = section_title(colours, "ROOM EVENTS"),
                .chats = chat_choices<Actions>(a, colours, choice_level::everywhere{},
                                               {.events_all = history.show_room_events,
                                                .event_kinds = history.room_event_kinds,
                                                .receipts = history.show_receipts,
                                                .previews = history.link_previews,
                                                .previews_direct = history.previews_direct.value_or(false),
                                                .jump_search = history.jump_search},
                                               0.0f),
                .typing = typing_choice<Actions>(a, colours, choice_level::everywhere{}, history.send_typing.value_or(true)),
                .history_note = note_text(colours, "Deleted messages are kept on disk, apart from the rest and up to their own size, the "
                               "oldest going first past it. Shown, one stays where it was, with all it said and its "
                               "time, marked removed.")} {
      fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 0.0f, 12.0f, 0.0f}});
      parts.memory_title.apply({.margin = {6.0f, 0.0f, 4.0f, 20.0f}});
      parts.disk_title.apply({.margin = {10.0f, 0.0f, 4.0f, 20.0f}});
      parts.history_title.apply({.margin = {14.0f, 0.0f, 4.0f, 20.0f}});
      for (nodes::Text* each : {&parts.note, &parts.history_note}) {
        each->setWrapped(true);
        each->apply({.fillX = true, .margin = {10.0f, 20.0f, 0.0f, 20.0f}});
      }
      parts.show_deleted.parts.toggle.setOnNow(history.show_deleted);
      parts.seal_title.apply({.margin = {6.0f, 0.0f, 4.0f, 20.0f}});
      parts.seal_note.setWrapped(true);
      parts.seal_note.apply({.fillX = true, .margin = {10.0f, 20.0f, 0.0f, 20.0f}});
      this->show_sealed(sealed, true);
      parts.events_title.apply({.margin = {14.0f, 0.0f, 4.0f, 20.0f}});
    }
    // Whether local data is encrypted now: its switch, and the passphrase
    // to change where it is.
    void show_sealed(bool sealed, bool at_once = false) {
      if (at_once)
        parts.seal.parts.toggle.setOnNow(sealed);
      else
        parts.seal.parts.toggle.setOn(sealed);
      parts.change.setVisible(sealed);
    }
  };
  struct parts_t {
    header_t header;
    body list;  // in the settings' own scroll view
  } parts;

  storage_page(const ui_needs<Actions>& n, const config::cache_limits& limits, const config::history_settings& history, bool sealed)
      : storage_page(*n.colours, n.actions, limits, history, sealed) {}
  storage_page(const palette& colours, Actions* a, const config::cache_limits& limits, const config::history_settings& history,
               bool sealed)
      : parts{.header = header_t(colours, "Storage", {a}, {a}, true, true),
              .list = body(colours, a, history, sealed)} {
    fState.apply({.fill = true});
    parts.list.apply({.fillX = true});
    this->show(limits);
  }
  [[nodiscard]] body& content() { return parts.list; }
  void show(const config::cache_limits& limits) {
    auto& rows = this->content().parts;
    rows.messages_in_memory.parts.value.setText(std::format("{} messages", limits.messages_in_memory));
    rows.pictures_in_memory.parts.value.setText(std::format("{} MB", limits.pictures_in_memory_mb));
    rows.messages_on_disk.parts.value.setText(std::format("{} MB", limits.messages_on_disk_mb));
    rows.pictures_on_disk.parts.value.setText(std::format("{} MB", limits.pictures_on_disk_mb));
    rows.deleted_on_disk.parts.value.setText(std::format("{} MB", config::deleted_on_disk_of(limits)));
  }
  void show_motion(std::string_view) {}
  void show_receipts(bool) {}
  void show_deleted(bool shown) { this->content().parts.show_deleted.parts.toggle.setOn(shown); }
  void show_sealed(bool sealed) { this->content().show_sealed(sealed); }
};

// Settings' Notifications page, as Telegram Desktop's: a notification on
// the desktop or not, the sender's name and the text in it or not, a sound
// or not; and what shows it -- the desktop's own service, or mux's window.
template <class Actions>
struct choose_notify_backend {
  Actions* actions = nullptr;
  config::notify_backend_t backend;
  void operator()() const { actions->set_notify_backend(backend); }
};
template <class Actions>
struct notifications_page : nodes::Stack {
  using header_t = page_header<ask<Actions, &Actions::settings_home>, ask<Actions, &Actions::close_settings>>;
  using push_row = switch_row<ask<Actions, &Actions::flip_unified_push>>;
  using backend_segment = segment<choose_notify_backend<Actions>>;
  struct backend_row : nodes::Stack {
    struct parts_t {
      backend_segment native, built_in;
    } parts;
    backend_row(const palette& colours, Actions* a)
        : parts{.native = backend_segment(colours, "System", {a, config::notify_backend::native{}}),
                .built_in = backend_segment(colours, "Built in", {a, config::notify_backend::built_in{}})} {
      this->setHorizontal();
      this->setGap(4.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {4.0f, 20.0f, 4.0f, 20.0f}});
    }
  };
  struct parts_t {
    header_t header;
    nodes::Text title;
    notify_choice_rows<Actions> choices;
    nodes::Text backend_title;
    backend_row backend;
    nodes::Text note;
    nodes::Text push_title;
    push_row push;
    nodes::Text push_note;
  } parts;
  notifications_page(const ui_needs<Actions>& n, const config::notification_settings& now)
      : notifications_page(*n.colours, n.actions, now) {}
  notifications_page(const palette& colours, Actions* a, const config::notification_settings& now)
      : parts{.header = header_t(colours, "Notifications", {a}, {a}, true, true),
              .title = section_title(colours, "NOTIFICATIONS"),
              .choices = notify_choice_rows<Actions>(a, colours, choice_level::everywhere{}, config::notify_choices_of(now), 4.0f),
              .backend_title = section_title(colours, "SHOWN BY"),
              .backend = backend_row(colours, a),
              .note = note_text(colours, "System asks the desktop's own notification service (org.freedesktop.Notifications); Built in "
                     "shows mux's own, in a corner of the screen, as Telegram Desktop does."),
              .push_title = section_title(colours, "WAKE"),
              .push = push_row(colours, "Wake by UnifiedPush", {a}),
              .push_note = note_text(colours, "Your Matrix servers push to the UnifiedPush distributor on this device (ntfy, NextPush, "
                          "KDE's), which wakes mux at once. Off, nothing is given to the servers, and mux only learns "
                          "of messages while it runs.")} {
    fState.apply({.fill = true});
    parts.choices.apply({.padding = {0.0f, 20.0f, 0.0f, 20.0f}});
    for (nodes::Text* title : {&parts.title, &parts.backend_title, &parts.push_title})
      title->apply({.margin = {10.0f, 0.0f, 4.0f, 20.0f}});
    for (nodes::Text* note : {&parts.note, &parts.push_note}) {
      note->setWrapped(true);
      note->apply({.fillX = true, .margin = {10.0f, 20.0f, 0.0f, 20.0f}});
    }
    this->show(now);
  }
  void show(const config::notification_settings& now) {
    parts.choices.show(config::notify_choices_of(now));
    parts.push.parts.toggle.setOnNow(now.unified_push.value_or(false));
    const bool native = splice::visit(splice::overloaded{[](config::notify_backend::native) { return true; },
                                              [](const auto&) { return false; }},
                                   config::notify_backend_of(now.backend));
    parts.backend.parts.native.set_active(native);
    parts.backend.parts.built_in.set_active(!native);
  }
  void show_motion(std::string_view) {}
  void show_receipts(bool) {}
};

// Settings' Files page: what is done to a picture dropped on the window
// before it is sent.
template <class Actions>
struct files_page : nodes::Stack {
  using header_t = page_header<ask<Actions, &Actions::settings_home>, ask<Actions, &Actions::close_settings>>;
  using strip_row = switch_row<ask<Actions, &Actions::flip_strip_metadata>>;
  using rename_row = switch_row<ask<Actions, &Actions::flip_rename_pictures>>;
  struct parts_t {
    header_t header;
    nodes::Text title;
    strip_row strip;
    rename_row rename;
    nodes::Text note;
  } parts;
  files_page(const ui_needs<Actions>& n, const config::sending_settings& now) : files_page(*n.colours, n.actions, now) {}
  files_page(const palette& colours, Actions* a, const config::sending_settings& now)
      : parts{.header = header_t(colours, "Files", {a}, {a}, true, true),
              .title = section_title(colours, "PICTURES DROPPED ON THE WINDOW"),
              .strip = strip_row(colours, "Remove metadata", {a}),
              .rename = rename_row(colours, "Name them image.<type>", {a}),
              .note = note_text(colours, "Metadata is where and when a picture was taken, with what, by whom: EXIF, XMP and the like. "
                     "It is cut out of the file; the picture itself is sent as it is, not compressed again.")} {
    fState.apply({.fill = true});
    parts.title.apply({.margin = {6.0f, 0.0f, 4.0f, 20.0f}});
    parts.note.setWrapped(true);
    parts.note.apply({.fillX = true, .margin = {10.0f, 20.0f, 0.0f, 20.0f}});
    this->show(now);
  }
  void show(const config::sending_settings& now) {
    parts.strip.parts.toggle.setOnNow(now.strip_metadata);
    parts.rename.parts.toggle.setOnNow(now.rename);
  }
  void show_motion(std::string_view) {}
  void show_receipts(bool) {}
};

}  // namespace mux::ui
