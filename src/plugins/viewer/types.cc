// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.viewer.types -- What the media viewer plugin names, and what it
// says it provides: a picture or a video seen whole, over the window -- a
// layer of it, what it has coming, and a part of the program. Light:
// mux.ui sees it.
export module mux.plugin.viewer.types;

import std;
import mux.core;
import mux.composition;

export namespace mux::plugins::viewer {

struct feature {
  static constexpr std::string_view name = "viewer";
};

// A picture or a video open: its source (a video's thumbnail), who sent
// it, its name, when; and a video's own source, played once it is there.
struct viewer_facts {
  std::string source;
  std::string sender;
  std::string name;
  std::string when;
  std::string video;
  // The video's file, once it is there: played.
  std::optional<std::filesystem::path> file;
};
// What is coming, as the program has it: the whole picture, or a video's
// file -- how far, and whether its download was stopped.
struct viewer_load {
  bool coming = false;
  std::optional<float> progress;
  bool stopped = false;
  friend bool operator==(const viewer_load&, const viewer_load&) = default;
};
// A video playing in the viewer, as the program plays it: its frame (a
// number, newer as one comes), where it is, how long.
struct video_shown {
  std::string source;
  bool playing = false;
  double at = 0.0, length = 0.0;
  std::uint64_t frame = 0;
  friend bool operator==(const video_shown&, const video_shown&) = default;
};
// Both, as the window keeps them for the viewer.
struct viewer_progress {
  viewer_load load;
  video_shown video;
  friend bool operator==(const viewer_progress&, const viewer_progress&) = default;
};
// Asked of the program by the viewer: the video paused or played on, gone
// to a share of it; its loader pressed (a picture's is the client's own,
// ui::request::press_loader). Closed by ui::request::close_shown<viewer_facts>.
struct toggle_video {};
struct video_seek {
  float share = 0.0f;
};
struct press_video_loader {
  std::string video;
};

consteval bool provides(contract::window_layers, type_tag<feature>) { return true; }
constexpr type_tag<std::optional<viewer_facts>> facts_type(contract::window_layers, type_tag<feature>) { return {}; }
consteval bool provides(contract::window_state, type_tag<feature>) { return true; }
constexpr type_tag<viewer_progress> state_type(contract::window_state, type_tag<feature>) { return {}; }
consteval bool provides(contract::program_parts, type_tag<feature>) { return true; }

}  // namespace mux::plugins::viewer
