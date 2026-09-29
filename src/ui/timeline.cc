// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:timeline -- A chat's messages.
export module mux.ui:timeline;

import std;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes.flow;
import skiff.nodes.scroll;
import skiff.widgets.loader;
import mux.core;
import mux.video;
import mux.config;
import mux.logic.links;
import :base;
import :names;
import :message;
import :composer;

export namespace mux::ui {

// The messages, and over them, where one has scrolled up from the newest,
// the way back down. Each is placed by its own spec.
// What a message's menu is made from: the message, and what it carries.
struct menu_facts {
  std::string id;
  bool own = false;
  std::string text;    // all of it
  std::string copied;  // what Copy takes: the selection, or all of it
  bool selection = false;
  std::vector<std::string> seen;
  std::optional<std::string> media;  // a picture's or a file's source
  std::string media_name;
  bool moving = false;  // a GIF or a moving WebP: one that can be saved to the GIFs
  bool pinned = false;  // pinned in its chat: the menu offers Unpin
  bool pinnable = false;  // in a chat where pins are kept: a Matrix room
  bool reaction_events = false;  // reacted to, the reactions being events
  std::size_t reaction_count = 0;  // how many reactions it has, of anyone
  std::string link;  // a link to it, where it has one
  float x = 0.0f, y = 0.0f;
};

template <class Actions>
struct timeline_area : scene::Node {
  // The loader's cross: the message jumped to no longer looked for.
  struct stop_jump {
    Actions* actions = nullptr;
    void operator()() const { actions->stop_jump(); }
  };
  struct parts_t {
    nodes::ScrollContainer<nodes::Flow<std::vector<message_bubble>>> timeline{
        nodes::Flow<std::vector<message_bubble>>({.spacingY = 0.0f, .wrap = false}, {})};
    jump_button<Actions> jump;
    mark_button<Actions> mentions;
    mark_button<Actions> reactions;
    // While a message jumped to is being fetched: turning in the middle,
    // its cross stopping the search.
    widgets::RadialLoader<stop_jump> loading;
  } parts;
  Actions* actions = nullptr;
  explicit timeline_area(Actions* a)
      : parts{.jump = jump_button<Actions>(a),
              .loading = widgets::RadialLoader<stop_jump>(44.0f, {a}),
              .mentions = mark_button<Actions>(a, mark_kind::mention{}, "@"),
              .reactions = mark_button<Actions>(a, mark_kind::reaction{}, "\u2665")},
        actions(a) {
    parts.timeline.apply({.fill = true});
    // The room around the messages is inside what scrolls, so the bar is at
    // the window's edge.
    std::get<0>(parts.timeline.fChildren).apply(
        {.fillX = true, .autoSize = scene::axes::kY, .padding = {8.0f, 12.0f, 8.0f, 12.0f}});
    parts.jump.setVisible(false);
    parts.loading.apply({.place = scene::anchor::kCentre});
    parts.loading.setVisible(false);
  }
  // The bubbles in the list, as they are made.
  [[nodiscard]] std::vector<message_bubble>& bubbles() {
    return std::get<0>(std::get<0>(parts.timeline.fChildren).fChildren);
  }
  // A message swiped left: watched from above, before the list's scrolling
  // and a text's selecting see the pointer. A press that moves left at once,
  // and more across than up or down, takes the pointer and moves the
  // message; let go past its mark, it is answered; either way it goes back.
  std::optional<std::string> swiping;
  bool swipe_armed = false;
  float swipe_x = 0.0f, swipe_y = 0.0f;
  std::chrono::steady_clock::time_point swipe_pressed{};
  message_bubble* swiped() {
    if (!swiping)
      return nullptr;
    auto& entries = this->bubbles();
    const auto it = std::ranges::find(entries, *swiping, &message_bubble::message_id);
    return it == entries.end() ? nullptr : &*it;
  }
  void swipe_down(const scene::pointer::down& press) {
    swipe_armed = press.button <= 1;
    swipe_x = press.x;
    swipe_y = press.y;
    swipe_pressed = std::chrono::steady_clock::now();
  }
  void swipe_move(const scene::pointer::move& at, scene::PointerReply& reply) {
    if (message_bubble* one = this->swiped()) {
      one->swipe.jump(std::clamp(at.x - swipe_x, -120.0f, 0.0f));
      one->markDamaged();
      reply.handle();
      return;
    }
    if (!swipe_armed)
      return;
    const float dx = at.x - swipe_x, dy = at.y - swipe_y;
    if (std::abs(dx) < 8.0f && std::abs(dy) < 8.0f)
      return;
    swipe_armed = false;
    if (dx >= 0.0f || std::abs(dx) < 2.0f * std::abs(dy) ||
        std::chrono::steady_clock::now() - swipe_pressed > std::chrono::milliseconds(250))
      return;
    for (message_bubble& one : this->bubbles())
      if (one.bounds().contains(swipe_x, swipe_y) && !one.message_id.empty()) {
        swiping = one.message_id;
        one.swipe.jump(std::clamp(dx, -120.0f, 0.0f));
        reply.capturePointer();
        reply.suppressHover();
        reply.handle();
        return;
      }
  }
  void swipe_up(scene::PointerReply& reply) {
    swipe_armed = false;
    if (message_bubble* one = this->swiped()) {
      if (one->swipe.value() <= -message_bubble::kSwipeToReply)
        actions->reply_to(one->message_id, one->plain);
      one->swipe.setTarget(0.0f);
      swiping.reset();
      reply.releasePointer();
      reply.handle();
    }
  }
  void swipe_cancel(scene::PointerReply& reply) {
    swipe_armed = false;
    if (message_bubble* one = this->swiped()) {
      one->swipe.setTarget(0.0f);
      swiping.reset();
      reply.releasePointer();
    }
  }

