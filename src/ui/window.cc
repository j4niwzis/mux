// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:window -- The window.
export module mux.ui:window;

import std;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes.box;
import skiff.widgets.motion;
import skiff.widgets.wallpaper;
import mux.core;
import mux.config;
import :base;
import :forms;
import :header;
import :info;
import :room_settings;
import :timeline;
import :conversations;
import :accounts;
import :drawer;
import :settings;
import :context_menu;
import :call_bar;
import :sending;
import :viewer;

export namespace mux::ui {

// ---- the window -------------------------------------------------------------------

// The conversations; over them the panel that is open, if one is, sliding in
// from the right and back out when closed; and over both, the drawer, pulled
// out from the left. All in this one window, switched by the program between
// events.
template <class Actions>
struct window : scene::Node {
  using panel_type = spl::variant<accounts_panel<Actions>>;
  using with_drawer = widgets::Drawer<conversations_screen<Actions>, drawer_panel<Actions>>;

  // What the window holds, made anew when the theme changes: what is made
  // takes its colours then. Its layers, bottom to top.
  // A text menu, where the pointer was pressed -- a right press, a long one
  // on a phone. A selectable text's: Copy, what it selected; and Copy Link,
  // where the press was on a link. A field's: Cut and Copy where something
  // is selected and it is no password's, Paste, Select All -- each the key
  // the field takes for it, given to it (it keeps the focus: a button takes
  // none).
  struct text_menu : nodes::Stack {
    struct copy_it {
      Actions* actions;
      std::string text;
      void operator()() const { actions->copy_text(text); }
    };
    struct key_it {
      Actions* actions;
      scene::Key key;
      bool shift = false;
      void operator()() const { actions->text_key(key, shift); }
    };
    struct parts_t {
      std::optional<widgets::Button<copy_it>> copy;
      std::optional<widgets::Button<copy_it>> copy_link;
      std::optional<widgets::Button<key_it>> cut;
      std::optional<widgets::Button<key_it>> copy_selected;
      std::optional<widgets::Button<key_it>> paste;
      std::optional<widgets::Button<key_it>> select_all;
      // A field that formats, with something selected: tdesktop's
      // Formatting items, each its shortcut given to the field.
      std::optional<widgets::Button<key_it>> bold;
      std::optional<widgets::Button<key_it>> italic;
      std::optional<widgets::Button<key_it>> underline;
      std::optional<widgets::Button<key_it>> strike;
      std::optional<widgets::Button<key_it>> monospace;
      std::optional<widgets::Button<key_it>> spoiler;
      std::optional<widgets::Button<key_it>> link;
      std::optional<widgets::Button<key_it>> plain;
    } parts;
    // A selectable text's.
    text_menu(const ui_needs<Actions>& n, std::string text, std::optional<std::string> link) : text_menu(*n.colours) {
      parts.copy.emplace(n.colours->widgets, "Copy", copy_it{n.actions, std::move(text)});
      if (link)
        parts.copy_link.emplace(n.colours->widgets, "Copy Link", copy_it{n.actions, std::move(*link)});
      this->rows();
    }
    // A field's.
    text_menu(const ui_needs<Actions>& n, const scene::text_menu::of_field& field) : text_menu(*n.colours) {
      const auto item = [&](std::optional<widgets::Button<key_it>>& button, std::string label, scene::Key key,
                            bool shift = false) {
        button.emplace(n.colours->widgets, std::move(label), key_it{n.actions, key, shift});
      };
      if (field.selection && !field.masked) {
        item(parts.cut, "Cut", scene::keys::kX);
        item(parts.copy_selected, "Copy", scene::keys::kC);
      }
      item(parts.paste, "Paste", scene::keys::kV);
      item(parts.select_all, "Select All", scene::keys::kA);
      if (field.formats && field.selection && !field.masked) {
        item(parts.bold, "Bold", scene::keys::kB);
        item(parts.italic, "Italic", scene::keys::kI);
        item(parts.underline, "Underline", scene::keys::kU);
        item(parts.strike, "Strikethrough", scene::keys::kX, true);
        item(parts.monospace, "Monospace", scene::keys::kM, true);
        item(parts.spoiler, "Spoiler", scene::keys::kP, true);
        item(parts.link, "Link", scene::keys::kK);
        item(parts.plain, "Plain text", scene::keys::kN, true);
      }
      this->rows();
    }
    // How tall it is, for where it is put: its rows and its padding.
    [[nodiscard]] float tall() const {
      const auto& [... row] = parts;
      return 12.0f + 34.0f * static_cast<float>((0 + ... + (row ? 1 : 0)));
    }

