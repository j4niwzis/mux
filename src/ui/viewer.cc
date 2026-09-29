// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:viewer -- The picture viewer.
export module mux.ui:viewer;

import std;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes;
import skiff.widgets;
import mux.core;
import mux.config;
export import :sending;

export namespace mux::ui {

// A picture seen whole, as tdesktop's media viewer -- but over this window,
// not in one of its own: dark behind; at the top, who sent it and when, and
// the buttons -- zoom out and in, save, close; the picture fitted in the
// rest, zoomed by the wheel or the buttons, dragged about when larger than
// the room. A press on the dark around it closes it; the whole picture
// replaces its thumbnail when it has come.
template <class Actions>
struct picture_viewer : nodes::Stack {
  Actions* actions = nullptr;
  std::string source;
  // The viewer's own buttons act on it; the rest on the program.
  struct zoom_by {
    picture_viewer* viewer;
    float factor;
    void operator()() const { viewer->zoom_to(viewer->zoom * factor); }
  };
  struct save_it {
    Actions* actions;
    std::string source;
    void operator()() const { actions->save_picture(source); }
  };
  struct top_bar : nodes::Stack {
    avatar_mark face;
    two_lines texts;
    nodes::Box<> gap{skia::colorSetARGB(0, 0, 0, 0)};
    icon_button<zoom_by> smaller;
    icon_button<zoom_by> larger;
    icon_button<save_it> save;
    icon_button<ask<Actions, &Actions::close_picture>> close;
    top_bar(Actions* a, picture_viewer* viewer, const std::string& source, const std::string& sender,
            const std::string& name, const std::string& when)
        : face(sender, name, 36.0f), texts(name, when, 14.0f, 2.0f), smaller(icon::minus{}, {viewer, 1.0f / 1.25f}),
          larger(icon::plus{}, {viewer, 1.25f}), save(icon::send{}, {a, source}), close(icon::close{}, {a}) {
      this->setHorizontal();
      this->setGap(8.0f);
      fState.apply({.fillX = true, .height = 56.0f, .padding = {0.0f, 12.0f, 0.0f, 16.0f}});
      texts.name.setColour(skia::colorSetARGB(255, 255, 255, 255));
      texts.state.setColour(skia::colorSetARGB(255, 200, 200, 200));
      gap.apply({.height = 1.0f, .grow = scene::axes::kX});
      for (auto* button : {&smaller.colour, &larger.colour, &close.colour})
        *button = skia::colorSetARGB(255, 255, 255, 255);
      save.set_colour(skia::colorSetARGB(255, 255, 255, 255));
      for (scene::Node* middle : std::initializer_list<scene::Node*>{&smaller, &larger, &save, &close})
        middle->apply({.alignSelf = scene::align::kMiddle});
    }
    void forEachChild(auto&& f) {
      f(face);
      f(texts);
      f(gap);
      f(smaller);
      f(larger);
      f(save);
      f(close);
    }
  } top;
  // Where the picture is drawn: fitted, zoomed, moved.
  struct stage : scene::Node {
    picture_viewer* viewer;
    // The picture: the whole one where it has come, its thumbnail until then.
    struct parts_t {
      nodes::Image picture;
    } parts;
    explicit stage(picture_viewer* v)
        : viewer(v), parts{.picture = nodes::Image([v] {
                            const skia::Sp<skia::SkImage>* one = whole_pictures().find(v->source);
                            return one && *one ? one : thumbnails().find(v->source);
                          })} {
      fState.apply({.fillX = true, .grow = scene::axes::kY, .masking = true});
    }
    // Where the picture goes: fitted, zoomed, moved -- laid out there, not
    // drawn there by hand.
    void layoutChildren() {
      const skia::SkRect at = this->where();
      const skia::SkRect& box = fState.fBounds;
      parts.picture.apply({.x = at.fLeft - box.fLeft, .y = at.fTop - box.fTop, .width = at.width(), .height = at.height()});
      scene::layoutChildrenInContentBox(*this);
    }
    [[nodiscard]] const skia::Sp<skia::SkImage>* image() const {
      const skia::Sp<skia::SkImage>* one = whole_pictures().find(viewer->source);
      if (!one || !*one)
        one = thumbnails().find(viewer->source);
      return one && *one ? one : nullptr;
    }
    [[nodiscard]] skia::SkRect where() const {
      const skia::Sp<skia::SkImage>* one = this->image();
      const skia::SkRect& box = fState.fBounds;
      if (!one)
        return skia::SkRect::MakeEmpty();
      const float w = static_cast<float>((*one)->width()), h = static_cast<float>((*one)->height());
      const float fit = std::min({1.0f, (box.width() - 48.0f) / w, (box.height() - 48.0f) / h});
      const float scale = fit * viewer->zoom;
      return skia::SkRect::MakeXYWH(box.centerX() - w * scale * 0.5f + viewer->pan_x,
                                    box.centerY() - h * scale * 0.5f + viewer->pan_y, w * scale, h * scale);
    }
    [[nodiscard]] bool acceptsInput() const { return true; }
    [[nodiscard]] bool onScroll(float ticks) {
      viewer->zoom_to(viewer->zoom * std::pow(1.25f, ticks));
      return true;
    }
    bool dragging = false;
    float last_x = 0.0f, last_y = 0.0f;
    using Node::onPointer;
    void onPointer(scene::phase::target, const scene::pointer::down& press, scene::PointerReply& reply) {
      // Off the picture: closed. On it, and larger than the room: dragged.
      if (!this->where().contains(press.x, press.y)) {
        viewer->actions->close_picture();
        reply.handle();
        return;
      }
      dragging = true;
      last_x = press.x;
      last_y = press.y;
      reply.capturePointer();
      reply.handle();
    }
    void onPointer(scene::phase::target, const scene::pointer::move& at, scene::PointerReply& reply) {
      if (!dragging || viewer->zoom <= 1.0f)
        return;
      viewer->pan_x += at.x - last_x;
      viewer->pan_y += at.y - last_y;
      last_x = at.x;
      last_y = at.y;
      this->invalidateLayout();
      reply.handle();
    }
    void onPointer(scene::phase::target, const scene::pointer::up&, scene::PointerReply& reply) {
      if (dragging) {
        dragging = false;
        reply.releasePointer();
      }
    }
  } view;
  float zoom = 1.0f;
  float pan_x = 0.0f, pan_y = 0.0f;
  void zoom_to(float wanted) {
    zoom = std::clamp(wanted, 1.0f, 8.0f);
    if (zoom == 1.0f)
      pan_x = pan_y = 0.0f;
    view.invalidateLayout();
  }

  picture_viewer(Actions* a, std::string where, std::string sender, std::string name, std::string when)
      : actions(a), source(std::move(where)), top(a, this, source, sender, name, when), view(this) {
    fState.apply({.fill = true});
  }
  void forEachChild(auto&& f) {
    f(top);
    f(view);
  }
  void drawSelf(skia::SkCanvas* canvas, float alpha) {
    skia::SkPaint dim;
    dim.setColor(skia::colorSetARGB(0xe6, 0, 0, 0));
    dim.setAlphaf(dim.getAlphaf() * alpha);
    canvas->drawRect(fState.fBounds, dim);
  }
  [[nodiscard]] bool acceptsInput() const { return true; }
};

}  // namespace mux::ui