  // The swipe is followed on the way down (capture) and at the list itself
  // (target): the same for both, one overload each.
  void onPointer(scene::phase::capture, const scene::pointer::down& press, scene::PointerReply&) { swipe_down(press); }
  void onPointer(scene::phase::target, const scene::pointer::down& press, scene::PointerReply&) { swipe_down(press); }
  void onPointer(scene::phase::capture, const scene::pointer::move& at, scene::PointerReply& reply) {
    swipe_move(at, reply);
  }
  void onPointer(scene::phase::target, const scene::pointer::move& at, scene::PointerReply& reply) {
    swipe_move(at, reply);
  }
  void onPointer(scene::phase::capture, const scene::pointer::up&, scene::PointerReply& reply) { swipe_up(reply); }
  void onPointer(scene::phase::target, const scene::pointer::up&, scene::PointerReply& reply) { swipe_up(reply); }
  void onPointer(scene::phase::capture, const scene::pointer::cancel&, scene::PointerReply& reply) {
    swipe_cancel(reply);
  }
  void onPointer(scene::phase::target, const scene::pointer::cancel&, scene::PointerReply& reply) {
    swipe_cancel(reply);
  }

  // Who has read a message: those whose receipt is for it or for one after
  // it, by their names in the chat -- its sender and the user aside.
  const model* seen_model = nullptr;
  std::optional<conversation_id> seen_chat;
  std::vector<std::string> seen_by(const std::string& id, const std::string& sender) const {
    std::vector<std::string> out;
    const conversation* chat = seen_model && seen_chat ? seen_model->find(*seen_chat) : nullptr;
    if (!chat)
      return out;
    std::map<std::string, std::size_t> at;
    for (std::size_t i = 0; i < chat->timeline.size(); ++i)
      at.emplace(chat->timeline[i].id, i);
    const auto mine = at.find(id);
    if (mine == at.end())
      return out;
    const auto when = chat->timeline[mine->second].at;
    for (const auto& [user, event] : chat->read_by) {
      if (user == sender || user == chat->id.account.address)
        continue;
      // Read up to a message here at or after it; else, the receipt pointing
      // at what is not among these -- a reaction, a state event -- read at
      // or after it was sent.
      if (const auto theirs = at.find(event); theirs != at.end()) {
        if (theirs->second >= mine->second)
          out.push_back(sender_name(*chat, user));
      } else if (const auto read = chat->receipt_times.find(user); read != chat->receipt_times.end() && read->second >= when) {
        out.push_back(sender_name(*chat, user));
      }
    }
    std::ranges::sort(out);
    return out;
  }

