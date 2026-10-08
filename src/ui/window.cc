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
import mux.kept_root;
import skiff.model;
import skiff.bind;
import :base;
import :controls;
import :themes;
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

// What the window shows that the program opens and closes, as a model:
// each dialog's facts, while it is open. The program edits it; the
// dialogs, bound to it, open and close as they read it.
// The chat screen as the program sets it: the account shown, the chat
// chosen, if one is, and whether its info is open beside it.
struct chat_shown {
  std::optional<conversation_id> chosen;
  std::optional<account_id> current;
  bool info_open = false;
  // The threads' panel open, and the thread open in it, if one is.
  bool threads_open = false;
  std::optional<std::string> thread;
  // The space opened as a forum, its rooms listed: by its id.
  std::optional<std::string> forum;
};
// Whether the drawer is out.
struct drawer_shown {
  bool out = false;
};
struct shown_root {
  skiff::model::Tracked<drawer_shown> drawer;
  skiff::model::Tracked<chat_shown> chat;
  skiff::model::Tracked<selection_shown> selection;
  skiff::model::Tracked<search_found> found;
  skiff::model::Tracked<std::optional<marks_facts>> marks;
  skiff::model::Tracked<std::optional<leave_space_facts>> leaving;
  skiff::model::Tracked<std::optional<link_facts>> linking;
  skiff::model::Tracked<std::optional<notice_facts>> notice;
  skiff::model::Tracked<std::optional<reactions_facts>> reactions;
  skiff::model::Tracked<std::optional<history_facts>> history;
  skiff::model::Tracked<std::optional<forward_facts>> forwarding;
  skiff::model::Tracked<std::optional<wallpaper_facts>> wallpaper;
  skiff::model::Tracked<std::optional<person_shown>> person;
  skiff::model::Tracked<std::optional<room_card_facts>> room;
  skiff::model::Tracked<std::optional<new_chat_facts>> new_chat;
  skiff::model::Tracked<std::optional<new_room_facts>> new_room;
  skiff::model::Tracked<std::optional<packs_facts>> packs;
  skiff::model::Tracked<std::optional<explore_facts>> explore;
  skiff::model::Tracked<std::optional<settings_facts>> settings;
  skiff::model::Tracked<std::optional<room_settings_facts>> manage;
  skiff::model::Tracked<std::optional<send_facts>> sending;
  skiff::model::Tracked<std::optional<verification_view>> verifying;
  skiff::model::Tracked<std::optional<passphrase_facts>> passphrase;
  skiff::model::Tracked<std::optional<menu_facts>> menu;
  skiff::model::Tracked<std::optional<viewer_facts>> viewer;
  skiff::model::Tracked<std::optional<emoji_facts>> emoji;
  skiff::model::Tracked<std::optional<panel_facts>> panel;
};
struct shown_reactions {};
using shown_model = skiff::model::Model<shown_root, shown_reactions>;
// A part that is always there -- the drawer out or back, the chat chosen --
// set.
template <class Part>
void show(shown_model& showing, Part part) {
  (void)showing.apply(skiff::model::edit(skiff::model::placeOf<Part, shown_root>(), skiff::model::setTo(std::move(part))));
}
// Such a part changed from what it is: the rest of it as it was.
template <class Part, class Change>
void change_shown(shown_model& showing, Change change) {
  Part now = *showing.look<Part>();
  change(now);
  show(showing, std::move(now));
}
// A dialog shown with these facts, or closed: its part of what is shown set.
template <class Facts>
void show(shown_model& showing, std::optional<Facts> facts) {
  (void)showing.apply(skiff::model::edit(skiff::model::placeOf<std::optional<Facts>, shown_root>(), skiff::model::setTo(std::move(facts))));
}