   private:
    explicit text_menu(const palette& colours) {
      fState.apply({.width = 150.0f, .autoSize = scene::axes::kY, .padding = {6.0f, 6.0f, 6.0f, 6.0f}, .cornerRadius = 10.0f,
                    .background = colours.popup(), .border = scene::Border{colours.band, 1.0f},
                    .shadow = scene::Shadow{skia::colorSetARGB(70, 0, 0, 0), 3.0f}});
    }
    void rows() {
      auto& [... row] = parts;
      ((row ? (row->apply({.fillX = true, .height = 30.0f}), 0) : 0), ...);
    }
  };
  // The dialogs protocols have of their own (dialogs(state), made by their
  // dialog_type, found by ADL where the window is made): one of them up at
  // a time, in one dialog of the window's.
  template <class List>
  struct dialog_nodes;
  template <class... Ds>
  struct dialog_nodes<proto::dialog_list<Ds...>> {
    using type = type_list<typename decltype(dialog_type(Ds{}, type_tag<Actions>{}))::type...>;
  };
  template <class>
  struct protocol_dialog_nodes;
  template <class... Tags>
  struct protocol_dialog_nodes<protocol_list<Tags...>> {
    using type = typename joined<type_list<>, typename dialog_nodes<decltype(proto::dialogs_of(::mux::state_of<Tags>{}))>::type...>::type;
  };
  // Never empty: a text, where no protocol has a dialog.
  using tool_t = typename variant_of_types<
      typename joined<type_list<nodes::Text>, typename protocol_dialog_nodes<protocols>::type>::type>::type;
  // The dialog's content, a node: the protocol's dialog in it, as the room
  // settings hold their page.
  struct tool_holder : nodes::Stack {
    // The dialog it is shown in.
    [[nodiscard]] static dialog_look look_of_dialog() { return {.size = dialog_size::fixed{560.0f, 560.0f}}; }
    struct parts_t {
      tool_t shown;
    } parts;
    template <class Node, class... Args>
    explicit tool_holder(std::in_place_type_t<Node> which, Args&&... args) : parts{.shown = tool_t(which, std::forward<Args>(args)...)} {
      fState.apply({.fillX = true, .autoSize = scene::axes::kY});
    }
  };
  struct layers : scene::Node {
    using frame_t = widgets::SlideOver<with_drawer, panel_type>;
    struct parts_t {
      nodes::Box<> backdrop;
      // The chat's background behind all of the window, where it is so:
      // drawn at its own opacity -- the desktop through it only where the
      // picture itself lets it be seen.
      wallpaper_t behind;
      // The pages slide over the drawer too: Manage accounts comes in over it.
      frame_t frame;
      widgets::Dialog<settings_dialog<Actions>> settings;
      widgets::Dialog<notice_box<Actions>> notice;
      // A person's info, in the middle, as tdesktop's profile layer.
      widgets::Dialog<person_card<Actions>> person;
      // A room not joined, from a link: its card, as a person's.
      widgets::Dialog<room_card<Actions>> room;
      // A message's reactions as events.
      widgets::Dialog<reactions_box<Actions>> reactions;
      // A message's earlier versions, as AyuGram's edit history.
      widgets::Dialog<edit_history_box<Actions>> history;
      // A link put on what is selected in the message field: Ctrl+K's.
      widgets::Dialog<link_box<Actions>> linking;
      // Leaving a space, and which of its rooms with it.
      widgets::Dialog<leave_space_box<Actions>> leaving;
      // The mentions or the reactions not yet seen, listed.
      widgets::Dialog<marks_box<Actions>> marks;
      // A room's management.
      widgets::Dialog<room_settings<Actions>> manage;
      // Where a message is forwarded to.
      widgets::Dialog<forward_box<Actions>> forwarding;
      // Element's Start chat, and its Create a room.
      widgets::Dialog<start_chat_box<Actions>> new_chat;
      widgets::Dialog<create_room_box<Actions>> new_room;
      // Emojis & Stickers: a room's packs, or one's own.
      widgets::Dialog<packs_box<Actions>> packs;
      // A chat background chosen, at a level.
      widgets::Dialog<wallpaper_box<Actions>> wallpaper;
      // A server's public rooms, searched.
      widgets::Dialog<explore_box<Actions>> explore;
      // A protocol's own dialog: Matrix's developer tools, for one.
      widgets::Dialog<tool_holder> tools;
      widgets::Dialog<send_box<Actions>> sending;
      // A passphrase asked for: at the start, where local data is encrypted;
      // or to turn that on or off, or change it. Over everything.
      widgets::Dialog<passphrase_box<Actions>> passphrase;
      // An emoji verification, as it goes.
      widgets::Dialog<verification_box<Actions>> verifying;
      std::optional<emoji_popup<Actions>> emoji;
      std::optional<context_menu<Actions>> menu;
      std::optional<picture_viewer<Actions>> viewer;
      // A selectable text's menu, where it was pressed with the right button.
      std::optional<text_menu> text_menu_up;
      // A call, while there is one: over everything.
      std::optional<call_bar<Actions>> call_up;
      // A call on a phone: the whole window, as Element's phone apps.
      std::optional<call_screen<Actions>> call_whole;
    } parts;