  // A right press on a message: its menu, where it was pressed.
  using Node::onPointer;
  // A press on what is in a message -- a picture, a file, a reply's quote,
  // its sender -- as a click: it comes here from what was pressed when that
  // did not take it, at once or, in a list that scrolls, on the release.
  [[nodiscard]] bool onClick(float x, float y) {
    const struct {
      float x, y;
    } press{x, y};
      for (const message_bubble& one : this->bubbles()) {
        // A picture: seen whole. A file: saved and opened.
        // A video, shown by its thumbnail: played in the viewer -- or, built
        // without video, by the system's player, as a file is opened.
        if (one.parts.body.parts.picture && one.parts.body.parts.picture->bounds().contains(press.x, press.y) &&
            one.said.attachment && one.said.attachment->video && !mux::video::kPlays) {
          actions->open_file(*one.said.attachment->video, one.said.attachment->name);
          return true;
        }
        if (one.parts.body.parts.picture && one.parts.body.parts.picture->bounds().contains(press.x, press.y) &&
            one.said.attachment && one.said.attachment->video) {
          const conversation* chat = seen_model && seen_chat ? seen_model->find(*seen_chat) : nullptr;
          const auto day = std::chrono::floor<std::chrono::days>(one.said.at);
          actions->open_video(one.parts.body.parts.picture->source, *one.said.attachment->video, one.sender,
                              chat ? sender_name(*chat, one.sender) : one.sender,
                              std::format("{:%d.%m.%Y} at {}", std::chrono::year_month_day{day}, clock_of(one.said.at)));
          return true;
        }
        if (one.parts.body.parts.picture && one.parts.body.parts.picture->bounds().contains(press.x, press.y)) {
          const conversation* chat = seen_model && seen_chat ? seen_model->find(*seen_chat) : nullptr;
          const auto day = std::chrono::floor<std::chrono::days>(one.said.at);
          actions->open_picture(one.parts.body.parts.picture->source, one.sender,
                                chat ? sender_name(*chat, one.sender) : one.sender,
                                std::format("{:%d.%m.%Y} at {}", std::chrono::year_month_day{day}, clock_of(one.said.at)));
          return true;
        }
        // A picture of an album: seen whole, as one alone is.
        if (one.parts.body.parts.album)
          for (const auto& row : one.parts.body.parts.album->parts.rows)
            for (const picture_view& cell : row.parts.cells)
              if (cell.bounds().contains(press.x, press.y)) {
                const conversation* chat = seen_model && seen_chat ? seen_model->find(*seen_chat) : nullptr;
                const auto day = std::chrono::floor<std::chrono::days>(one.said.at);
                actions->open_picture(cell.source, one.sender, chat ? sender_name(*chat, one.sender) : one.sender,
                                      std::format("{:%d.%m.%Y} at {}", std::chrono::year_month_day{day},
                                                  clock_of(one.said.at)));
                return true;
              }
        if (one.parts.body.parts.file && one.parts.body.parts.file->bounds().contains(press.x, press.y) && one.said.attachment) {
          if (one.parts.body.parts.file->sound)
            actions->play_audio(one.parts.body.parts.file->source);
          else
            actions->open_file(one.parts.body.parts.file->source, one.said.attachment->name);
          return true;
        }
        // A reaction's chip: the user's own put or taken back.
        if (one.parts.body.parts.reactions)
          for (const reaction_chip& chip : one.parts.body.parts.reactions->chips())
            if (chip.bounds().contains(press.x, press.y)) {
              actions->react(one.message_id, chip.key);
              return true;
            }
        // A card of a link to a room or a message: followed.
        for (const link_card& card : one.parts.body.parts.cards)
          if (card.bounds().contains(press.x, press.y)) {
            actions->open_url(card.url);
            return true;
          }
        // A link's preview: the link, followed.
        if (const auto& preview = one.parts.body.parts.preview; preview && preview->bounds().contains(press.x, press.y)) {
          actions->open_url(preview->url);
          return true;
        }
        // A reaction shown as a line: pressed anywhere, to what it is on.
        if (one.said.service && one.said.replies_to && one.parts.body.bounds().contains(press.x, press.y) &&
            std::visit(overloaded{[](room_event::reactions) { return true; }, [](const auto&) { return false; }},
                       one.said.event_kind)) {
          actions->jump_to_message(*one.said.replies_to);
          return true;
        }
        // A quoted stretch of a reply's text -- the part of the message it
        // answers, as "> " quotes it: to that message, the part marked, as
        // the reply's own quote goes.
        if (const auto& text = one.parts.body.parts.text;
            one.said.replies_to && text.visible() && text.bounds().contains(press.x, press.y) && !text.hasSelection() &&
            text.quotedAt(press.x, press.y)) {
          actions->jump_to_message(*one.said.replies_to, quoted_fragment(one.said));
          return true;
        }
        // The quote: to the message it quotes.
        if (one.parts.body.parts.quote && one.said.replies_to && one.parts.body.parts.quote->bounds().contains(press.x, press.y)) {
          actions->jump_to_message(*one.said.replies_to, quoted_fragment(one.said));
          return true;
        }
        // The sender, by their avatar or their name: their page.
        if ((one.parts.face.visible() && one.parts.face.fState.fAlpha > 0.0f && one.parts.face.bounds().contains(press.x, press.y)) ||
            (one.parts.body.parts.name && one.parts.body.parts.name->bounds().contains(press.x, press.y))) {
          actions->open_member_info(one.sender);
          return true;
        }
      }
    return false;
  }
  void onPointer(scene::phase::bubble, const scene::pointer::down& press, scene::PointerReply& reply) {
    if (press.button == 1)
      return;  // the main button's presses come as clicks, above
    if (press.button != 3)
      return;
    // Whichever message's row the press is in -- its text, its bubble or the
    // room beside it. What Copy takes is what is selected in it, if anything
    // is, and all of it if not.
    for (const message_bubble& one : this->bubbles())
      if (one.bounds().contains(press.x, press.y)) {
        menu_facts facts;
        facts.id = one.message_id;
        facts.own = one.outgoing;
        facts.text = one.plain;
        facts.selection = one.parts.body.parts.text.hasSelection();
        facts.copied = facts.selection ? one.parts.body.parts.text.selected() : one.plain;
        facts.seen = this->seen_by(one.message_id, one.sender);
        if (one.said.attachment) {
          facts.media = one.said.attachment->video.value_or(one.said.attachment->source);
          facts.media_name = one.said.attachment->name;
          facts.moving = moves(one.said.attachment->kind);
        }
        if (const conversation* chat = seen_model && seen_chat ? seen_model->find(*seen_chat) : nullptr) {
          facts.pinned = std::ranges::contains(chat->pinned, one.message_id);
          facts.pinnable = is_matrix(chat->id.account.speaks) && one.message_id.starts_with('$');
          facts.reaction_events = !one.said.reaction_events.empty();
          for (const auto& [key, who] : one.said.reactions)
            facts.reaction_count += who.size();
        }
        // A Matrix message's link: matrix.to, to it in its room.
        if (seen_chat && is_matrix(seen_chat->account.speaks) &&
            one.message_id.starts_with('$'))
          if (const conversation* chat = seen_model ? seen_model->find(*seen_chat) : nullptr)
            facts.link = logic::message_link(*chat, one.message_id);
        facts.x = press.x;
        facts.y = press.y;
        actions->message_menu(std::move(facts));
        reply.handle();
        return;
      }
  }
};

}  // namespace mux::ui
