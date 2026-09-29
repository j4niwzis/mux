// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:storage -- Settings: storage and files.
export module mux.ui:storage;

import std;
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
      nodes::Text value{"", 14.0f, accent_colour, true};
      step_button less;
      step_button more;
    } parts;
    stepper(Actions* a, std::string what, config::limit_t which)
        : parts{.label = nodes::Text(std::move(what), 15.0f, text_colour),
                .less = step_button(icon::minus{}, {a, which, false}),
                .more = step_button(icon::plus{}, {a, which, true})} {
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
  // What is under the header: it scrolls where the dialog is too low for it.
  struct body : nodes::Stack {
    struct parts_t {
      nodes::Text memory_title = section_title("IN MEMORY");
      stepper messages_in_memory;
      stepper pictures_in_memory;
      nodes::Text disk_title = section_title("ON DISK");
      stepper messages_on_disk;
      stepper pictures_on_disk;
      clear_row clear;
      nodes::Text note{"Memory holds the newest of the chats read lately; the disk holds the rest, and what is scrolled "
                       "back to comes from there before the server. Past a limit, what was used longest ago goes first.",
                       13.0f, dim_colour};
      nodes::Text history_title = section_title("DELETED MESSAGES");
      keep_row show_deleted;
      stepper deleted_on_disk;
      nodes::Text events_title = section_title("ROOM EVENTS");
      event_kind_list<Actions> events;
      nodes::Text history_note{"Deleted messages are kept on disk, apart from the rest and up to their own size, the "
                               "oldest going first past it. Shown, one stays where it was, with all it said and its "
                               "time, marked removed.",
                               13.0f, dim_colour};
    } parts;
    body(Actions* a, const config::history_settings& history)
        : parts{.messages_in_memory = stepper(a, "Messages", config::limit::messages_in_memory{}),
                .pictures_in_memory = stepper(a, "Pictures", config::limit::pictures_in_memory{}),
                .messages_on_disk = stepper(a, "Messages", config::limit::messages_on_disk{}),
                .pictures_on_disk = stepper(a, "Pictures", config::limit::pictures_on_disk{}),
                .clear = clear_row("Clear stored messages and pictures", {a}, icon::close{}),
                .show_deleted = keep_row("Show deleted messages", {a}),
                .deleted_on_disk = stepper(a, "On disk", config::limit::deleted_on_disk{}),
                .events = event_kind_list<Actions>(a, choice_level::everywhere{}, history.show_room_events,
                                                   history.room_event_kinds)} {
      fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 0.0f, 12.0f, 0.0f}});
      parts.memory_title.apply({.margin = {6.0f, 0.0f, 4.0f, 20.0f}});
      parts.disk_title.apply({.margin = {10.0f, 0.0f, 4.0f, 20.0f}});
      parts.history_title.apply({.margin = {14.0f, 0.0f, 4.0f, 20.0f}});
      for (nodes::Text* each : {&parts.note, &parts.history_note}) {
        each->setWrapped(true);
        each->apply({.fillX = true, .margin = {10.0f, 20.0f, 0.0f, 20.0f}});
      }
      parts.show_deleted.parts.toggle.setOnNow(history.show_deleted);
      parts.events_title.apply({.margin = {14.0f, 0.0f, 4.0f, 20.0f}});
    }
  };
  struct parts_t {
    header_t header;
    nodes::ScrollContainer<body> list;
  } parts;

  storage_page(Actions* a, const config::cache_limits& limits, const config::history_settings& history)
      : parts{.header = header_t("Storage", {a}, {a}, true, true),
              .list = nodes::ScrollContainer<body>(body(a, history))} {
    fState.apply({.fill = true});
    parts.list.apply({.fillX = true, .grow = scene::axes::kY});
    this->show(limits);
  }
  [[nodiscard]] body& content() { return std::get<0>(parts.list.fChildren); }
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
    nodes::Text title = section_title("PICTURES DROPPED ON THE WINDOW");
    strip_row strip;
    rename_row rename;
    nodes::Text note{"Metadata is where and when a picture was taken, with what, by whom: EXIF, XMP and the like. "
                     "It is cut out of the file; the picture itself is sent as it is, not compressed again.",
                     13.0f, dim_colour};
  } parts;
  files_page(Actions* a, const config::sending_settings& now)
      : parts{.header = header_t("Files", {a}, {a}, true, true),
              .strip = strip_row("Remove metadata", {a}),
              .rename = rename_row("Name them image.<type>", {a})} {
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