    Actions* actions_of = nullptr;
    // Where the pointer was last pressed, in the window: where a menu asked
    // by that press is put. A press off the text menu closes it, at once --
    // nothing of it is pressed.
    skia::SkPoint last_press{};
    // A press off the emoji popup, where it was: let go there -- a tap, not
    // a drag to scroll the chat under it -- it closes the popup.
    std::optional<skia::SkPoint> press_off_emoji;
    using Node::onPointer;
    void onPointer(scene::phase::capture, const scene::pointer::up& lift, scene::PointerReply&) {
      const auto off = std::exchange(press_off_emoji, std::nullopt);
      if (off && parts.emoji && std::hypot(lift.x - off->fX, lift.y - off->fY) < 8.0f)
        actions_of->close_emoji();
    }
    void onPointer(scene::phase::capture, const scene::pointer::down& press, scene::PointerReply&) {
      last_press = {press.x, press.y};
      press_off_emoji = parts.emoji && !parts.emoji->parts.card.bounds().contains(press.x, press.y)
                            ? std::optional<skia::SkPoint>(skia::SkPoint::Make(press.x, press.y))
                            : std::nullopt;
      if (parts.text_menu_up && !parts.text_menu_up->bounds().contains(press.x, press.y)) {
        parts.text_menu_up.reset();
        this->invalidateLayout();
        this->markDamaged();
      }
    }
    // Esc closes the emoji popup first, whatever has the keys: the input
    // keeps them while the popup is open, so the press comes down to it
    // through here -- caught on its way, before the chat reads Esc as
    // letting an answer go.
    using Node::onKey;
    // Esc closes what is on top, whatever has the keys -- a menu, the emoji
    // popup, a picture, a dialog -- one at a time, the topmost first.
    // Ctrl+C, where nothing under the keys took it: what any text shows
    // selected -- a dialog's, a notice's -- copied. The chat's own Ctrl+C
    // comes first, for its messages.
    void onKey(scene::phase::bubble, const scene::key::down& press, scene::Reply& reply) {
      if (press.key != scene::keys::kC || !press.modifiers.template has<scene::modifier::control>())
        return;
      if (const std::string selected = scene::selectedText(); !selected.empty()) {
        skiff::scene::setClipboardText(selected);
        reply.handle();
      }
    }
    void onKey(scene::phase::capture, const scene::key::down& press, scene::Reply& reply) {
      if (press.key != scene::keys::kEscape)
        return;
      Actions* a = actions_of;
      const auto closed = [&] { reply.handle(); };
      if (parts.text_menu_up) {
        parts.text_menu_up.reset();
        this->invalidateLayout();
        this->markDamaged();
        return closed();
      }
      if (parts.menu)
        return a->close_menu(), closed();
      if (parts.emoji)
        return a->close_emoji(), closed();
      if (parts.viewer)
        return a->close_picture(), closed();
      // A verification: OK where it is over, Decline or Cancel where it
      // waits. Not while the emoji are compared: an answer is asked there.
      if (auto* box = parts.verifying.shown()) {
        if (box->parts.close.visible())
          return a->close_verification(), closed();
        if (box->parts.decline.visible())
          return a->verify_cancel_now(), closed();
      }
      // The dialogs, the one drawn last -- on top -- first.
      if (parts.sending.shown())
        return a->close_send_box(), closed();
      if (parts.tools.shown())
        return a->close_dialog(), closed();
      if (parts.explore.shown())
        return a->close_explore(), closed();
      if (parts.wallpaper.shown())
        return a->close_wallpaper(), closed();
      if (parts.packs.shown())
        return a->close_packs(), closed();
      if (parts.new_room.shown())
        return a->close_new_room(), closed();
      if (parts.new_chat.shown())
        return a->close_new_chat(), closed();
      if (parts.forwarding.shown())
        return a->close_forward(), closed();
      if (parts.manage.shown())
        return a->close_manage(), closed();
      if (parts.marks.shown())
        return a->close_marks(), closed();
      if (parts.leaving.shown())
        return a->close_leave_space(), closed();
      if (parts.linking.shown())
        return a->close_link(), closed();
      if (parts.history.shown())
        return a->close_edit_history(), closed();
      if (parts.reactions.shown())
        return a->close_reactions(), closed();
      if (parts.room.shown())
        return a->close_room_card(), closed();
      if (parts.person.shown())
        return a->close_person_info(), closed();
      if (parts.notice.shown())
        return a->close_notice(), closed();
      // Settings: a page back to where its ← goes; home, closed.
      if (auto* box = parts.settings.shown()) {
        if (box->step_back())
          return closed();
        return a->close_settings(), closed();
      }
      // The drawer, under every dialog.
      if (parts.frame.base().isOpen())
        return parts.frame.base().close(), closed();
    }

