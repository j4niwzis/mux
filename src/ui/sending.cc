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
import :controls;
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
// The files about to be sent, shown before they go.
struct send_facts {
  std::vector<pending_file> files;
};
template <class Actions>
struct send_box : nodes::Stack {
  // Sized as it is opened, by the files it is opened with.
  [[nodiscard]] static dialog_look look_of_dialog() { return {}; }
  struct previews_column : nodes::Stack {
    // A picture to be sent, as it will look: rounded, its thumbnail by its
    // local id.
    struct picture_preview : nodes::Image<from_thumbnails> {
      std::string key;
      picture_preview(std::string k, float width, float height)
          : nodes::Image<from_thumbnails>({k}), key(std::move(k)) {
        fState.apply({.width = width, .height = height, .alignSelf = scene::align::kMiddle, .cornerRadius = 10.0f});
      }
    };
    struct parts_t {
      std::vector<picture_preview> pictures;
      std::vector<file_view> files;
    } parts;
    static constexpr float kGap = 8.0f;
    // A picture's size in the box: its own, scaled down to fit -- lower
    // where there are several.
    [[nodiscard]] static std::pair<float, float> size_of(const pending_file& one, std::size_t count) {
      const skia::Sp<skia::SkImage>* image = thumbnails().find(one.key);
      const float w = image && *image ? static_cast<float>((*image)->width()) : 380.0f;
      const float h = image && *image ? static_cast<float>((*image)->height()) : 240.0f;
      const float scale = std::min({1.0f, 380.0f / w, (count > 1 ? 160.0f : 300.0f) / h});
      return {w * scale, h * scale};
    }
    // As high as what it holds: its pictures' heights and its files' rows,
    // with the gaps between -- so the box is as high as it needs.
    [[nodiscard]] static float height_of(const std::vector<pending_file>& all) {
      float total = all.empty() ? 0.0f : static_cast<float>(all.size() - 1) * kGap;
      for (const pending_file& one : all)
        total += one.image ? size_of(one, all.size()).second : file_view::kIcon;
      return total;
    }
    previews_column(const palette& colours, const std::vector<pending_file>& all) {
      this->setGap(kGap);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY});
      for (const pending_file& one : all) {
        if (one.image) {
          const auto [w, h] = size_of(one, all.size());
          parts.pictures.emplace_back(one.key, w, h);
        } else {
          parts.files.emplace_back(colours, nullptr, std::string(), one.name, one.size);
        }
      }
    }
  };
  using buttons_row = dialog_buttons<sends<::mux::ui::request::close_send_box>, sends<::mux::ui::request::send_files>>;
  // What a box of these says it sends.
  [[nodiscard]] static std::string title_of(const std::vector<pending_file>& all) {
    return all.size() == 1 ? std::string(all.front().image ? "Send a photo" : "Send a file")
                           : std::format("Send {} {}", all.size(),
                                         std::ranges::all_of(all, &pending_file::image) ? "photos" : "files");
  }
  // How high the previews go before they scroll: tdesktop's
  // sendMediaPreviewHeightMax is 1280, more than a window; this keeps the
  // caption and buttons in view on a laptop's.
  static constexpr float kMostPreviews = 420.0f;
  struct parts_t {
    nodes::Text title;
    nodes::ScrollContainer<previews_column> previews;
    widgets::TextArea<> caption;
    buttons_row buttons;
  } parts;

  send_box(const ui_needs<Actions>& n, const send_facts& facts) : send_box(*n.colours, facts.files) {}
  send_box(const ui_needs<Actions>& n, const std::vector<pending_file>& all) : send_box(*n.colours, all) {}
  send_box(const palette& colours, const std::vector<pending_file>& all)
      : parts{.title = nodes::Text(title_of(all), 17.0f, colours.text, true),
              .previews = nodes::ScrollContainer<previews_column>(previews_column(colours, all)),
              .caption = widgets::TextArea<>(colours.widgets, "Add a caption…"),
              .buttons = buttons_row(colours, "Send", {}, {})} {
    this->setGap(12.0f);
    // Sized by what it holds, not by the window: the dialog fits it (up to
    // most of the window, the previews scrolling past what fits of them).
    fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {18.0f, 20.0f, 16.0f, 20.0f}});
    parts.previews.apply({.fillX = true, .height = std::min(previews_column::height_of(all), kMostPreviews)});
    parts.caption.apply({.fillX = true});
  }
};

}  // namespace mux::ui
