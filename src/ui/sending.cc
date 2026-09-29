// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:sending -- Files being sent.
export module mux.ui:sending;

import std;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes.flow;
import skiff.nodes.image;
import skiff.nodes.scroll;
import skiff.nodes.text;
import skiff.widgets.button;
import skiff.widgets.textarea;
import mux.core;
import mux.config;
import :base;
import :avatars;
import :message;
import :context_menu;

export namespace mux::ui {

// What is about to be sent, as tdesktop's send box shows it: its title, a
// picture scaled to the box or a file's row for each, a caption, and Cancel
// and Send.
struct pending_file {
  std::string name;
  std::string key;  // its picture's, where it is one: its local id, in thumbnails()
  std::int64_t size = 0;
  bool image = false;
};
template <class Actions>
struct send_box : nodes::Stack {
  struct previews_column : nodes::Stack {
    // A picture to be sent, as it will look: rounded, its thumbnail by its
    // local id.
    struct picture_preview : nodes::Image {
      std::string key;
      picture_preview(std::string k, float width, float height)
          : nodes::Image([k] { return thumbnails().find(k); }), key(std::move(k)) {
        fState.apply({.width = width, .height = height, .alignSelf = scene::align::kMiddle, .cornerRadius = 10.0f});
      }
    };
    struct parts_t {
      std::vector<picture_preview> pictures;
      std::vector<file_view> files;
    } parts;
    explicit previews_column(const std::vector<pending_file>& all) {
      this->setGap(8.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY});
      for (const pending_file& one : all) {
        if (one.image) {
          const skia::Sp<skia::SkImage>* image = thumbnails().find(one.key);
          float w = image && *image ? static_cast<float>((*image)->width()) : 380.0f;
          float h = image && *image ? static_cast<float>((*image)->height()) : 240.0f;
          const float scale = std::min({1.0f, 380.0f / w, (all.size() > 1 ? 160.0f : 300.0f) / h});
          parts.pictures.emplace_back(one.key, w * scale, h * scale);
        } else {
          parts.files.emplace_back(std::string(), one.name, one.size);
        }
      }
    }
  };
  struct buttons_row : nodes::Stack {
    using cancel_button = widgets::Button<ask<Actions, &Actions::close_send_box>>;
    using send_button = widgets::Button<ask<Actions, &Actions::send_files>>;
    struct parts_t {
      cancel_button cancel;
      send_button send;
    } parts;
    explicit buttons_row(Actions* a) : parts{.cancel = cancel_button("Cancel", {a}), .send = send_button("Send", {a})} {
      this->setHorizontal();
      this->setGap(8.0f);
      fStack.justify = nodes::justify::end{};
      fState.apply({.fillX = true, .autoSize = scene::axes::kY});
      parts.send.setPrimary(true);
      parts.cancel.apply({.width = 96.0f, .height = 36.0f});
      parts.send.apply({.width = 96.0f, .height = 36.0f});
    }
  };
  // What a box of these says it sends.
  [[nodiscard]] static std::string title_of(const std::vector<pending_file>& all) {
    return all.size() == 1 ? std::string(all.front().image ? "Send a photo" : "Send a file")
                           : std::format("Send {} {}", all.size(),
                                         std::ranges::all_of(all, &pending_file::image) ? "photos" : "files");
  }
  struct parts_t {
    nodes::Text title;
    nodes::ScrollContainer<previews_column> previews;
    widgets::TextArea<> caption{"Add a caption…"};
    buttons_row buttons;
  } parts;

  send_box(Actions* a, const std::vector<pending_file>& all)
      : parts{.title = nodes::Text(title_of(all), 17.0f, text_colour, true),
              .previews = nodes::ScrollContainer<previews_column>(previews_column(all)),
              .buttons = buttons_row(a)} {
    this->setGap(12.0f);
    fState.apply({.fill = true, .padding = {18.0f, 20.0f, 16.0f, 20.0f}});
    parts.previews.apply({.fillX = true, .grow = scene::axes::kY});
    parts.caption.apply({.fillX = true});
  }
};

}  // namespace mux::ui