    // While a dialog fades in or out, what is under it -- the window's
    // background, its wallpaper, the chats and the messages -- drawn once,
    // as the fade sets off, into pixels kept: each frame of the fade puts
    // those down and draws only the dialogs and what floats over them, its
    // one blur taken from them (the user's, #13520). Every frame of a fade
    // repainted all of the window under its scrim, and blurred it again.
    // Done, it is let go of: the frame the fade ends on draws all of it as
    // it is, whatever changed under it meanwhile.
    skia::Sp<skia::SkImage> frozen;
    skia::SkRect frozen_at = skia::SkRect::MakeEmpty();  // where it is on the device

    [[nodiscard]] bool dialog_fading() {
      auto& [backdrop, behind, frame, settings, notice, person, room, reactions, history, linking, leaving, marks, manage, forwarding, new_chat, new_room, packs, wallpaper, explore, tools, sending, passphrase, verifying, emoji, menu, viewer, text_menu_up, call_up, call_whole] = parts;
      return settings.settling() || notice.settling() || person.settling() || room.settling() || reactions.settling() ||
             history.settling() || linking.settling() || leaving.settling() ||
             marks.settling() || manage.settling() || forwarding.settling() || new_chat.settling() ||
             new_room.settling() || packs.settling() || wallpaper.settling() || explore.settling() ||
             tools.settling() || sending.settling() || passphrase.settling() || verifying.settling();
    }
    void draw(skiff::scene::Painting& painting, skia::SkCanvas* canvas, float alpha) {
      auto& [backdrop, behind, frame, settings, notice, person, room, reactions, history, linking, leaving, marks, manage, forwarding, new_chat, new_room, packs, wallpaper, explore, tools, sending, passphrase, verifying, emoji, menu, viewer, text_menu_up, call_up, call_whole] = parts;
      skia::SkMatrix inverse;
      if (!this->dialog_fading() || !canvas->getTotalMatrix().invert(&inverse)) {
        frozen = nullptr;
        scene::drawDefault(*this, painting, canvas, alpha);
        return;
      }
      if (!frozen) {
        const skia::SkRect device = canvas->getTotalMatrix().mapRect(fState.fBounds);
        const int width = std::max(1, static_cast<int>(std::ceil(device.width())));
        const int height = std::max(1, static_cast<int>(std::ceil(device.height())));
        skia::SkBitmap pixels;
        if (pixels.tryAllocN32Pixels(width, height)) {
          pixels.eraseColor(0);
          skia::SkCanvas into(pixels);
          into.translate(-device.fLeft, -device.fTop);
          into.concat(canvas->getTotalMatrix());
          scene::draw(backdrop, painting, &into, alpha);
          scene::draw(behind, painting, &into, alpha);
          scene::draw(frame, painting, &into, alpha);
          frozen = pixels.asImage();
          frozen_at = skia::SkRect::MakeXYWH(device.fLeft, device.fTop, static_cast<float>(width), static_cast<float>(height));
        }
      }
      if (!frozen) {
        scene::drawDefault(*this, painting, canvas, alpha);
        return;
      }
      canvas->drawImageRect(frozen, inverse.mapRect(frozen_at), skia::SkSamplingOptions(skia::SkFilterMode::kNearest));
      const auto over = [&](auto&... each) { (scene::draw(each, painting, canvas, alpha), ...); };
      over(settings, notice, person, room, reactions, history, linking, leaving, marks, manage, forwarding, new_chat, new_room, packs, wallpaper, explore,
           tools, sending, passphrase, verifying);
      const auto over_if = [&](auto&... each) { ((each ? scene::draw(*each, painting, canvas, alpha) : void()), ...); };
      over_if(emoji, menu, viewer, text_menu_up, call_up, call_whole);
    }

