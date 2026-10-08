// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:viewer -- The picture viewer.
export module mux.ui:viewer;

import std;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes.box;
import skiff.nodes.flow;
import skiff.nodes.image;
import skiff.widgets.loader;
import mux.platform.video;
import mux.core;
import mux.config;
import :base;
import :icons;
import :avatars;
import :controls;

export namespace mux::ui {

// A picture seen whole, as tdesktop's media viewer -- but over this window,
// not in one of its own: dark behind; at the top, who sent it and when, and
// the buttons -- zoom out and in, save, close; the picture fitted in the
// rest, zoomed by the wheel or the buttons, dragged about when larger than
// the room. A press on the dark around it closes it; the whole picture
// replaces its thumbnail when it has come.
template <class Actions>
struct picture_viewer : nodes::Stack, outbox {
  // The colours its menu is made in.
  const palette* colours_ = nullptr;
  std::string source;
  // A video's: its file's source, and it playing once its file is here --
  // its picture shown in place of the thumbnail, a bar under it.
  std::optional<std::string> video;
  std::unique_ptr<mux::platform::video::player> playing;
  void start(const std::filesystem::path& file) {
    playing = mux::platform::video::player::open(file);
    parts.bar.setVisible(playing != nullptr);
    this->invalidateLayout();
  }
  // The viewer's own buttons act on it; the rest on the program.
  struct zoom_by {
    picture_viewer* viewer;
    float factor;
    void operator()() const { viewer->zoom_to(viewer->zoom * factor); }
  };
  struct save_it : outbox {
    std::string source;
    void operator()() { this->emit(::mux::ui::request::save_picture{source}); }
  };
  // The loader pressed: the download stopped, or started again.
  struct press_loader {
    picture_viewer* viewer;
    void operator()() const { viewer->emit(::mux::ui::request::press_loader{viewer->video.value_or(viewer->source)}); }
  };
  struct top_bar : nodes::Stack {
    using close_button = icon_button<sends<::mux::ui::request::close_picture>>;
    struct parts_t {
      avatar_mark face;
      two_lines texts;
      nodes::Box<> gap{skia::colorSetARGB(0, 0, 0, 0)};
      icon_button<zoom_by> smaller;
      icon_button<zoom_by> larger;
      icon_button<save_it> save;
      close_button close;
    } parts;
    top_bar(const palette& colours, picture_viewer* viewer, const std::string& source, const std::string& sender,
            const std::string& name, const std::string& when)
        : parts{.face = avatar_mark(sender, name, 36.0f),
                .texts = two_lines(colours, name, when, 14.0f, 2.0f),
                .smaller = icon_button<zoom_by>(colours, icon::minus{}, {viewer, 1.0f / 1.25f}),
                .larger = icon_button<zoom_by>(colours, icon::plus{}, {viewer, 1.25f}),
                .save = icon_button<save_it>(colours, icon::download{}, {{}, source}),
                .close = close_button(colours, icon::close{}, {})} {
      this->setHorizontal();
      this->setGap(8.0f);
      fState.apply({.fillX = true, .height = 56.0f, .padding = {0.0f, 12.0f, 0.0f, 16.0f}});
      parts.texts.parts.name.setColour(skia::colorSetARGB(255, 255, 255, 255));
      parts.texts.parts.state.setColour(skia::colorSetARGB(255, 200, 200, 200));
      parts.gap.apply({.height = 1.0f, .grow = scene::axes::kX});
      // White on the dark of the viewer.
      for (auto* white : {&parts.smaller, &parts.larger})
        white->set_colour(skia::colorSetARGB(255, 255, 255, 255));
      parts.close.set_colour(skia::colorSetARGB(255, 255, 255, 255));
      parts.save.set_colour(skia::colorSetARGB(255, 255, 255, 255));
      for (scene::Node* middle : std::initializer_list<scene::Node*>{&parts.smaller, &parts.larger, &parts.save, &parts.close})
        middle->apply({.alignSelf = scene::align::kMiddle});
    }
  };
  // Where the picture is drawn: fitted, zoomed, moved.
  struct stage : scene::Node {
    picture_viewer* viewer;
    // The picture: the whole one where it has come, its thumbnail until then.
    struct parts_t {
      // The picture shown: whole where it has come, else its thumbnail.
      struct shown_picture {
        picture_viewer* viewer;
        const skia::Sp<skia::SkImage>* operator()() const {
          if (viewer->playing && viewer->playing->picture())
            return &viewer->playing->picture();
          const skia::Sp<skia::SkImage>* one = whole_pictures().find(viewer->source);
          return one && *one ? one : thumbnails().find(viewer->source);
        }
      };
      nodes::Image<shown_picture> picture;
      widgets::RadialLoader<press_loader> loader;  // while the whole picture is coming
    } parts;
    explicit stage(picture_viewer* v)
        : viewer(v), parts{.picture = nodes::Image<typename parts_t::shown_picture>({v}),
                           .loader = widgets::RadialLoader<press_loader>(44.0f, {v})} {
      fState.apply({.fillX = true, .grow = scene::axes::kY, .masking = true});
      parts.loader.apply({.place = scene::anchor::kCentre});
    }
    // A video playing: frames while it plays.
    [[nodiscard]] bool settling() const { return viewer->playing && !viewer->playing->paused(); }
    void update(double now) {
      if (viewer->playing) {
        const bool first = !viewer->playing->picture();
        if (viewer->playing->advance(now)) {
          parts.picture.markDamaged();
          if (first)
            this->invalidateLayout();  // its own size, not the thumbnail's
        }
        viewer->parts.bar.show(*viewer->playing);
      }
      // What is coming: the whole picture, or the video's file.
      const std::string& loading = viewer->video.value_or(viewer->source);
      const bool coming = viewer->video ? !viewer->playing : !whole_pictures().has(viewer->source);
      if (coming != parts.loader.visible())
        parts.loader.setVisible(coming);
      if (coming) {
        parts.loader.setProgress(progress_of(loading));
        parts.loader.setStopped(stopped_downloads().contains(loading));
      }
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
      if (viewer->playing && viewer->playing->picture())
        return &viewer->playing->picture();
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
      // Its menu up: a press anywhere else closes it, and does nothing more.
      if (viewer->parts.menu) {
        viewer->close_menu_later();
        reply.handle();
        return;
      }
      // The other button on the picture: its menu, there.
      if (press.button == 3) {
        if (this->where().contains(press.x, press.y)) {
          const skia::SkRect& whole = viewer->fState.fBounds;
          viewer->parts.menu.emplace(viewer, press.x - whole.fLeft, press.y - whole.fTop);
          viewer->invalidateLayout();
        }
        reply.handle();
        return;
      }
      // Off the picture: closed. On it, and larger than the room: dragged.
      if (!this->where().contains(press.x, press.y)) {
        viewer->emit(::mux::ui::request::close_picture{});
        reply.handle();
        return;
      }
      // On a video playing: paused, or played on.
      if (viewer->playing) {
        viewer->playing->toggle();
        this->markDamaged();
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
  };
  // Under a video: how far it has played, a bar pressed to go elsewhere in
  // it, and its time, as tdesktop's player.
  struct video_bar : nodes::Stack {
    picture_viewer* viewer;
    struct track_t : nodes::Stack {
      struct parts_t {
        nodes::Box<> played{skia::colorSetARGB(255, 255, 255, 255)};
      } parts;
      track_t() {
        this->setHorizontal();
        fState.apply({.height = 4.0f, .grow = scene::axes::kX, .alignSelf = scene::align::kMiddle, .cornerRadius = 2.0f,
                      .background = skia::colorSetARGB(0x60, 255, 255, 255)});
        parts.played.apply({.fillY = true, .width = 0.0f, .cornerRadius = 2.0f});
      }
    };
    struct parts_t {
      track_t track;
      nodes::Text time{"", 13.0f, skia::colorSetARGB(255, 255, 255, 255)};
    } parts;
    explicit video_bar(picture_viewer* v) : viewer(v) {
      this->setHorizontal();
      this->setGap(12.0f);
      fState.apply({.fillX = true, .height = 44.0f, .padding = {0.0f, 20.0f, 0.0f, 20.0f}});
      parts.time.apply({.alignSelf = scene::align::kMiddle});
      this->setVisible(false);
    }
    [[nodiscard]] static std::string clock_of(double seconds) {
      const auto whole = static_cast<std::int64_t>(seconds);
      return std::format("{}:{:02}", whole / 60, whole % 60);
    }
    void show(const mux::platform::video::player& one) {
      const double length = one.length();
      const float share = length > 0.0 ? static_cast<float>(one.position() / length) : 0.0f;
      const float width = std::floor(parts.track.bounds().width() * share);
      if (std::abs(width - parts.track.parts.played.fState.fWidth) >= 1.0f) {
        parts.track.parts.played.apply({.width = width});
        parts.track.invalidateLayout();
      }
      if (std::string now = clock_of(one.position()) + " / " + clock_of(length); now != parts.time.text())
        parts.time.setText(std::move(now));
    }
    [[nodiscard]] bool acceptsInput() const { return true; }
    // A press on the bar: there in the video.
    using Node::onPointer;
    void onPointer(scene::phase::target, const scene::pointer::down& press, scene::PointerReply& reply) {
      const skia::SkRect track = parts.track.bounds();
      if (!viewer->playing || track.width() <= 0.0f)
        return;
      const double share = std::clamp((press.x - track.fLeft) / track.width(), 0.0f, 1.0f);
      viewer->playing->seek(share * viewer->playing->length());
      reply.handle();
    }
  };
  // Its own menu, as a right press on the picture opens it there: the
  // picture copied, or saved. A press anywhere else closes it.
  struct copy_it {
    picture_viewer* viewer;
    void operator()() const { viewer->emit(::mux::ui::request::copy_picture{viewer->source}); }
  };
  struct save_this {
    picture_viewer* viewer;
    void operator()() const { viewer->emit(::mux::ui::request::save_picture{viewer->source}); }
  };
  template <class Do>
  struct menu_row : nodes::Stack {
    picture_viewer* viewer;
    Do act;
    struct parts_t {
      nodes::Text label;
    } parts;
    menu_row(picture_viewer* v, std::string label)
        : viewer(v), act{v}, parts{.label = nodes::Text(std::move(label), 13.0f, v->colours_->text)} {
      fState.apply({.fillX = true, .height = 33.0f, .padding = {0.0f, 17.0f, 0.0f, 17.0f}, .hoverBackground = v->colours_->chosen});
      parts.label.apply({.alignSelf = scene::align::kMiddle});
    }
    [[nodiscard]] bool acceptsInput() const { return true; }
    [[nodiscard]] bool hoverChangesAppearance() const { return true; }
    [[nodiscard]] bool onClick(float, float) {
      act();
      viewer->close_menu_later();
      return true;
    }
  };
  struct picture_menu : nodes::Stack {
    struct parts_t {
      menu_row<copy_it> copy;
      menu_row<save_this> save;
    } parts;
    picture_menu(picture_viewer* v, float x, float y)
        : parts{.copy = menu_row<copy_it>(v, "Copy Image"), .save = menu_row<save_this>(v, "Save As…")} {
      fState.apply({.place = scene::anchor::kTopLeft, .x = x, .y = y, .width = 200.0f, .autoSize = scene::axes::kY,
                    .padding = {6.0f, 0.0f, 6.0f, 0.0f}, .cornerRadius = 10.0f, .background = v->colours_->sidebar,
                    .border = scene::Border{v->colours_->band, 1.0f},
                    .shadow = scene::Shadow{skia::colorSetARGB(70, 0, 0, 0), 3.0f}});
    }
    [[nodiscard]] bool acceptsInput() const { return true; }
  };
  struct parts_t {
    top_bar top;
    stage view;
    video_bar bar;
    std::optional<picture_menu> menu;
  } parts;
  // The menu let go at the next frame -- not from inside one of its rows.
  bool menu_done = false;
  void close_menu_later() {
    menu_done = true;
    scene::work::mark(fState.fId);
  }
  [[nodiscard]] bool wantsTick() const { return menu_done; }
  void update(double) {
    if (!menu_done)
      return;
    menu_done = false;
    parts.menu.reset();
    this->invalidateLayout();
  }
  float zoom = 1.0f;
  float pan_x = 0.0f, pan_y = 0.0f;
  void zoom_to(float wanted) {
    zoom = std::clamp(wanted, 1.0f, 8.0f);
    if (zoom == 1.0f)
      pan_x = pan_y = 0.0f;
    parts.view.invalidateLayout();
  }

  picture_viewer(const ui_needs<Actions>& n, std::string where, std::string sender, std::string name, std::string when)
      : picture_viewer(n.colours, std::move(where), std::move(sender), std::move(name), std::move(when)) {}
  picture_viewer(const palette* colours, std::string where, std::string sender, std::string name, std::string when)
      : colours_(colours), source(std::move(where)),
        parts{.top = top_bar(*colours, this, source, sender, name, when), .view = stage(this), .bar = video_bar(this)} {
    fState.apply({.fill = true, .background = skia::colorSetARGB(0xe6, 0, 0, 0)});
  }
  [[nodiscard]] bool acceptsInput() const { return true; }
  // Space: a video paused, or played on.
  using Node::onKey;
  void onKey(scene::phase::bubble, const scene::key::down& press, scene::Reply& reply) {
    if (playing && press.key == scene::keys::kSpace) {
      playing->toggle();
      parts.view.markDamaged();
      reply.handle();
    }
  }
};

}  // namespace mux::ui
