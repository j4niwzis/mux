// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:storage -- Settings: storage and files.
export module mux.ui:storage;

import std;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes;
import skiff.widgets;
import mux.core;
import mux.config;
export import :appearance;

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
// line with its number and a step down and up; and a way to clear what is
// kept on disk.
template <class Actions>
struct storage_page : nodes::Stack {
  page_header<ask<Actions, &Actions::settings_home>, ask<Actions, &Actions::close_settings>> header;
  struct stepper : nodes::Stack {
    nodes::Text label;
    nodes::Text value{"", 14.0f, accent_colour, true};
    icon_button<step_limit<Actions>> less;
    icon_button<step_limit<Actions>> more;
    stepper(Actions* a, std::string what, config::limit_t which)
        : label(std::move(what), 15.0f, text_colour), less(icon::minus{}, {a, which, false}),
          more(icon::plus{}, {a, which, true}) {
      this->setHorizontal();
      this->setGap(6.0f);
      fState.apply({.fillX = true, .height = 50.0f, .padding = {0.0f, 12.0f, 0.0f, 20.0f}});
      label.setElided(true);
      label.apply({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
      for (scene::Node* middle : std::initializer_list<scene::Node*>{&value, &less, &more})
        middle->apply({.alignSelf = scene::align::kMiddle});
    }
    void forEachChild(auto&& f) {
      f(label);
      f(value);
      f(less);
      f(more);
    }
  };
  nodes::Text memory_title = section_title("IN MEMORY");
  stepper messages_in_memory;
  stepper pictures_in_memory;
  nodes::Text disk_title = section_title("ON DISK");
  stepper messages_on_disk;
  stepper pictures_on_disk;
  row_item<ask<Actions, &Actions::clear_stored>> clear;
  nodes::Text note{"Memory holds the newest of the chats read lately; the disk holds the rest, and what is scrolled "
                   "back to comes from there before the server. Past a limit, what was used longest ago goes first.",
                   13.0f, dim_colour};

  storage_page(Actions* a, const config::cache_limits& limits)
      : header("Storage", {a}, {a}, true, true),
        messages_in_memory(a, "Messages", config::limit::messages_in_memory{}),
        pictures_in_memory(a, "Pictures", config::limit::pictures_in_memory{}),
        messages_on_disk(a, "Messages", config::limit::messages_on_disk{}),
        pictures_on_disk(a, "Pictures", config::limit::pictures_on_disk{}),
        clear("Clear stored messages and pictures", {a}, icon::close{}) {
    fState.apply({.fill = true});
    memory_title.apply({.margin = {6.0f, 0.0f, 4.0f, 20.0f}});
    disk_title.apply({.margin = {10.0f, 0.0f, 4.0f, 20.0f}});
    note.setWrapped(true);
    note.apply({.fillX = true, .margin = {10.0f, 20.0f, 0.0f, 20.0f}});
    this->show(limits);
  }
  void show(const config::cache_limits& limits) {
    messages_in_memory.value.setText(std::format("{} messages", limits.messages_in_memory));
    pictures_in_memory.value.setText(std::format("{} MB", limits.pictures_in_memory_mb));
    messages_on_disk.value.setText(std::format("{} MB", limits.messages_on_disk_mb));
    pictures_on_disk.value.setText(std::format("{} MB", limits.pictures_on_disk_mb));
  }
  void show_motion(std::string_view) {}
  void show_receipts(bool) {}
  void forEachChild(auto&& f) {
    f(header);
    f(memory_title);
    f(messages_in_memory);
    f(pictures_in_memory);
    f(disk_title);
    f(messages_on_disk);
    f(pictures_on_disk);
    f(clear);
    f(note);
  }
};

// Settings' Files page: what is done to a picture dropped on the window
// before it is sent.
template <class Actions>
struct files_page : nodes::Stack {
  page_header<ask<Actions, &Actions::settings_home>, ask<Actions, &Actions::close_settings>> header;
  nodes::Text title = section_title("PICTURES DROPPED ON THE WINDOW");
  switch_row<ask<Actions, &Actions::flip_strip_metadata>> strip;
  switch_row<ask<Actions, &Actions::flip_rename_pictures>> rename;
  nodes::Text note{"Metadata is where and when a picture was taken, with what, by whom: EXIF, XMP and the like. "
                   "It is cut out of the file; the picture itself is sent as it is, not compressed again.",
                   13.0f, dim_colour};
  files_page(Actions* a, const config::sending_settings& now)
      : header("Files", {a}, {a}, true, true), strip("Remove metadata", {a}), rename("Name them image.<type>", {a}) {
    fState.apply({.fill = true});
    title.apply({.margin = {6.0f, 0.0f, 4.0f, 20.0f}});
    note.setWrapped(true);
    note.apply({.fillX = true, .margin = {10.0f, 20.0f, 0.0f, 20.0f}});
    this->show(now);
  }
  void show(const config::sending_settings& now) {
    strip.parts.toggle.setOnNow(now.strip_metadata);
    rename.parts.toggle.setOnNow(now.rename);
  }
  void show_motion(std::string_view) {}
  void show_receipts(bool) {}
  void forEachChild(auto&& f) {
    f(header);
    f(title);
    f(strip);
    f(rename);
    f(note);
  }
};

}  // namespace mux::ui