    explicit layers(const ui_needs<Actions>& n) : layers(n, n.actions) {}
    layers(const ui_needs<Actions>& n, Actions* a)
        : parts{.backdrop = nodes::Box<>(n.colours->background),
                .frame = frame_t(std::piecewise_construct, std::forward_as_tuple(n), std::forward_as_tuple(n))},
          actions_of(a) {
      auto& [backdrop, behind, frame, ...over] = parts;
      fState.apply({.fill = true});
      backdrop.apply({.fill = true});
      behind.apply({.fill = true});
      behind.setVisible(n.looks->window.behind);
      // The pages over the chats (Accounts) on the panels' colour: as
      // see-through as the panels are.
      frame.setSheetColour(n.colours->sidebar);
      frame.base().setSheetColour(n.colours->sidebar);
      // Each dialog as what it shows declares it.
      (look_as_its_content(over, *n.colours), ...);
    }
  };

  Actions* actions = nullptr;
  struct parts_t {
    std::optional<layers> now;
  } parts;
  // The layers as they are.
  [[nodiscard]] typename layers::parts_t& layer() { return parts.now->parts; }

  // What it was handed: the program's own objects, for the layers it makes.
  ui_needs<Actions> needs_;
  explicit window(const ui_needs<Actions>& n) : actions(n.actions), needs_(n) {
    fState.apply({.fill = true});
    parts.now.emplace(n);
  }
  // Everything made again, in the colours of the theme now in place.
  void rebuild() {
    parts.now.reset();
    parts.now.emplace(needs_);
    this->invalidateLayout();
    this->markDamaged();
  }

  [[nodiscard]] conversations_screen<Actions>& main() { return layer().frame.base().base(); }
  // The background behind the whole window, where it is so.
  void show_behind(const config::wallpaper_t& chosen) {
    if (needs_.looks->window.behind)
      show_wallpaper_on(layer().behind, chosen, *needs_.colours, *needs_.looks);
  }
  // The panel that is up, not on its way out.
  [[nodiscard]] panel_type* open_panel() { return layer().frame.shown(); }

  // A panel opened, in place of the one up if there is one.
  // A panel up: the one on top if it is one of these, else a new one sliding
  // in over it.
  template <class Panel>
  Panel& open() {
    if (panel_type* up = layer().frame.shown())
      if (Panel* same = up->visit(spl::overloaded{[](Panel& one) -> Panel* { return &one; },
                                             [](auto&) -> Panel* { return nullptr; }}))
        return *same;
    return spl::get<Panel>(layer().frame.open(std::in_place_type<Panel>, needs_));
  }
  // The top panel goes, and the one under it is up again.
  void back_panel() { layer().frame.back(); }
  void close() { layer().frame.close(); }
  // From the program, between events.
  void drop_closed() {
    layer().frame.dropClosed();
    layer().settings.dropClosed();
    layer().notice.dropClosed();
    layer().person.dropClosed();
    layer().room.dropClosed();
    layer().reactions.dropClosed();
    layer().history.dropClosed();
    layer().linking.dropClosed();
    layer().leaving.dropClosed();
    layer().marks.dropClosed();
    layer().manage.dropClosed();
    layer().forwarding.dropClosed();
    layer().new_chat.dropClosed();
    layer().new_room.dropClosed();
    layer().packs.dropClosed();
    layer().wallpaper.dropClosed();
    layer().explore.dropClosed();
    layer().tools.dropClosed();
    layer().sending.dropClosed();
  }