// A dialog open while the part it is bound to holds its facts -- made from
// them, with what the program handed the window (Needs) -- and closed where
// they are gone. Pressed off, or Esc: that part emptied.
template <class Content, class Facts, class Needs>
struct shown_dialog : widgets::Dialog<Content, widgets::dismiss::pressed> {
  const Needs* needs = nullptr;
  explicit shown_dialog(const Needs* handed) : needs(handed) {}
  void read(const std::optional<Facts>& now) {
    if (now) {
      this->dismissable_for(*now);
      this->show_with(*now);
    } else {
      this->close();
    }
  }
  // Facts changed while it is up: shown in place where what it shows says
  // how (show_page); else made again from them.
  void show_with(const Facts& facts)
    requires requires(Content& up) { up.show_page(facts); }
  {
    if (auto* up = this->shown())
      up->show_page(facts);
    else
      (void)this->open(*needs, facts);
  }
  void show_with(const Facts& facts) { (void)this->open(*needs, facts); }
  // Whether a press off it or Esc dismisses it, where what it shows says so
  // of these facts; else as its look says.
  void dismissable_for(const Facts& facts)
    requires requires { Content::dismissable(facts); }
  {
    this->setDismissable(Content::dismissable(facts));
  }
  void dismissable_for(const Facts&) {}
  auto onPress() { return skiff::bind::own(skiff::model::setTo(std::optional<Facts>{})); }
};

// The chat screen, its chat the one what is shown says is chosen.
template <class Screen>
struct shown_screen : Screen {
  using Screen::Screen;
  void read(const chat_shown& now) {
    this->chosen = now.chosen;
    // The account the program asked for; where it asked for none, the one
    // the screen falls back on as it shows the chats (the first) is kept.
    if (now.current)
      this->current = now.current;
    if (this->info_open != now.info_open)
      this->set_info_open(now.info_open);
    if (this->threads_open != now.threads_open || this->parts.threads.open != now.thread)
      this->set_threads(now.threads_open, now.thread);
    // A forum opened or left as the program says it, where that changed:
    // one the screen let go itself -- its space gone -- stays gone.
    if (std::exchange(forum_read, now.forum) != now.forum && this->forum_open != now.forum) {
      if (now.forum)
        this->open_forum(*now.forum);
      else
        this->close_forum();
    }
  }
  std::optional<std::string> forum_read;
};

// The drawer, out while what is shown says so; pushed back -- the scrim
// pressed, a swipe, Esc -- that part set so.
template <class Base, class Content>
struct shown_drawer : widgets::Drawer<Base, Content, widgets::dismiss::pressed> {
  using widgets::Drawer<Base, Content, widgets::dismiss::pressed>::Drawer;
  void read(const drawer_shown& now) {
    if (now.out != this->isOpen())
      this->setOpen(now.out);
  }
  auto onPress() { return skiff::bind::own(skiff::model::setTo(drawer_shown{false})); }
};

// The pages over the chats -- the accounts panel -- up while what is shown
// holds its facts, and gone where they are not; what is beside its list
// left due for the program to show as it brings the panel up to date.
template <class Frame, class Panel, class Needs>
struct shown_frame : Frame {
  const Needs* needs = nullptr;
  template <class... Args>
  explicit shown_frame(const Needs* handed, Args&&... args) : Frame(std::forward<Args>(args)...), needs(handed) {}
  void read(const std::optional<panel_facts>& now) {
    if (!now) {
      if (this->shown())
        this->close();
      return;
    }
    Panel* panel = this->shown() ? this->shown()->visit(spl::overloaded{[](Panel& one) -> Panel* { return &one; }, [](auto&) -> Panel* { return nullptr; }})
                                 : nullptr;
    if (panel == nullptr) {
      panel = &spl::get<Panel>(this->open(std::in_place_type<Panel>, *needs));
      if (now->note)
        panel->say(*now->note);
    }
    panel->detail_due = now->detail;
  }
};

// A layer holding Node while its part of what is shown holds its facts --
// made from them, with what the program handed the window -- and nothing
// where they are gone: a message's menu.
template <class Content, class Facts, class Needs>
struct shown_layer : scene::Node {
  const Needs* needs = nullptr;
  struct parts_t {
    std::optional<Content> up;
  } parts;
  explicit shown_layer(const Needs* handed) : needs(handed) {
    fState.apply({.fill = true});
    fState.setFloats(true);
    this->setVisible(false);
  }
  void read(const std::optional<Facts>& now) {
    if (now)
      parts.up.emplace(*needs, *now);
    else
      parts.up.reset();
    this->setVisible(now.has_value());
    this->invalidateLayout();
    this->markDamaged();
  }
  [[nodiscard]] Content* shown() { return parts.up ? &*parts.up : nullptr; }
};

