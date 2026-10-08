// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:context_menu -- A message's menu.
export module mux.ui:context_menu;

import std;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes.box;
import skiff.nodes.flow;
import skiff.nodes.image;
import skiff.nodes.scroll;
import skiff.nodes.text;
import skiff.widgets.textbox;
import mux.core;
import mux.config;
import mux.logic.emoji;
import :base;
import :icons;
import :controls;
import :themes;
import :avatars;
import :timeline;
import :conversations;
import :forms;
import :names;

export import :emoji_kept;
export import :emoji_panels;
export import :emoji_popup;

export namespace mux::ui {

// What is done with a message from its menu, as the program keeps it.
// Who has seen a message, as tdesktop's menu shows it (chat_helpers.style's
// defaultWhoRead, who_reacted_context_action.cpp): the read ticks 15 in;
// "N Seen", the one reader's name, or "Nobody Viewed", 44 in; up to three
// userpics of 22 at the right, 17 in, each 8 over the next, in a ring of the
// menu's colour. Hovered, every reader in a submenu beside the menu: a
// userpic of 30, 13 in, the name 57 in, and under it when they read.
template <class Actions>
struct seen_row : nodes::Stack, outbox {
  static constexpr float kHeight = 33.0f, kFace = 22.0f, kOverlap = 8.0f, kRight = 17.0f;
  static constexpr std::size_t kMostFaces = 3;
  // How far the readers list lies over the menu it opens from.
  static constexpr float kOverlapMenu = 6.0f;
  // A reader, as a line of the submenu: pressed, their card, as a name
  // pressed anywhere opens it.
  struct reader_row : nodes::Stack, outbox {
    Actions* actions = nullptr;
    std::string id;
    struct lines_t : nodes::Stack {
      struct parts_t {
        nodes::Text name;
        nodes::Text when;
      } parts;
      lines_t(const palette& colours, const seen_reader& one)
          : parts{.name = nodes::Text(one.name, 13.0f, colours.text),
                  .when = nodes::Text(one.at ? clock_of(*one.at) : std::string("seen"), 12.0f, colours.dim)} {
        fState.apply({.autoSize = scene::axes::kY, .grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
        parts.name.setElided(true);
        parts.when.setElided(true);
      }
    };
    struct parts_t {
      avatar_mark face;
      lines_t lines;
    } parts;
    reader_row(const palette& colours, Actions* a, const seen_reader& one)
        : actions(a), id(one.id), parts{.face = avatar_mark(one.id, one.name, 30.0f), .lines = lines_t(colours, one)} {
      this->setHorizontal();
      this->setGap(14.0f);  // the name at 13 + 30 + 14 = 57
      // 6 over and under: the name at 13 and the time at 12 are 31.25 high,
      // and 7 left them 30 -- 1.25 out of the row.
      fState.apply({.fillX = true, .height = 44.0f, .padding = {6.0f, 17.0f, 6.0f, 13.0f}, .hoverBackground = colours.chosen});
    }
    [[nodiscard]] bool acceptsInput() const { return true; }
    [[nodiscard]] bool hoverChangesAppearance() const { return true; }
    [[nodiscard]] bool onClick(float, float) {
      this->emit(::mux::ui::request::close_menu{});
      this->emit(::mux::ui::request::open_member_info{id});
      return true;
    }
  };
  // The readers, scrolling where there are more than fit: at most about
  // seven rows tall.
  struct submenu_t : nodes::Stack {
    static constexpr float kRow = 44.0f, kMostHeight = 320.0f, kWidth = 240.0f;
    [[nodiscard]] static float height_for(std::size_t readers) {
      return std::min(kMostHeight, kRow * static_cast<float>(readers) + 10.0f);
    }
    using rows_t = nodes::Flow<std::vector<reader_row>>;
    struct parts_t {
      nodes::ScrollContainer<rows_t> list{rows_t({.spacingY = 0.0f, .wrap = false}, {})};
    } parts;
    submenu_t(const palette& colours, Actions* a, const std::vector<seen_reader>& readers) {
      auto& rows = std::get<0>(std::get<0>(parts.list.fChildren).fChildren);
      rows.reserve(readers.size());
      for (const seen_reader& one : readers)
        rows.emplace_back(colours, a, one);
      std::get<0>(parts.list.fChildren).apply({.fillX = true, .autoSize = scene::axes::kY});
      parts.list.apply({.fill = true});
      const float tall = height_for(readers.size());
      fState.apply({.width = kWidth, .height = tall, .padding = {6.0f, 0.0f, 4.0f, 0.0f},
                    .cornerRadius = 10.0f, .background = colours.popup(), .border = scene::Border{colours.band, 1.0f},
                    .masking = true});
    }
  };
  Actions* actions = nullptr;
  // The colours it is made in, for the readers' list it opens.
  const palette* colours_ = nullptr;
  std::vector<seen_reader> readers;
  // The window, as the menu fills it: the submenu kept inside it.
  const skia::SkRect* window = nullptr;
  // The submenu, held by the menu's layer over the whole window -- not by
  // this row, outside whose box and its card's it lies: what is repainted
  // or pointed at there went by the card's box, and missed it. And that
  // layer, laid out again as it opens or goes.
  std::optional<submenu_t>* submenu = nullptr;
  scene::Node* layer = nullptr;
  struct parts_t {
    icon_mark mark;
    nodes::Text label;
    std::vector<avatar_mark> faces;
  } parts;
  seen_row(const palette& colours, Actions* a, std::vector<seen_reader> who)
      : actions(a), colours_(&colours), readers(std::move(who)),
        parts{.mark = icon_mark(colours, icon::check{}),
              .label = nodes::Text(readers.empty()       ? std::string("Nobody Viewed")
                                   : readers.size() == 1 ? readers.front().name
                                                         : std::to_string(readers.size()) + " Seen",
                                   13.0f, colours.text)} {
    auto& [mark, label, faces] = parts;
    this->setHorizontal();
    const std::size_t shown = std::min(kMostFaces, readers.size());
    const float faces_width = shown ? kFace + static_cast<float>(shown - 1) * (kFace - kOverlap) : 0.0f;
    // The text 44 in, 9 over and 7 under it; the faces' room kept at the right.
    fState.apply({.fillX = true,
                  .height = kHeight,
                  .padding = {9.0f, kRight + (shown ? faces_width + 8.0f : 0.0f), 7.0f, 44.0f},
                  .hoverBackground = colours.chosen});
    // The ticks 15 in, in the middle of the row's height: out of the flow,
    // back over the padding.
    mark.apply({.place = scene::anchor::kCentreLeft, .x = 15.0f - 44.0f});
    label.setElided(true);
    label.apply({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
    // The faces out of the flow, in the room at the right: the first the
    // rightmost, 17 in from the edge, each next 14 to its left.
    for (std::size_t i = 0; i < shown; ++i) {
      faces.emplace_back(readers[i].id, readers[i].name, kFace);
      faces.back().apply({.place = scene::anchor::kCentreRight,
                          .x = faces_width + 8.0f - static_cast<float>(i) * (kFace - kOverlap),
                          .border = scene::Border{colours.sidebar, 2.0f}});
    }
  }
  // The submenu while the row or the submenu is hovered, as tdesktop's opens
  // under the pointer: moving over to it no longer closes it. It overlaps the
  // menu's edge: a gap between them, where neither is hovered, closed it on
  // the way over.
  void update(double) {
    if (submenu == nullptr || layer == nullptr)
      return;
    const bool open = (fState.hovered() || (*submenu && (*submenu)->fState.hovered())) && !readers.empty();
    if (open == submenu->has_value())
      return;
    if (open) {
      submenu->emplace(*colours_, actions, readers);
      // Beside the menu, over its edge -- left of it where the window has
      // no room on the right. Its top at the row's, or higher where it would
      // pass the window's bottom -- as Telegram's, kept on the screen -- but
      // never above its top. In the layer's box, the window's.
      const skia::SkRect row = fState.fBounds;
      const skia::SkRect box = window != nullptr && !window->isEmpty() ? *window : row;
      const float tall = submenu_t::height_for(readers.size());
      const float top = std::max(box.fTop + 8.0f, std::min(row.fTop, box.fBottom - 8.0f - tall));
      float left = row.fRight - kOverlapMenu;
      if (left + submenu_t::kWidth > box.fRight - 8.0f)
        left = std::max(box.fLeft + 8.0f, row.fLeft + kOverlapMenu - submenu_t::kWidth);
      (*submenu)->apply({.place = scene::anchor::kTopLeft, .x = left - box.fLeft, .y = top - box.fTop});
    } else {
      submenu->reset();
    }
    layer->invalidateLayout();
    layer->markDamaged();
  }
};

template <class Actions>
struct context_menu : scene::Node, outbox {
  struct card : nodes::Stack {
    // Quick reactions, as tdesktop's menu has them at its top.
    struct quick_reaction : nodes::Stack, outbox {
      Actions* actions;
      std::string key;
      struct parts_t {
        nodes::Text face;
      } parts;
      quick_reaction(const palette& colours, Actions* a, std::string k)
          : actions(a), key(k), parts{.face = nodes::Text(std::move(k), 22.0f, colours.text)} {
        auto& face = parts.face;
        this->setHorizontal();
        fStack.justify = nodes::justify::middle{};
        // tdesktop's reactionCornerSize (36 by 32) and reactionCornerImage (22).
        fState.apply({.width = 36.0f, .height = 32.0f, .cornerRadius = 16.0f, .hoverBackground = colours.chosen});
        face.apply({.alignSelf = scene::align::kMiddle});
      }
      [[nodiscard]] bool acceptsInput() const { return true; }
      [[nodiscard]] bool hoverChangesAppearance() const { return true; }
      [[nodiscard]] bool onClick(float, float) {
        this->emit(::mux::ui::request::menu_react{key});
        return true;
      }
    };
    // The six, and at their end the way to every emoji, as tdesktop's.
    struct expand_emoji {
      card* of;
      void operator()() const { of->expand(); }
    };
    struct quick_row : nodes::Stack {
      struct parts_t {
        std::vector<quick_reaction> each;
        icon_button<expand_emoji> more;
      } parts;
      quick_row(const palette& colours, Actions* a, card* of) : parts{.more = icon_button<expand_emoji>(colours, icon::down{}, {of})} {
        auto& [each, more] = parts;
        // As wide as what is in it: the menu is sized by it, not it by the
        // menu -- a menu of a set width had the arrow run out past its edge.
        this->setHorizontal();
        fState.apply({.autoSize = scene::axes::kBoth, .padding = {2.0f, 6.0f, 4.0f, 6.0f}});
        for (const char* key : {"👍", "❤️", "😂", "😮", "😢", "🙏"})
          each.emplace_back(colours, a, key);
        more.apply({.width = 28.0f, .height = 32.0f, .cornerRadius = 14.0f});
      }
    };
    Actions* actions_of = nullptr;
    // The colours it is made in, for the emoji it unrolls; and the emoji
    // kept, the program's.
    const palette* colours_ = nullptr;
    emoji_kept* kept_ = nullptr;
    using reply_row = row_item<sends<::mux::ui::request::menu_reply>>;
    using thread_row = row_item<sends<::mux::ui::request::menu_thread>>;
    using quote_reply_row = row_item<sends<::mux::ui::request::menu_quote_reply>>;
    using edit_row = row_item<sends<::mux::ui::request::menu_edit>>;
    using later_row = row_item<not_yet<Actions>>;
    using copy_row = row_item<sends<::mux::ui::request::menu_copy>>;
    using link_row = row_item<sends<::mux::ui::request::menu_copy_link>>;
    using url_row = row_item<sends<::mux::ui::request::menu_copy_url>>;
    using fave_row = row_item<sends<::mux::ui::request::menu_fave_sticker>>;
    using copy_image_row = row_item<sends<::mux::ui::request::menu_copy_image>>;
    using save_row = row_item<sends<::mux::ui::request::menu_save>>;
    using gif_row = row_item<sends<::mux::ui::request::menu_save_gif>>;
    using pin_row = row_item<sends<::mux::ui::request::menu_pin>>;
    using reactions_row = row_item<sends<::mux::ui::request::menu_reactions>>;
    using forward_row = row_item<sends<::mux::ui::request::menu_forward>>;
    using source_row = row_item<sends<::mux::ui::request::menu_view_source>>;
    using history_row = row_item<sends<::mux::ui::request::menu_edit_history>>;
    using select_row = row_item<sends<::mux::ui::request::menu_select>>;
    using delete_row = row_item<sends<::mux::ui::request::menu_delete>>;
    // As tdesktop's, in its order: the quick reactions; every emoji, in
    // place of the rest once asked for; Reply, Edit, Pin, Copy, Copy
    // Message Link, Save As, Forward, Delete; and who has seen it -- how
    // many, and their names under it -- at the foot.
    struct parts_t {
      quick_row quick;
      nodes::Box<> quick_band;
      reply_row reply;
      // Reply in thread, as Element's menu has it: a Matrix room's.
      thread_row thread_reply;
      // What is selected of another's message, quoted in an answer.
      quote_reply_row quote_reply;
      edit_row edit;
      pin_row pin;
      copy_row copy;
      link_row copy_link;
      // The link pressed on, in the text or its preview.
      url_row copy_url;
      // A sticker: made a favourite, or no longer one.
      fave_row fave;
      copy_image_row copy_image;
      save_row save;
      gif_row save_gif;
      reactions_row reactions;
      forward_row forward;
      source_row source;
      history_row history;
      select_row select;
      delete_row remove;
      nodes::Box<> seen_band;
      seen_row<Actions> seen;
      // Every emoji, once asked for: over the items, out of their flow, and
      // after them, so drawn on top of them and pressed first.
      std::optional<emoji_panel<react_with<Actions>>> emoji;
    } parts;
    void expand() {
      auto& [quick, quick_band, reply, thread_reply, quote_reply, edit, pin, copy, copy_link, copy_url, fave, copy_image, save, save_gif, reactions, forward, source, history, select,
             remove, seen_band, seen, emoji] = parts;
      if (emoji)
        return;
      // As Telegram's: the list takes the room the items had under the
      // quick reactions, and unrolls down over them from the top; they stay
      // under it until it is down, and the menu keeps its size -- grown only
      // where the items left too little room for a list.
      const skia::SkRect box = fState.contentBox();
      const float under = box.fBottom - quick_band.bounds().fBottom;
      rolled = std::max(under, kEmojiLeast);
      fState.apply({.minHeight = this->bounds().height() + (rolled - under)});
      emoji.emplace(*colours_, *kept_, react_with<Actions>{actions_of});
      emoji->apply({.place = scene::anchor::kTopLeft,
                     .y = quick_band.bounds().fBottom - box.fTop,
                     .fillX = true,
                     .height = 0.0f,
                     .background = colours_->sidebar,
                     .masking = true});
      unroll.jump(0.0f);
      unroll.setTarget(rolled);
      quick.parts.more.setVisible(false);
      this->invalidateLayout();
    }
    // The items, once the list is down over them: gone, the menu keeping
    // its size by its least height.
    void hide_items() {
      auto& [quick, quick_band, reply, thread_reply, quote_reply, edit, pin, copy, copy_link, copy_url, fave, copy_image, save, save_gif, reactions, forward, source, history, select, remove,
             seen_band, seen, emoji] = parts;
      for (scene::Node* item : std::initializer_list<scene::Node*>{&reply, &thread_reply, &quote_reply, &edit, &pin, &copy, &copy_link, &copy_url, &fave, &copy_image, &save,
                                                                   &save_gif, &reactions, &forward, &source,
                                                                   &remove, &seen_band, &seen})
        item->setVisible(false);
      this->invalidateLayout();
    }
    static constexpr float kEmojiLeast = 220.0f;
    float rolled = 0.0f;
    skiff::paint::Tween unroll{0.0f, 220.0f};
    [[nodiscard]] bool settling() const { return unroll.moving(); }
    void update(double now_ms) {
      if (unroll.step(now_ms) && parts.emoji) {
        parts.emoji->apply({.height = unroll.value()});
        if (!unroll.moving())
          this->hide_items();
        this->invalidateLayout();
      }
    }
    // The keys, as tdesktop's menu takes them: Up and Down through its
    // items, round; Enter does what is lit (the item's own); Esc closes it.
    [[nodiscard]] bool focusable() const { return true; }
    using Node::onKey;
    void onKey(scene::phase::bubble, const scene::key::down& press, scene::Reply& reply) {
      namespace keys = scene::keys;
      if (press.key == keys::kUp || press.key == keys::kDown) {
        reply.moveFocus(press.key == keys::kUp);
      } else if (press.key == keys::kEscape) {
        actions_of->close_menu();
        reply.handle();
      }
    }
    // What does not apply to the message left out.
    card(const palette& colours, emoji_kept& kept, Actions* a, const menu_facts& facts)
        : actions_of(a),
          colours_(&colours),
          kept_(&kept),
          parts{.quick = quick_row(colours, a, this),
                .quick_band = nodes::Box<>(colours.band),
                .reply = reply_row(colours, "Reply", {a}, icon::back{}),
                .thread_reply = thread_row(colours, "Reply in thread", {a}, icon::threads{}),
                .quote_reply = quote_reply_row(colours, "Quote & Reply", {a}, icon::back{}),
                .edit = edit_row(colours, "Edit", {a}, icon::sliders{}),
                .pin = pin_row(colours, facts.pinned ? "Unpin" : "Pin", {a}, icon::check{}),
                .copy = copy_row(colours, facts.selection ? "Copy Selected Text" : "Copy Text", {a}, icon::clip{}),
                .copy_link = link_row(colours, "Copy Message Link", {a}, icon::info{}),
                .copy_url = url_row(colours, "Copy Link", {a}, icon::clip{}),
                .fave = fave_row(colours, facts.sticker && kept.is_favourite(facts.sticker->url) ? "Remove from Favourites" : "Add to Favourites",
                                 {a}, icon::check{}),
                .copy_image = copy_image_row(colours, "Copy Image", {a}, icon::clip{}),
                .save = save_row(colours, "Save As…", {a}, icon::send{}),
                .save_gif = gif_row(colours, "Save GIF", {a}, icon::check{}),
                .reactions = reactions_row(colours, facts.reaction_count == 1 ? std::string("1 reaction")
                                                                     : std::format("{} reactions", facts.reaction_count),
                                           {a}, icon::people{}),
                .forward = forward_row(colours, "Forward", {a}, icon::send{}),
                .source = source_row(colours, "View Source", {a}, icon::info{}),
                .history = history_row(colours, "Edit History", {a}, icon::pencil{}),
                .select = select_row(colours, "Select", {a}, icon::check{}),
                .remove = delete_row(colours, "Delete", {a}, icon::close{}),
                .seen_band = nodes::Box<>(colours.band),
                .seen = seen_row<Actions>(colours, a, facts.seen)} {
      fState.setFloats(true);  // over the chat: frosted live, where asked
      auto& [quick, quick_band, reply, thread_reply, quote_reply, edit, pin, copy, copy_link, copy_url, fave, copy_image, save, save_gif, reactions, forward, source, history, select,
             remove, seen_band, seen, emoji] = parts;
      quick_band.apply({.fillX = true, .height = 1.0f, .margin = {0.0f, 0.0f, 4.0f, 0.0f}});
      // A menu's rows as tdesktop's menuWithIcons: 8 over and under the
      // 13px normalFont's line -- 33 high -- the icon 15 in, the text 54 in.
      const auto compact = [](auto& row) {
        row.apply({.height = 33.0f, .padding = {0.0f, 17.0f, 0.0f, 15.0f}});
        row.setGap(15.0f);
        row.parts.mark.apply({.width = 24.0f, .height = 24.0f});
        row.parts.label.setFontSize(13.0f);
      };
      compact(reply);
      compact(quote_reply);
      compact(edit);
      compact(pin);
      compact(thread_reply);
      compact(copy);
      compact(copy_link);
      compact(copy_url);
      compact(fave);
      compact(copy_image);
      compact(save);
      compact(save_gif);
      compact(reactions);
      compact(forward);
      compact(source);
      compact(remove);
      // One's own text, or one's own picture's caption, as Element edits it.
      // A reaction is neither edited nor reacted to: Matrix changes none.
      edit.setVisible(facts.editable && !facts.reaction && ((!facts.text.empty() && !facts.media) || facts.captioned));
      // Reactions where the account sends them.
      quick.setVisible(!facts.reaction && facts.can.react);
      quick_band.setVisible(!facts.reaction && facts.can.react);
      forward.setVisible(facts.can.forward);
      quote_reply.setVisible(facts.selection && !facts.own && !facts.copied.empty());
      copy.setVisible(!facts.copied.empty());
      copy_link.setVisible(!facts.link.empty());
      copy_url.setVisible(!facts.pressed_link.empty());
      fave.setVisible(facts.sticker.has_value());
      copy_image.setVisible(facts.picture.has_value());
      save.setVisible(facts.media.has_value());
      save_gif.setVisible(facts.media.has_value() && facts.moving);
      remove.setVisible(facts.deletable);
      pin.setVisible(facts.pinnable);
      // A message the server named, where the account has threads, and its
      // source: what was offered by being pinnable, a Matrix room's.
      thread_reply.setVisible(facts.can.threads && !facts.link.empty());
      source.setVisible(facts.can.view_source && !facts.link.empty());
      history.setVisible(facts.history);
      select.setVisible(!facts.id.empty());
      // Who reacted, as Telegram's menu lists them: wherever there are any.
      reactions.setVisible(facts.reaction_count > 0);
      seen_band.apply({.fillX = true, .height = 1.0f, .margin = {4.0f, 0.0f, 4.0f, 0.0f}});
      // As wide as its widest -- the quick reactions -- and no narrower than a
      // menu reads well at; the items fill that width.
      fState.apply({.autoSize = scene::axes::kBoth, .minWidth = 220.0f, .padding = {6.0f, 0.0f, 6.0f, 0.0f}, .cornerRadius = 10.0f, .background = colours.popup(), .border = scene::Border{colours.band, 1.0f},
                    .shadow = scene::Shadow{skia::colorSetARGB(70, 0, 0, 0), 3.0f}});
    }
  };
  struct parts_t {
    card menu;
    // Who has seen it, listed beside the menu: over the whole window, as the
    // menu is.
    std::optional<typename seen_row<Actions>::submenu_t> seen_list;
  } parts;
  Actions* actions = nullptr;

  // Where it was asked for: the pointer.
  float at_x = 0.0f, at_y = 0.0f;
  context_menu(const ui_needs<Actions>& n, const menu_facts& facts)
      : parts{.menu = card(*n.colours, *n.emoji, n.actions, facts)}, actions(n.actions), at_x(facts.x), at_y(facts.y) {
    fState.apply({.fill = true});
    parts.menu.parts.seen.window = &fState.fBounds;
    parts.menu.parts.seen.submenu = &parts.seen_list;
    parts.menu.parts.seen.layer = this;
    parts.menu.apply({.x = at_x, .y = at_y});
  }
  // As tdesktop's popup menu: at the pointer, going down and right -- up
  // where it would pass the window's bottom, left where it would pass its
  // right -- and kept inside the window.
  void layoutChildren() {
    scene::layoutChildrenInContentBox(*this);
    const skia::SkRect box = fState.contentBox();
    auto& menu = parts.menu;
    const float w = menu.bounds().width(), h = menu.bounds().height();
    constexpr float kEdge = 8.0f;
    float x = at_x, y = at_y;
    if (box.fTop + y + h > box.fBottom - kEdge)
      y = at_y - h;
    if (box.fLeft + x + w > box.fRight - kEdge)
      x = at_x - w;
    x = std::clamp(x, kEdge, std::max(kEdge, box.width() - w - kEdge));
    y = std::clamp(y, kEdge, std::max(kEdge, box.height() - h - kEdge));
    if (x != placed_x || y != placed_y) {
      placed_x = x;
      placed_y = y;
      menu.apply({.x = x, .y = y});
      scene::layoutChildrenInContentBox(*this);
    }
  }
  float placed_x = -1.0f, placed_y = -1.0f;
  // A press off the menu closes it.
  [[nodiscard]] bool acceptsInput() const { return true; }
  using Node::onPointer;
  void onPointer(scene::phase::target, const scene::pointer::down&, scene::PointerReply& reply) {
    this->emit(::mux::ui::request::close_menu{});
    reply.handle();
  }
};

}  // namespace mux::ui