  void open_settings(std::string motion) { layer().settings.open(needs_, std::move(motion)); }
  void open_picture(std::string source, std::string sender, std::string name, std::string when) {
    layer().viewer.emplace(needs_, std::move(source), std::move(sender), std::move(name), std::move(when));
  }
  void open_send_box(const std::vector<pending_file>& files) {
    layer().sending.setWidthFittingContent(440.0f);
    layer().sending.open(needs_, files);
  }
  void close_send_box() { layer().sending.close(); }
  [[nodiscard]] send_box<Actions>* send_box_up() { return layer().sending.shown(); }
  void close_picture() { layer().viewer.reset(); }
  // A video: the viewer on its thumbnail, waiting for it; played once its
  // file is there, where the viewer is still up for it.
  void open_video(std::string thumbnail, std::string video, std::string sender, std::string name, std::string when) {
    layer().viewer.emplace(needs_, std::move(thumbnail), std::move(sender), std::move(name), std::move(when));
    layer().viewer->video = std::move(video);
  }
  void play_video(const std::string& video, const std::filesystem::path& file) {
    if (auto& up = layer().viewer; up && up->video == video)
      up->start(file);
  }
  void close_settings() { layer().settings.close(); }
  [[nodiscard]] settings_dialog<Actions>* settings_up() { return layer().settings.shown(); }
  // A selectable text's menu, where the pointer was pressed, kept in the
  // window; and gone.
  void show_text_menu(std::string text, std::optional<std::string> link = std::nullopt) {
    this->place_text_menu(parts.now->parts.text_menu_up.emplace(needs_, std::move(text), std::move(link)));
  }
  // A field's: Paste and the rest, for the field with the focus.
  void show_field_menu(const scene::text_menu::of_field& field) {
    this->place_text_menu(parts.now->parts.text_menu_up.emplace(needs_, field));
  }
  void place_text_menu(text_menu& menu) {
    auto& now = *parts.now;
    const skia::SkRect box = fState.fBounds;
    menu.apply({.place = scene::anchor::kTopLeft,
                .x = std::clamp(now.last_press.x() - box.fLeft, 0.0f, std::max(0.0f, box.width() - 150.0f)),
                .y = std::clamp(now.last_press.y() - box.fTop, 0.0f, std::max(0.0f, box.height() - menu.tall()))});
    now.invalidateLayout();
    now.markDamaged();
  }
  // A call, as the call is now: in its chat, where that is the one shown
  // and it does not ring here; else the card at the top of the window. And
  // gone, with the call.
  void show_call(const call_view& view) {
    auto& now = *parts.now;
    // A phone's: the whole window, whatever it rings or is in.
    if (view.whole) {
      this->hide_call_card();
      this->main().chat.hide_call();
      if (now.parts.call_whole)
        now.parts.call_whole->show(view);
      else
        now.parts.call_whole.emplace(needs_, view);
      now.invalidateLayout();
      now.markDamaged();
      return;
    }
    this->hide_call_screen();
    if (view.in_view && !rings_here(view)) {
      this->hide_call_card();
      this->main().chat.show_call(view);
      return;
    }
    this->main().chat.hide_call();
    if (now.parts.call_up)
      now.parts.call_up->show(view);
    else
      now.parts.call_up.emplace(needs_, view);
    now.invalidateLayout();
    now.markDamaged();
  }
  void hide_call() {
    this->main().chat.hide_call();
    this->hide_call_card();
    this->hide_call_screen();
  }
  void hide_call_screen() {
    auto& now = *parts.now;
    if (!now.parts.call_whole)
      return;
    now.parts.call_whole.reset();
    now.invalidateLayout();
    now.markDamaged();
  }
  void hide_call_card() {
    auto& now = *parts.now;
    if (!now.parts.call_up)
      return;
    now.parts.call_up.reset();
    now.invalidateLayout();
    now.markDamaged();
  }
  // A message's menu up: the right press was its.
  [[nodiscard]] bool context_menu_up() { return layer().menu.has_value(); }
  void close_text_menu() {
    auto& now = *parts.now;
    if (now.parts.text_menu_up) {
      now.parts.text_menu_up.reset();
      now.invalidateLayout();
      now.markDamaged();
    }
  }
  [[nodiscard]] room_settings<Actions>* manage_up() { return layer().manage.shown(); }

