// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:timeline -- A chat's messages.
export module mux.ui:timeline;

import std;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes.flow;
import skiff.nodes.scroll;
import skiff.nodes.text;
import skiff.widgets.loader;
import skiff.widgets.wallpaper;
import mux.core;
import mux.video;
import mux.config;
import mux.logic.links;
import mux.protocols;
import :base;
import :names;
import :message;
import :composer;
import :themes;

export namespace mux::ui {

// The messages, and over them, where one has scrolled up from the newest,
// the way back down. Each is placed by its own spec.
// What a message's menu is made from: the message, and what it carries.
// One who has read a message: who, by their name in the chat, and when,
// where their receipt says.
struct seen_reader {
  std::string id;
  std::string name;
  std::optional<std::chrono::sys_time<std::chrono::milliseconds>> at;
};
struct menu_facts {
  std::string id;
  bool own = false;
  std::string text;    // all of it
  std::string copied;  // what Copy takes: the selection, or all of it
  bool selection = false;
  std::vector<seen_reader> seen;
  std::optional<std::string> media;  // a picture's or a file's source
  std::optional<std::string> picture;  // a picture's source, or a video's thumbnail's: what Copy Image copies
  bool captioned = false;  // a picture whose caption may be edited (not a video's)
  std::string media_name;
  bool moving = false;  // a GIF or a moving WebP: one that can be saved to the GIFs
  bool pinned = false;  // pinned in its chat: the menu offers Unpin
  bool pinnable = false;  // in a chat where pins are kept: a Matrix room
  bool editable = false;  // one's own, as its protocol's rule for edits allows
  proto::account_ops can;  // what its account does: React, Forward, threads...
  bool deletable = false;  // one may take it away: one's own, or another's with the power to
  bool reaction_events = false;  // reacted to, the reactions being events
  std::size_t reaction_count = 0;  // how many reactions it has, of anyone
  std::string link;  // a link to it, where it has one
  std::string pressed_link;  // the link pressed on: in its text, or its preview
  std::optional<emote> sticker;  // a sticker's: what making it a favourite keeps
  // A reaction's: the message it is on, and its key -- the menu's reactions
  // change it to another, where it is one's own.
  struct reaction_facts {
    std::string to;
    std::string key;
  };
  std::optional<reaction_facts> reaction;
  float x = 0.0f, y = 0.0f;
};

// A chat's background shown on a wallpaper: the theme's gradient and
// Telegram's pattern, a plain colour (what is behind showing), or a picture.
inline void show_wallpaper_on(wallpaper_t& wall, const config::wallpaper_t& chosen) {
  // Frosted's blur, as chosen: 0 to 100 for none to about five pixels.
  wall.setBlur(static_cast<float>(window_look().frost) / 100.0f);
  // And each look's and element's own, where it frosts: made once for a size.
  std::vector<float> blurs;
  for (const config::bubble_look* look : {&bubble_look_now(), &panel_look_now(), &bubble_look_everywhere(), &panel_look_everywhere()})
    if (frosts(*look)) {
      blurs.push_back(blur_of(*look));
      for (const auto& [name, member] : config::kElementBlurNames)
        blurs.push_back(element_blur_of(*look, member));
    }
  wall.setBlurs(std::move(blurs));
  splice::visit(splice::overloaded{[&](config::wallpaper::theme) {
                                     wall.setPicture(nullptr);
                                     wall.setGradient(scene::Gradient{chat_top_colour, chat_colour});
                                     wall.setPattern(telegram_pattern(), pattern_colour);
                                   },
                                   [&](config::wallpaper::plain) {
                                     wall.setPicture(nullptr);
                                     wall.setGradient(std::nullopt);
                                     wall.setPattern(nullptr, 0);
                                   },
                                   [&](const config::wallpaper::picture& at) {
                                     wall.setGradient(std::nullopt);
                                     wall.setPattern(nullptr, 0);
                                     wall.setPicture(wallpaper_picture(at.path));
                                   }},
                chosen);
}

// A press on what is in a message -- a picture, a file, a reply's quote,
// its sender -- as a click, wherever the message is shown: the timeline, a
// thread. The press in the space its bubble is laid out in.
template <class Actions>
[[nodiscard]] bool press_in_bubble(Actions* actions, const message_bubble<Actions>& one, float x, float y, const conversation* chat) {
  const struct {
    float x, y;
  } press{x, y};
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
    
    const auto day = std::chrono::floor<std::chrono::days>(one.said.at);
    actions->open_video(one.parts.body.parts.picture->source, *one.said.attachment->video, one.sender,
                        chat ? sender_name(*chat, one.sender) : one.sender,
                        std::format("{:%d.%m.%Y} at {}", std::chrono::year_month_day{day}, clock_of(one.said.at)));
    return true;
  }
  if (one.parts.body.parts.picture && one.parts.body.parts.picture->bounds().contains(press.x, press.y)) {
    
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
  // A thread's summary under its root: the thread, beside the chat.
  if (const auto& thread = one.parts.body.parts.thread; thread && thread->bounds().contains(press.x, press.y)) {
    actions->open_thread(one.message_id);
    return true;
  }
  // A reaction shown as a line: pressed anywhere, to what it is on.
  if (one.said.service && one.said.replies_to && one.parts.body.bounds().contains(press.x, press.y) &&
      splice::visit(splice::overloaded{[](room_event::reactions) { return true; },
                                       [](room_event::unreactions) { return true; }, [](const auto&) { return false; }},
                 one.said.event_kind)) {
    actions->jump_to_message(*one.said.replies_to, std::nullopt, one.message_id);
    return true;
  }
  // A quoted stretch of a reply's text -- the part of the message it
  // answers, as "> " quotes it: to that message, the part marked, as
  // the reply's own quote goes.
  if (const auto& text = one.parts.body.parts.text;
      one.said.replies_to && text.visible() && text.bounds().contains(press.x, press.y) && !text.hasSelection()) {
    // The quote pressed, of those the reply has: its own words marked.
    if (const auto quote = text.quoteAt(press.x, press.y)) {
      actions->jump_to_message(*one.said.replies_to,
                               trimmed_fragment(std::string_view(text.text()).substr(quote->first, quote->second - quote->first)),
                               one.message_id);
      return true;
    }
  }
  // The reply's header: to the message it answers, as it is -- a part
  // marked there only by a click on the quoted stretch itself.
  // Where it is shown: beside a sticker it is moved there from its layout.
  if (one.parts.body.parts.quote && one.said.replies_to && one.parts.body.parts.quote->shownBounds().contains(press.x, press.y)) {
    // Where the header shows the quote itself, the quoted part marked.
    if (one.header_quote)
      actions->jump_to_message(*one.said.replies_to, one.header_quote, one.message_id);
    else
      actions->jump_to_message(*one.said.replies_to, std::nullopt, one.message_id);
    return true;
  }
  // A forward's line: its sender's pill, their page; its words, the
  // original, where its link is.
  if (one.parts.body.parts.forwarded && one.said.forwarded &&
      one.parts.body.parts.forwarded->shownBounds().contains(press.x, press.y)) {
    if (proto::person_link(state_before(protocol_of(one.said.forwarded->from)), one.said.forwarded->from) &&
        one.parts.body.parts.forwarded->parts.who.bounds()
            .makeOffset(one.parts.body.parts.forwarded->fState.fShiftX, one.parts.body.parts.forwarded->fState.fShiftY)
            .contains(press.x, press.y)) {
      actions->open_member_info(one.said.forwarded->from);
      return true;
    }
    if (!one.said.forwarded->link.empty()) {
      actions->open_url(one.said.forwarded->link);
      return true;
    }
  }
  // The sender, by their avatar or their name: their page.
  if ((one.parts.face.visible() && one.parts.face.fState.fAlpha > 0.0f && one.parts.face.bounds().contains(press.x, press.y)) ||
      (one.parts.body.parts.name && one.parts.body.parts.name->bounds().contains(press.x, press.y))) {
    actions->open_member_info(one.sender);
    return true;
  }
  return false;
}

// What a message's menu offers, for a right press on it wherever it is
// shown: the timeline, a thread.
template <class Actions>
[[nodiscard]] menu_facts facts_of_bubble(const message_bubble<Actions>& one, const conversation* chat, float x, float y) {
  const struct {
    float x, y;
  } press{x, y};
  menu_facts facts;
  facts.id = one.message_id;
  facts.own = one.outgoing;
  facts.text = one.plain;
  facts.selection = one.parts.body.parts.text.hasSelection();
  facts.copied = facts.selection ? one.parts.body.parts.text.selected() : one.plain;
  if (one.said.attachment) {
    facts.media = one.said.attachment->video.value_or(one.said.attachment->source);
    facts.media_name = one.said.attachment->name;
    facts.moving = moves(one.said.attachment->kind);
    if (is_picture(one.said.attachment->kind) || one.said.attachment->video)
      facts.picture = one.said.attachment->source;
    facts.captioned = is_picture(one.said.attachment->kind) && !one.said.attachment->video;
  }
  if (chat) {
    facts.pinned = std::ranges::contains(chat->pinned, one.message_id);
    const protocol_state_t now = protocol_state_of(chat->id.account);
    // What its account does, offered only where anything may be done now.
    if (proto::available(now))
      facts.can = ops_of(chat->id.account);
    facts.pinnable = proto::can_pin(now, one.message_id);
    // Edited as its protocol's rule allows: any of one's own, or the last.
    facts.editable = proto::may_edit(now, *chat, one.said);
    // Delete as the protocol allows it -- Matrix: as the room's power levels do.
    facts.deletable = proto::may_delete(now, *chat, one.outgoing);
    facts.reaction_events = !one.said.reaction_events.empty();
    for (const auto& [key, who] : one.said.reactions)
      facts.reaction_count += who.size();
  }
  // The message's link, where its protocol has one.
  if (chat)
    if (auto link = proto::message_link(protocol_state_of(chat->id.account), *chat, one.message_id))
      facts.link = std::move(*link);
  // The link the press was on: one in the text -- its text asked a menu
  // of its own with it, which this one is in place of -- or the
  // preview's.
  if (const auto& asked = skiff::nodes::textMenusAsked(); !asked.empty() && asked.back().link)
    facts.pressed_link = *asked.back().link;
  else if (const auto& preview = one.parts.body.parts.preview;
           preview && preview->fState.fBounds.contains(press.x, press.y))
    facts.pressed_link = preview->url;
  // A sticker: what sending it again takes.
  if (one.said.sticker && one.said.attachment)
    facts.sticker = emote{.shortcode = one.said.attachment->name,
                          .url = one.said.attachment->source,
                          .body = one.said.attachment->name,
                          .w = one.said.attachment->width > 0 ? std::optional<std::int64_t>(one.said.attachment->width) : std::nullopt,
                          .h = one.said.attachment->height > 0 ? std::optional<std::int64_t>(one.said.attachment->height) : std::nullopt,
                          .mimetype = one.said.attachment->mimetype.empty() ? std::nullopt
                                                                            : std::optional<std::string>(one.said.attachment->mimetype)};
  // A reaction shown as a line: what it is on, and with what.
  if (!one.said.reaction_key.empty() && one.said.replies_to)
    facts.reaction = menu_facts::reaction_facts{*one.said.replies_to, one.said.reaction_key};
  facts.x = press.x;
  facts.y = press.y;
  return facts;
}

// How a stretch of messages is shown: which room events, whether readers
// and link previews, and where the unread start.
struct shown_how {
  room_event_filter filter;
  bool receipts = false;
  bool previews = true;
  std::optional<std::string> unread_from;
};

template <class Actions>
struct timeline_area : scene::Node {
  // The loader's cross: the message jumped to no longer looked for.
  struct stop_jump {
    Actions* actions = nullptr;
    void operator()() const { actions->stop_jump(); }
  };
  struct parts_t {
    // Behind the messages: the theme's gradient, Telegram's pattern over it.
    wallpaper_t wall;
    nodes::ScrollContainer<nodes::Flow<std::vector<message_bubble<Actions>>>> timeline{
        nodes::Flow<std::vector<message_bubble<Actions>>>({.spacingY = 0.0f, .wrap = false}, {})};
    jump_button<Actions> jump;
    back_button<Actions> back;
    mark_button<Actions> mentions;
    mark_button<Actions> reactions;
    // While a message jumped to is being fetched: turning in the middle,
    // its cross stopping the search.
    widgets::RadialLoader<stop_jump> loading;
  } parts;
  Actions* actions = nullptr;
  explicit timeline_area(Actions* a)
      : parts{.jump = jump_button<Actions>(a),
              .back = back_button<Actions>(a),
              .mentions = mark_button<Actions>(a, mark_kind::mention{}, "@"),
              .reactions = mark_button<Actions>(a, mark_kind::reaction{}, "\u2665"),
              .loading = widgets::RadialLoader<stop_jump>(44.0f, {a})},
        actions(a) {
    parts.wall.apply({.fill = true});
    this->show_wallpaper(config::wallpaper::theme{});
    parts.timeline.apply({.fill = true});
    // Over the wallpaper's gradient, which stays where it is: a scroll step
    // repainted, not copied.
    parts.timeline.setCopiesOnScroll(false);
    // The room around the messages is inside what scrolls, so the bar is at
    // the window's edge.
    std::get<0>(parts.timeline.fChildren).apply(
        {.fillX = true,
         .autoSize = scene::axes::kY,
         .padding = {8.0f, message_bubble<Actions>::kListSide, 8.0f, message_bubble<Actions>::kListSide}});
    parts.jump.setVisible(false);
    parts.loading.apply({.place = scene::anchor::kCentre});
    parts.loading.setVisible(false);
  }
  // The chat's background: the theme's gradient and Telegram's pattern, a
  // plain colour (what is behind showing), or a picture.
  // Not here where it is behind the whole window: the window's shows
  // through. Else at the window's opacity, as the panels are.
  void show_wallpaper(const config::wallpaper_t& chosen) {
    parts.wall.setVisible(!window_look().behind);
    parts.wall.setOpacity(static_cast<float>(window_look().opacity) / 100.0f);
    show_wallpaper_on(parts.wall, chosen);
  }
  // The bubbles in the list, as they are made.
  [[nodiscard]] std::vector<message_bubble<Actions>>& bubbles() {
    return std::get<0>(std::get<0>(parts.timeline.fChildren).fChildren);
  }
  // The bubbles, as a function of the messages from first to last of
  // all -- a chat's timeline, or a thread's root and answers: those that
  // show the same are kept, with a selection in them, and only the new are
  // made. Where each is in its sender's run (the first has the name, the
  // last the avatar), who has read up to it, what its quote says now.
  // Arrives(i): whether the bubble of all[i] comes in moving.
  template <class Arrives>
  void show_messages(const conversation& one, const std::vector<message>& all, std::size_t first_made, std::size_t last_made,
                     const model& now, const shown_how& how, Arrives arrives, std::set<std::string>& rooms_wanted) {
    auto& entries = this->bubbles();
    const auto shows = [&](const message& said) { return !said.service || how.filter.shows(said.event_kind); };
    const auto neighbour = [&](std::size_t i, bool forward) -> std::optional<std::size_t> {
      for (std::size_t j = i;;) {
        if (forward ? j + 1 >= all.size() : j == 0)
          return std::nullopt;
        j = forward ? j + 1 : j - 1;
        if (shows(all[j]))
          return j;
      }
    };
    const auto same = [&](std::size_t i, std::optional<std::size_t> j) {
      return j && !all[i].service && !all[*j].service && all[*j].sender == all[i].sender &&
             all[*j].outgoing == all[i].outgoing;
    };
    // Who has read up to where, where the chat shows it: each other person
    // on the message their receipt points at -- or, pointing at what is not
    // shown or not here, the nearest shown before it, by its time.
    std::map<std::string, std::vector<std::string>> readers;
    if (how.receipts) {
      std::map<std::string, std::size_t> place;
      for (std::size_t i = 0; i < all.size(); ++i)
        place.emplace(all[i].id, i);
      for (const auto& [user, event] : one.read_by) {
        if (user == one.id.account.address)
          continue;
        std::optional<std::size_t> at;
        if (const auto found = place.find(event); found != place.end())
          at = found->second;
        else if (const auto when = one.receipt_times.find(user); when != one.receipt_times.end())
          for (std::size_t j = all.size(); j-- > 0;)
            if (all[j].at <= when->second) {
              at = j;
              break;
            }
        while (at && !shows(all[*at]))
          at = *at == 0 ? std::nullopt : std::optional<std::size_t>(*at - 1);
        if (at)
          readers[all[*at].id].push_back(user);
      }
    }
    const auto readers_of = [&](std::size_t i) {
      const auto found = readers.find(all[i].id);
      return found == readers.end() ? std::vector<std::string>{} : found->second;
    };
    const auto first_of_run = [&](std::size_t i) { return !same(i, neighbour(i, false)); };
    const auto last_of_run = [&](std::size_t i) { return !same(i, neighbour(i, true)); };
    // What the message a bubble replies to says now, where it is held: a
    // bubble quoting it is made again as it changes -- edited, deleted --
    // not left quoting what it said once.
    const auto quote_body = [&](std::size_t i) -> std::optional<decltype(message::body)> {
      if (!all[i].replies_to)
        return std::nullopt;
      if (const message* said = held_message(one, *all[i].replies_to))
        return said->body;
      return std::nullopt;
    };
    // The bubbles, as a function of the messages: those that show the same
    // are kept -- with a selection in them -- and only the new are made.
    if (nodes::reconcile(
            entries, std::views::iota(first_made, last_made),
            [&](std::size_t i) { return all[i].id; }, [](const message_bubble<Actions>& row) { return row.message_id; },
            [&](std::size_t i) {
              message_bubble<Actions> made(one, all[i], first_of_run(i), last_of_run(i), &now, shows(all[i]),
                                  how.previews);
              made.quote_said = quote_body(i);
              if (how.unread_from && all[i].id == *how.unread_from)
                made.mark_unread_start();
              made.show_readers(one, readers_of(i));
              rooms_wanted.insert(made.rooms_unknown.begin(), made.rooms_unknown.end());
              if (arrives(i))
                made.appear();
              return made;
            },
            [&](const message_bubble<Actions>& row, std::size_t i) {
              const bool quote_known = !all[i].replies_to || one.quoted.contains(*all[i].replies_to) ||
                                       std::ranges::find(all, *all[i].replies_to, &message::id) != all.end();
              const auto link = first_link_of(all[i]);
              const bool preview_known = link && now.previews.contains(*link);
              return row.said == all[i] && row.quote_said == quote_body(i) && row.first == first_of_run(i) &&
                     row.last == last_of_run(i) &&
                     row.quote_known == quote_known && row.events_shown == shows(all[i]) && row.unread_start == (how.unread_from && all[i].id == *how.unread_from) &&
                     row.preview_known == preview_known && row.readers_shown == readers_of(i) &&
                     row.previews_shown == how.previews && !row.rooms_came();
            }))
      // Laid out again; painted where rows came, went or moved -- a hidden
      // one coming moves nothing, and paints nothing.
      std::get<0>(parts.timeline.fChildren).fState.relayoutQuietly();
  }
  // A message swiped left: watched from above, before the list's scrolling
  // and a text's selecting see the pointer. A press that moves left at once,
  // and more across than up or down, takes the pointer and moves the
  // message; let go past its mark, it is answered; either way it goes back.
  std::optional<std::string> swiping;
  bool swipe_armed = false;
  float swipe_x = 0.0f, swipe_y = 0.0f;
  std::chrono::steady_clock::time_point swipe_pressed{};
  message_bubble<Actions>* swiped() {
    if (!swiping)
      return nullptr;
    auto& entries = this->bubbles();
    const auto it = std::ranges::find(entries, *swiping, &message_bubble<Actions>::message_id);
    return it == entries.end() ? nullptr : &*it;
  }
  void swipe_down(const scene::pointer::down& press) {
    swipe_armed = press.button <= 1;
    swipe_x = press.x;
    swipe_y = press.y;
    swipe_pressed = std::chrono::steady_clock::now();
  }
  void swipe_move(const scene::pointer::move& at, scene::PointerReply& reply) {
    if (message_bubble<Actions>* one = this->swiped()) {
      one->swipe.jump(std::clamp(at.x - swipe_x, -120.0f, 0.0f));
      one->markDamaged();
      reply.handle();
      return;
    }
    if (!swipe_armed)
      return;
    // Something holds the pointer already -- a text being selected: this
    // drag is its, not a swipe.
    if (reply.fCaptured) {
      swipe_armed = false;
      return;
    }
    const float dx = at.x - swipe_x, dy = at.y - swipe_y;
    if (std::abs(dx) < 8.0f && std::abs(dy) < 8.0f)
      return;
    swipe_armed = false;
    if (dx >= 0.0f || std::abs(dx) < 2.0f * std::abs(dy) ||
        std::chrono::steady_clock::now() - swipe_pressed > std::chrono::milliseconds(250))
      return;
    for (message_bubble<Actions>& one : this->bubbles())
      // The rows are laid out as if unscrolled: the press, where they are.
      if (parts.timeline.toView(one.bounds()).contains(swipe_x, swipe_y) && !one.message_id.empty()) {
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
    if (message_bubble<Actions>* one = this->swiped()) {
      if (one->swipe.value() <= -message_bubble<Actions>::kSwipeToReply)
        actions->reply_to(one->message_id, one->plain);
      one->swipe.setTarget(0.0f);
      scene::work::mark(one->fState.fId);  // ticked back: nothing else asks for its frames
      swiping.reset();
      reply.releasePointer();
      reply.handle();
    }
  }
  void swipe_cancel(scene::PointerReply& reply) {
    swipe_armed = false;
    if (message_bubble<Actions>* one = this->swiped()) {
      one->swipe.setTarget(0.0f);
      scene::work::mark(one->fState.fId);  // ticked back: nothing else asks for its frames
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
  std::vector<seen_reader> seen_by(const std::string& id, const std::string& sender) const {
    std::vector<seen_reader> out;
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
      const auto read = chat->receipt_times.find(user);
      const auto read_at = read == chat->receipt_times.end() ? std::nullopt : std::optional(read->second);
      if (const auto theirs = at.find(event); theirs != at.end()) {
        if (theirs->second >= mine->second)
          out.push_back({user, sender_name(*chat, user), read_at});
      } else if (read_at && *read_at >= when) {
        out.push_back({user, sender_name(*chat, user), read_at});
      }
    }
    std::ranges::sort(out, {}, &seen_reader::name);
    return out;
  }

  // A right press on a message: its menu, where it was pressed.
  using Node::onPointer;
  // A press on what is in a message -- a picture, a file, a reply's quote,
  // its sender -- as a click: it comes here from what was pressed when that
  // did not take it, at once or, in a list that scrolls, on the release.
  [[nodiscard]] const conversation* seen_chat_of() const { return seen_model && seen_chat ? seen_model->find(*seen_chat) : nullptr; }
  [[nodiscard]] bool onClick(float x, float y) {
    // In the space the rows are laid out in: the list draws them scrolled.
    const struct {
      float x, y;
    } press{x, y - parts.timeline.contentsShift()};
      for (const message_bubble<Actions>& one : this->bubbles()) {
        if (press_in_bubble(actions, one, press.x, press.y, seen_chat_of()))
          return true;
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
    for (const message_bubble<Actions>& one : this->bubbles())
      if (parts.timeline.toView(one.bounds()).contains(press.x, press.y)) {
        menu_facts facts = facts_of_bubble(one, seen_chat_of(), press.x, press.y);
        facts.seen = this->seen_by(one.message_id, one.sender);
        actions->message_menu(std::move(facts));
        reply.handle();
        return;
      }
  }
};

}  // namespace mux::ui
