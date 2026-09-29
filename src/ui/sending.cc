// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:sending -- Files being sent.
export module mux.ui:sending;

import std;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes;
import skiff.widgets;
import mux.core;
import mux.config;
export import :context_menu;

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
  nodes::Text title;
  struct previews_column : nodes::Stack {
    struct picture_preview : scene::Node {
      std::string key;
      picture_preview(std::string k, float width, float height) : key(std::move(k)) {
        fState.apply({.width = width, .height = height, .alignSelf = scene::align::kMiddle});
      }
      void drawSelf(skia::SkCanvas* canvas, float alpha) {
        const skia::SkRect& box = fState.fBounds;
        const int saved = canvas->save();
        canvas->clipRRect(skia::SkRRect::MakeRectXY(box, 10.0f, 10.0f), true);
        if (const skia::Sp<skia::SkImage>* image = thumbnails().find(key); image && *image) {
          skia::SkPaint paint;
          paint.setAlphaf(alpha);
          canvas->drawImageRect(*image, box, skia::SkSamplingOptions(skia::SkFilterMode::kLinear), &paint);
        }
        canvas->restoreToCount(saved);
      }
    };
    std::vector<picture_preview> pictures;
    std::vector<file_view> files;
    explicit previews_column(const std::vector<pending_file>& all) {
      this->setGap(8.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY});
      for (const pending_file& one : all) {
        if (one.image) {
          const skia::Sp<skia::SkImage>* image = thumbnails().find(one.key);
          float w = image && *image ? static_cast<float>((*image)->width()) : 380.0f;
          float h = image && *image ? static_cast<float>((*image)->height()) : 240.0f;
          const float scale = std::min({1.0f, 380.0f / w, (all.size() > 1 ? 160.0f : 300.0f) / h});
          pictures.emplace_back(one.key, w * scale, h * scale);
        } else {
          files.emplace_back(std::string(), one.name, one.size);
        }
      }
    }
    void forEachChild(auto&& f) {
      f(pictures);
      f(files);
    }
  };
  nodes::ScrollContainer<previews_column> previews;
  widgets::TextArea<> caption{"Add a caption…"};
  struct buttons_row : nodes::Stack {
    widgets::Button<ask<Actions, &Actions::close_send_box>> cancel;
    widgets::Button<ask<Actions, &Actions::send_files>> send;
    explicit buttons_row(Actions* a) : cancel("Cancel", {a}), send("Send", {a}) {
      this->setHorizontal();
      this->setGap(8.0f);
      fStack.justify = nodes::justify::end{};
      fState.apply({.fillX = true, .autoSize = scene::axes::kY});
      send.setPrimary(true);
      cancel.apply({.width = 96.0f, .height = 36.0f});
      send.apply({.width = 96.0f, .height = 36.0f});
    }
    void forEachChild(auto&& f) {
      f(cancel);
      f(send);
    }
  } buttons;

  send_box(Actions* a, const std::vector<pending_file>& all)
      : title(all.size() == 1 ? (all.front().image ? "Send a photo" : "Send a file")
                              : std::format("Send {} {}", all.size(),
                                            std::ranges::all_of(all, &pending_file::image) ? "photos" : "files"),
              17.0f, text_colour, true),
        previews(previews_column(all)), buttons(a) {
    this->setGap(12.0f);
    fState.apply({.fill = true, .padding = {18.0f, 20.0f, 16.0f, 20.0f}});
    previews.apply({.fillX = true, .grow = scene::axes::kY});
    caption.apply({.fillX = true});
  }
  void forEachChild(auto&& f) {
    f(title);
    f(previews);
    f(caption);
    f(buttons);
  }
};

}  // namespace mux::ui