// The conversations; over them the panel that is open, if one is, sliding in
// from the right and back out when closed; and over both, the drawer, pulled
// out from the left. All in this one window, switched by the program between
// events.

template <class Actions>
struct window : scene::Node {
  using panel_type = spl::variant<accounts_panel<Actions>>;
  using screen_node = skiff::bind::Bound<chat_shown, shown_screen<conversations_screen<Actions>>>;
  using drawer_node = shown_drawer<screen_node, drawer_panel<Actions>>;
  using with_drawer = skiff::bind::Bound<drawer_shown, drawer_node>;

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
      using Answer = ::mux::ui::request::copy_text;
      std::string text;
      ::mux::ui::request::copy_text operator()() { return ::mux::ui::request::copy_text{text}; }
    };
    struct key_it {
      using Answer = ::mux::ui::request::text_key;
      scene::Key key;
      bool shift = false;
      ::mux::ui::request::text_key operator()() { return ::mux::ui::request::text_key{key, shift}; }
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
      parts.copy.emplace(n.colours->widgets, "Copy", copy_it{std::move(text)});
      if (link)
        parts.copy_link.emplace(n.colours->widgets, "Copy Link", copy_it{std::move(*link)});
      this->rows();
    }
    // A field's.
    text_menu(const ui_needs<Actions>& n, const scene::text_menu::of_field& field) : text_menu(*n.colours) {
      const auto item = [&](std::optional<widgets::Button<key_it>>& button, std::string label, scene::Key key,
                            bool shift = false) {
        button.emplace(n.colours->widgets, std::move(label), key_it{key, shift});
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
    // What its handlers ask for, returned.
    using Answer = std::variant<::mux::ui::request::close_emoji, ::mux::ui::request::close_menu, ::mux::ui::request::close_picture, ::mux::ui::request::close_verification, ::mux::ui::request::verify_cancel_now, ::mux::ui::request::close_send_box, ::mux::ui::request::close_dialog, ::mux::ui::request::close_explore, ::mux::ui::request::close_wallpaper, ::mux::ui::request::close_packs, ::mux::ui::request::close_new_room, ::mux::ui::request::close_new_chat, ::mux::ui::request::close_forward, ::mux::ui::request::close_manage, ::mux::ui::request::close_marks, ::mux::ui::request::close_leave_space, ::mux::ui::request::close_link, ::mux::ui::request::close_edit_history, ::mux::ui::request::close_reactions, ::mux::ui::request::close_room_card, ::mux::ui::request::close_person_info, ::mux::ui::request::close_notice, ::mux::ui::request::close_settings, ::mux::ui::request::close_drawer>;
    using frame_t = skiff::bind::Bound<std::optional<panel_facts>,
                                       shown_frame<widgets::SlideOver<with_drawer, panel_type>, accounts_panel<Actions>, ui_needs<Actions>>>;
    // A dialog of Content, bound to its Facts' part of what is shown.
    template <class Content, class Facts>
    using shown_in = skiff::bind::Bound<std::optional<Facts>, shown_dialog<Content, Facts, ui_needs<Actions>>>;
    template <class Content, class Facts>
    static shown_in<Content, Facts> shown_made(const ui_needs<Actions>& n) {
      return shown_in<Content, Facts>(shown_dialog<Content, Facts, ui_needs<Actions>>(&n));
    }
    struct parts_t {
      nodes::Box<> backdrop;
      // The chat's background behind all of the window, where it is so:
      // drawn at its own opacity -- the desktop through it only where the
      // picture itself lets it be seen.
      wallpaper_t behind;
      // The pages slide over the drawer too: Manage accounts comes in over it.
      frame_t frame;
      shown_in<settings_dialog<Actions>, settings_facts> settings;
      shown_in<notice_box<Actions>, notice_facts> notice;
      // A person's info, in the middle, as tdesktop's profile layer.
      shown_in<person_card<Actions>, person_shown> person;
      // A room not joined, from a link: its card, as a person's.
      shown_in<room_card<Actions>, room_card_facts> room;
      // A message's reactions as events.
      shown_in<reactions_box<Actions>, reactions_facts> reactions;
      // A message's earlier versions, as AyuGram's edit history.
      shown_in<edit_history_box<Actions>, history_facts> history;
      // A link put on what is selected in the message field: Ctrl+K's.
      shown_in<link_box<Actions>, link_facts> linking;
      // Leaving a space, and which of its rooms with it.
      shown_in<leave_space_box<Actions>, leave_space_facts> leaving;
      // The mentions or the reactions not yet seen, listed.
      shown_in<marks_box<Actions>, marks_facts> marks;
      // A room's management.
      shown_in<room_settings<Actions>, room_settings_facts> manage;
      // Where a message is forwarded to.
      shown_in<forward_box<Actions>, forward_facts> forwarding;
      // Element's Start chat, and its Create a room.
      shown_in<start_chat_box<Actions>, new_chat_facts> new_chat;
      shown_in<create_room_box<Actions>, new_room_facts> new_room;
      // Emojis & Stickers: a room's packs, or one's own.
      shown_in<packs_box<Actions>, packs_facts> packs;
      // A chat background chosen, at a level.
      shown_in<wallpaper_box<Actions>, wallpaper_facts> wallpaper;
      // A server's public rooms, searched.
      shown_in<explore_box<Actions>, explore_facts> explore;
      // A protocol's own dialog: Matrix's developer tools, for one.
      widgets::Dialog<tool_holder> tools;
      shown_in<send_box<Actions>, send_facts> sending;
      // A passphrase asked for: at the start, where local data is encrypted;
      // or to turn that on or off, or change it. Over everything.
      shown_in<passphrase_box<Actions>, passphrase_facts> passphrase;
      // An emoji verification, as it goes.
      shown_in<verification_box<Actions>, verification_view> verifying;
      skiff::bind::Bound<std::optional<emoji_facts>, shown_layer<emoji_popup<Actions>, emoji_facts, ui_needs<Actions>>> emoji;
      skiff::bind::Bound<std::optional<menu_facts>, shown_layer<context_menu<Actions>, menu_facts, ui_needs<Actions>>> menu;
      skiff::bind::Bound<std::optional<viewer_facts>, shown_layer<picture_viewer<Actions>, viewer_facts, ui_needs<Actions>>> viewer;
      // A selectable text's menu, where it was pressed with the right button.
      std::optional<text_menu> text_menu_up;
      // A call, while there is one: over everything.
      std::optional<call_bar<Actions>> call_up;
      // A call on a phone: the whole window, as Element's phone apps.
      std::optional<call_screen<Actions>> call_whole;
    } parts;
    // Where the pointer was last pressed, in the window: where a menu asked
    // by that press is put. A press off the text menu closes it, at once --
    // nothing of it is pressed.
    skia::SkPoint last_press{};
    // A press off the emoji popup, where it was: let go there -- a tap, not
    // a drag to scroll the chat under it -- it closes the popup.
    std::optional<skia::SkPoint> press_off_emoji;
    using Node::onPointer;
    std::optional<Answer> onPointer(scene::phase::capture, const scene::pointer::up& lift, scene::PointerReply&) {
      std::optional<Answer> answer;
      const auto off = std::exchange(press_off_emoji, std::nullopt);
      if (off && parts.emoji.shown() && std::hypot(lift.x - off->fX, lift.y - off->fY) < 8.0f)
        answer = ::mux::ui::request::close_emoji{};
      return answer;
    }
    void onPointer(scene::phase::capture, const scene::pointer::down& press, scene::PointerReply&) {
      last_press = {press.x, press.y};
      press_off_emoji = parts.emoji.shown() && !parts.emoji.shown()->parts.card.bounds().contains(press.x, press.y)
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
    std::optional<Answer> onKey(scene::phase::capture, const scene::key::down& press, scene::Reply& reply) {
      if (press.key != scene::keys::kEscape)
        return std::nullopt;
      // Taken: with what it asks for, where it asks for something.
      const auto closed = [&](std::optional<Answer> asked = std::nullopt) {
        reply.handle();
        return asked;
      };
      if (parts.text_menu_up) {
        parts.text_menu_up.reset();
        this->invalidateLayout();
        this->markDamaged();
        return closed();
      }
      if (parts.menu.shown())
        return closed(::mux::ui::request::close_menu{});
      if (parts.emoji.shown())
        return closed(::mux::ui::request::close_emoji{});
      if (parts.viewer.shown())
        return closed(::mux::ui::request::close_picture{});
      // A verification: OK where it is over, Decline or Cancel where it
      // waits. Not while the emoji are compared: an answer is asked there.
      if (auto* box = parts.verifying.shown()) {
        if (step_over(box->step))
          return closed(::mux::ui::request::close_verification{});
        if (step_waits(box->step))
          return closed(::mux::ui::request::verify_cancel_now{});
      }
      // The dialogs, the one drawn last -- on top -- first.
      if (parts.sending.shown())
        return closed(::mux::ui::request::close_send_box{});
      if (parts.tools.shown())
        return closed(::mux::ui::request::close_dialog{});
      if (parts.explore.shown())
        return closed(::mux::ui::request::close_explore{});
      if (parts.wallpaper.shown())
        return closed(::mux::ui::request::close_wallpaper{});
      if (parts.packs.shown())
        return closed(::mux::ui::request::close_packs{});
      if (parts.new_room.shown())
        return closed(::mux::ui::request::close_new_room{});
      if (parts.new_chat.shown())
        return closed(::mux::ui::request::close_new_chat{});
      if (parts.forwarding.shown())
        return closed(::mux::ui::request::close_forward{});
      if (parts.manage.shown())
        return closed(::mux::ui::request::close_manage{});
      if (parts.marks.shown())
        return closed(::mux::ui::request::close_marks{});
      if (parts.leaving.shown())
        return closed(::mux::ui::request::close_leave_space{});
      if (parts.linking.shown())
        return closed(::mux::ui::request::close_link{});
      if (parts.history.shown())
        return closed(::mux::ui::request::close_edit_history{});
      if (parts.reactions.shown())
        return closed(::mux::ui::request::close_reactions{});
      if (parts.room.shown())
        return closed(::mux::ui::request::close_room_card{});
      if (parts.person.shown())
        return closed(::mux::ui::request::close_person_info{});
      if (parts.notice.shown())
        return closed(::mux::ui::request::close_notice{});
      // Settings: a page back to where its ← goes; home, closed.
      if (auto* box = parts.settings.shown()) {
        if (box->step_back())
          return closed();
        return closed(::mux::ui::request::close_settings{});
      }
      // The drawer, under every dialog.
      if (parts.frame.base().isOpen())
        return closed(::mux::ui::request::close_drawer{});
      return std::nullopt;
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
      // A layer up: an optional's node, or a shown layer with what it holds.
      const auto draw_up = spl::overloaded{[&](auto& one)
                                             requires requires { one.shown(); }
                                           {
                                             if (one.shown())
                                               scene::draw(one, painting, canvas, alpha);
                                           },
                                           [&](auto& one) {
                                             if (one)
                                               scene::draw(*one, painting, canvas, alpha);
                                           }};
      const auto over_if = [&](auto&... each) { (draw_up(each), ...); };
      over_if(emoji, menu, viewer, text_menu_up, call_up, call_whole);
    }

    layers(const ui_needs<Actions>& n)
        : parts{.backdrop = nodes::Box<>(n.colours->background),
                .frame = frame_t(std::in_place, &n, with_drawer(drawer_node(std::piecewise_construct, std::forward_as_tuple(std::in_place, n), std::forward_as_tuple(n)))),
                .settings = shown_made<settings_dialog<Actions>, settings_facts>(n),
                .notice = shown_made<notice_box<Actions>, notice_facts>(n),
                .person = shown_made<person_card<Actions>, person_shown>(n),
                .room = shown_made<room_card<Actions>, room_card_facts>(n),
                .reactions = shown_made<reactions_box<Actions>, reactions_facts>(n),
                .history = shown_made<edit_history_box<Actions>, history_facts>(n),
                .linking = shown_made<link_box<Actions>, link_facts>(n),
                .leaving = shown_made<leave_space_box<Actions>, leave_space_facts>(n),
                .marks = shown_made<marks_box<Actions>, marks_facts>(n),
                .manage = shown_made<room_settings<Actions>, room_settings_facts>(n),
                .forwarding = shown_made<forward_box<Actions>, forward_facts>(n),
                .new_chat = shown_made<start_chat_box<Actions>, new_chat_facts>(n),
                .new_room = shown_made<create_room_box<Actions>, new_room_facts>(n),
                .packs = shown_made<packs_box<Actions>, packs_facts>(n),
                .wallpaper = shown_made<wallpaper_box<Actions>, wallpaper_facts>(n),
                .explore = shown_made<explore_box<Actions>, explore_facts>(n),
                .sending = shown_made<send_box<Actions>, send_facts>(n),
                .passphrase = shown_made<passphrase_box<Actions>, passphrase_facts>(n),
                .verifying = shown_made<verification_box<Actions>, verification_view>(n),
                .emoji = decltype(parts_t::emoji)(shown_layer<emoji_popup<Actions>, emoji_facts, ui_needs<Actions>>(&n)),
                .menu = decltype(parts_t::menu)(shown_layer<context_menu<Actions>, menu_facts, ui_needs<Actions>>(&n)),
                .viewer = decltype(parts_t::viewer)(shown_layer<picture_viewer<Actions>, viewer_facts, ui_needs<Actions>>(&n))} {
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
      // The files to send: as wide as 440 at most, as high as they are.
      parts.sending.setWidthFittingContent(440.0f);
    }
  };
  struct parts_t {
    std::optional<layers> now;
  } parts;
  // The layers as they are.
  [[nodiscard]] typename layers::parts_t& layer() { return parts.now->parts; }

  // What it was handed: the program's own objects, for the layers it makes.
  ui_needs<Actions> needs_;
  explicit window(const ui_needs<Actions>& n) : needs_(n) {
    fState.apply({.fill = true});
    // From its own copy, which the layers' dialogs point at.
    parts.now.emplace(needs_);
  }
  // Everything made again, in the colours of the theme now in place.
  void rebuild() {
    parts.now.reset();
    parts.now.emplace(needs_);
    this->invalidateLayout();
    this->markDamaged();
  }

  [[nodiscard]] conversations_screen<Actions>& main() { return layer().frame.base().base(); }
  // What is kept, as the window's binding last read it.
  const kept_root* kept_ = nullptr;
  // Read by the window's binding as what is kept moves, and by the chats
  // binding as the chats -- or the chat chosen -- do: the looks in effect.
  template <class Reactions>
  void refresh(const skiff::model::Model<kept_root, Reactions>& kept) {
    kept_ = &kept.root();
    this->read_looks();
  }
  void refresh(const chats_model&) { this->read_looks(); }
  // The looks in effect, as what is kept says for the chat chosen: what each
  // level holds of the looks and of room events, for the choices to show
  // what is in effect; the panels' look, the window repainted where it
  // changes; and the background behind the window -- the chat's, else
  // every chat's.
  void read_looks() {
    auto& screen = this->main();
    if (kept_ == nullptr || screen.last_model == nullptr)
      return;
    const auto& looks = kept_->looks.fValue;
    const auto& history = kept_->history.fValue;
    const auto space = space_above_of(screen.last_model->accounts());
    const chat_settings reads{kept_, &space};
    const std::optional<conversation_id>& chosen = screen.chosen;
    looks_shown& shown = *needs_.looks;
    looks_held account_held, chat_held;
    room_events_held account_events, chat_events;
    if (chosen) {
      chat_held = {reads.own_of<&config::chat_choices::wallpaper>(*chosen), reads.own_of<&config::chat_choices::bubbles>(*chosen),
                   reads.own_of<&config::chat_choices::panels>(*chosen)};
      chat_events = {reads.own_of<&config::chat_choices::room_events>(*chosen), reads.own_of<&config::chat_choices::room_event_kinds>(*chosen)};
      if (const config::account_t* account = reads.settings_of(chosen->account.address)) {
        if (const auto& word = config::wallpaper_of(*account))
          account_held.wallpaper = config::wallpaper_of(std::string_view(*word));
        if (const auto& word = config::bubbles_of(*account))
          account_held.bubbles = config::bubble_look_of(*word);
        if (const auto& word = config::panels_of(*account))
          account_held.panels = config::bubble_look_of(*word);
        account_events = {config::room_events_of(*account), config::room_event_kinds_of(*account)};
      }
    }
    shown.at(choice_level::everywhere{}) = {looks.wallpaper, looks.bubbles, looks.panels};
    shown.at(choice_level::account{}) = std::move(account_held);
    shown.at(choice_level::chat{}) = std::move(chat_held);
    room_events_at(choice_level::everywhere{}) = {history.show_room_events, history.room_event_kinds};
    room_events_at(choice_level::account{}) = std::move(account_events);
    room_events_at(choice_level::chat{}) = std::move(chat_events);
    shown.panels = chosen ? reads.panels_of(*chosen) : looks.panels.value_or(config::bubble_look{});
    if (show_panels(*needs_.paint, shown.panels, shown.window, *needs_.colours)) {
      this->markDamaged();
      scene::work::mark(screen.fState.fId);  // an ease ticked by the screen
    }
    this->show_behind(chosen ? reads.wallpaper_of(*chosen) : looks.wallpaper.value_or(config::wallpaper_t{config::wallpaper::theme{}}));
    // The drawer: the accounts saved, with what the chats say of each.
    const auto& current = screen.current;
    layer().frame.base().content().show(kept_->accounts.values(), *screen.last_model,
                                        current ? std::string_view(current->address) : std::string_view());
  }
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
    if (std::exchange(text_menu_close_due, false))
      this->close_text_menu_now();
    if (std::exchange(call_hide_due, false))
      this->hide_call_now();
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

  [[nodiscard]] send_box<Actions>* send_box_up() { return layer().sending.shown(); }
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
    text_menu_close_due = false;
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
    call_hide_due = false;
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
  // The call gone: between events, not now -- its Hang up is still being
  // answered as the program says so.
  void hide_call() { call_hide_due = true; }
  void hide_call_now() {
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
  [[nodiscard]] bool context_menu_up() { return layer().menu.shown() != nullptr; }
  // Closed between events, not now: the program asks it as it does what
  // was pressed in it, while that press is still being answered.
  void close_text_menu() {
    if (parts.now->parts.text_menu_up)
      text_menu_close_due = true;
  }
  void close_text_menu_now() {
    auto& now = *parts.now;
    if (now.parts.text_menu_up) {
      now.parts.text_menu_up.reset();
      now.invalidateLayout();
      now.markDamaged();
    }
  }
  bool text_menu_close_due = false;
  bool call_hide_due = false;
  [[nodiscard]] room_settings<Actions>* manage_up() { return layer().manage.shown(); }

  void close_drawer_now() { layer().frame.base().closeNow(); }
  [[nodiscard]] bool drawer_open() { return layer().frame.base().isOpen(); }
  // Whether the pages are still moving.
  [[nodiscard]] bool pages_moving() { return layer().frame.settling(); }

  // The input's emoji panel, over the chat above its button.
  // The emoji panel gone: the field back where it was.
  void emoji_closed() {
    needs_.shared->set_docked_panel_height(0.0f);  // the field back at the bottom
    // Told here, not only by its id: the screen made at the start was moved
    // into the window since, and the panel gone left an empty space under
    // the field where nothing ticked it.
    main().follow_docked();
  }
  // The GIFs saved, for the popup's GIF tab, where it is open.
  void show_gifs(const std::vector<std::string>& paths) {
    if (auto* up = layer().emoji.shown())
      up->parts.card.parts.gifs.show(paths);
  }
  [[nodiscard]] bool emoji_open() { return layer().emoji.shown() != nullptr; }
  // The sticker pictures the panel shows, for the program to ask for.
  [[nodiscard]] std::vector<std::string> emoji_pictures_shown() {
    auto* up = layer().emoji.shown();
    return up ? up->parts.card.parts.stickers.pictures_shown() : std::vector<std::string>{};
  }
  // The menu's card, where one is up: what takes the keys while it is.
  [[nodiscard]] scene::Node* menu_card() {
    auto* up = layer().menu.shown();
    return up ? &up->parts.menu : nullptr;
  }


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
  // A protocol's own dialog up (Node, one of its dialogs), made from args.
  template <class Node, class... Args>
  void open_dialog(Args&&... args) {
    layer().tools.open(std::in_place_type<Node>, *needs_.colours, std::forward<Args>(args)...);
  }
  void close_dialog() { layer().tools.close(); }


  // Its layers, each filling the window, as the default layout places them:
  // nothing placed by hand.
};

}  // namespace mux::ui