  void open_drawer() { layer().frame.base().open(); }
  void close_drawer() { layer().frame.base().close(); }
  void close_drawer_now() { layer().frame.base().closeNow(); }
  [[nodiscard]] bool drawer_open() { return layer().frame.base().isOpen(); }
  // Whether the pages are still moving.
  [[nodiscard]] bool pages_moving() { return layer().frame.settling(); }

  void open_menu(const menu_facts& facts) { layer().menu.emplace(needs_, facts); }
  void close_menu() { layer().menu.reset(); }
  // The input's emoji panel, over the chat above its button.
  void open_emoji(float right, float bottom) { layer().emoji.emplace(needs_, right, bottom); }
  void close_emoji() {
    layer().emoji.reset();
    needs_.shared->set_docked_panel_height(0.0f);  // the field back at the bottom
    // Told here, not only by its id: the screen made at the start was moved
    // into the window since, and the panel gone left an empty space under
    // the field where nothing ticked it.
    main().follow_docked();
  }
  // The GIFs saved, for the popup's GIF tab, where it is open.
  void show_gifs(const std::vector<std::string>& paths) {
    if (layer().emoji)
      layer().emoji->parts.card.parts.gifs.show(paths);
  }
  [[nodiscard]] bool emoji_open() { return layer().emoji.has_value(); }
  // The sticker pictures the panel shows, for the program to ask for.
  [[nodiscard]] std::vector<std::string> emoji_pictures_shown() {
    return layer().emoji ? layer().emoji->parts.card.parts.stickers.pictures_shown() : std::vector<std::string>{};
  }
  // The menu's card, where one is up: what takes the keys while it is.
  [[nodiscard]] scene::Node* menu_card() { return layer().menu ? &layer().menu->parts.menu : nullptr; }

  void show_notice(std::string what) {
    layer().notice.open(needs_, "Not implemented yet", std::format("{} isn't implemented yet.", what));
  }
  void show_message(std::string heading, std::string text) {
    layer().notice.open(needs_, std::move(heading), std::move(text));
  }
  void close_notice() { layer().notice.close(); }
  // A passphrase asked for: the one at the start is not dismissed.
  void ask_passphrase(proto::passphrase_for_t why) {
    auto& dialog = layer().passphrase;
    dialog.setDismissable(spl::visit(
        spl::overloaded{[](config::passphrase_for::unlock) { return false; }, [](const auto&) { return true; }}, why));
    dialog.open(needs_, why);
  }
  void passphrase_refused(std::string why) {
    if (auto* box = layer().passphrase.shown())
      box->say(std::move(why));
  }
  void close_passphrase() { layer().passphrase.close(); }
  void show_verification(const verification_view& view) { layer().verifying.open(needs_, view); }
  void close_verification() { layer().verifying.close(); }

  void open_person(const account_id& account, const std::string& key, const person_facts& facts) {
    layer().person.open(actions, *needs_.colours, *needs_.shared, account, key, facts);
  }
  void close_person() { layer().person.close(); }
  // Opened again while up, it takes what is known now in place.
  void open_room_card(const std::string& asked, const room_preview& known) {
    layer().room.open(actions, *needs_.colours, asked, known);
  }
  void close_room_card() { layer().room.close(); }
  [[nodiscard]] bool room_card_up() { return layer().room.shown() != nullptr; }
  void open_reactions(const conversation& in, const std::vector<reaction_entry>& entries, const model* now) {
    layer().reactions.open(needs_, in, entries, now);
  }
  void close_reactions() { layer().reactions.close(); }
  void open_edit_history(const conversation& in, const message& now, const model* known) {
    layer().history.open(needs_, in, now, known);
  }
  void close_edit_history() { layer().history.close(); }
  void open_link(std::string text, std::string url) { layer().linking.open(needs_, std::move(text), std::move(url)); }
  void close_link() { layer().linking.close(); }
  void open_leave_space(leave_space_facts facts) { layer().leaving.open(needs_, std::move(facts)); }
  void close_leave_space() { layer().leaving.close(); }
  void open_marks(mark_kind_t kind, const conversation& in, const std::vector<mark_entry>& entries, const model* now) {
    layer().marks.open(needs_, kind, in, entries, now);
  }
  void close_marks() { layer().marks.close(); }
  [[nodiscard]] bool marks_up() { return layer().marks.shown() != nullptr; }
  void open_manage(const room_settings_facts& facts) { layer().manage.open(needs_, facts); }
  void close_manage() { layer().manage.close(); }
  void open_forward(const std::vector<forward_target>& chats) { layer().forwarding.open(actions, *needs_.colours, chats); }
  void close_forward() { layer().forwarding.close(); }
  void open_new_chat(std::vector<found_person> known, std::string own_link) {
    close_drawer();
    layer().new_chat.open(actions, *needs_.colours, std::move(known), std::move(own_link));
  }
  void close_new_chat() { layer().new_chat.close(); }
  void show_found_people(const std::vector<found_person>& people, const std::string& query) {
    if (auto* up = layer().new_chat.shown())
      up->show_found(people, query);
  }
  void open_new_room(const std::string& own_server, std::optional<new_room_place> place = std::nullopt) {
    close_drawer();
    layer().new_room.open(actions, *needs_.colours, own_server, std::move(place));
  }
  void close_new_room() { layer().new_room.close(); }
  void open_packs(std::optional<std::string> room, bool editable) { layer().packs.open(actions, *needs_.colours, *needs_.shared, std::move(room), editable); }
  void close_packs() { layer().packs.close(); }
  void open_wallpaper(choice_level_t level) { layer().wallpaper.open(actions, *needs_.colours, *needs_.looks, level); }
  void close_wallpaper() { layer().wallpaper.close(); }
  void show_packs(std::vector<emote_pack> packs) {
    if (auto* up = layer().packs.shown())
      up->show_packs(std::move(packs));
  }
  // A pack saved: in the list as it is now, the list shown again -- or one's
  // own left open; one taken away, out of it.
  void pack_saved(const emote_pack& pack, bool removed, bool done) {
    auto* up = layer().packs.shown();
    if (!up)
      return;
    if (!done) {
      up->parts.note.setText(removed ? "The pack was not deleted." : "The pack was not saved.");
      return;
    }
    const auto same = [&](const emote_pack& one) { return one.chat == pack.chat && one.key == pack.key; };
    std::erase_if(up->packs, same);
    if (!removed)
      up->packs.push_back(pack);
    if (up->room)
      up->show_list();
    else
      up->parts.note.setText("Saved.");
  }
  void pack_picture_uploaded(const pack_picture& picture, bool done) {
    if (auto* up = layer().packs.shown()) {
      if (done)
        up->add_picture(picture);
      else
        up->parts.note.setText("An image could not be uploaded: " + picture.body);
    }
  }
  void open_explore(const std::string& own_server) {
    close_drawer();
    layer().new_chat.close();
    layer().explore.open(actions, *needs_.colours, own_server);
  }
  void close_explore() { layer().explore.close(); }
  void explore_as_space(const std::string& room, const std::string& name) {
    if (auto* up = layer().explore.shown())
      up->as_space(room, name);
  }
  // Explore rooms asking its server's directory: said so until it answers.
  void explore_loading() {
    if (auto* up = layer().explore.shown()) {
      up->parts.status.setText("Loading the rooms this server lists\u2026");
      up->parts.status.setVisible(true);
    }
  }
  void show_directory(const std::vector<directory_room>& rooms, const std::string& server,
                      const std::optional<std::string>& space = std::nullopt, const std::string& query = {},
                      const std::optional<std::string>& next = std::nullopt, bool more = false) {
    if (auto* up = layer().explore.shown())
      up->show(rooms, server, space, query, next, more);
  }
  // A protocol's own dialog up (Node, one of its dialogs), made from args.
  template <class Node, class... Args>
  void open_dialog(Args&&... args) {
    layer().tools.open(std::in_place_type<Node>, actions, *needs_.colours, std::forward<Args>(args)...);
  }
  void close_dialog() { layer().tools.close(); }

  void show(const std::vector<config::account_t>& saved, const model& now) {
    const auto& current = layer().frame.base().base().current;
    layer().frame.base().content().show(actions, saved, now, current ? std::string_view(current->address) : std::string_view());
  }
  void show_motion(std::string_view level) {
    if (auto* up = layer().settings.shown())
      up->show_motion(std::string(level));
  }

  // Its layers, each filling the window, as the default layout places them:
  // nothing placed by hand.
};

}  // namespace mux::ui
