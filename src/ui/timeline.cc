// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:timeline -- A chat's messages.
export module mux.ui.messages:timeline;

import std;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.compose;
import skiff.nodes.flow;
import skiff.nodes.scroll;
import skiff.nodes.text;
import skiff.widgets.loader;
import skiff.widgets.wallpaper;
import mux.core;
import mux.platform.video;
import mux.config;
import mux.logic.links;
import mux.protocols;
import mux.ui.base;
import mux.plugins.ui;
import mux.ui.proto;
import :message;
import :composer;

export namespace mux::ui {

// The messages, and over them, where one has scrolled up from the newest,
// the way back down. Each is placed by its own spec.
// What a message's menu is made from: the message, and what it carries.

// A chat's background shown on a wallpaper: the theme's gradient and
// Telegram's pattern, a plain colour (what is behind showing), or a picture.
inline void show_wallpaper_on(wallpaper_t& wall, const config::wallpaper_t& chosen, const palette& colours, const looks_shown& looks) {
  // Frosted's blur, as chosen: 0 to 100 for none to about five pixels.
  wall.setBlur(static_cast<float>(looks.window.frost) / 100.0f);
  // And each look's and element's own, where it frosts: made once for a size.
  std::vector<float> blurs;
  for (const config::bubble_look* look : {&looks.bubbles, &looks.panels, &looks.bubbles_everywhere, &looks.panels_everywhere})
    if (frosts(*look)) {
      blurs.push_back(blur_of(*look, looks.window));
      for (const auto& [name, member] : config::kElementBlurNames)
        blurs.push_back(element_blur_of(*look, member, looks.window));
    }
  wall.setBlurs(std::move(blurs));
  spl::visit(spl::overloaded{[&](config::wallpaper::theme) {
                                     wall.setFreeform({});
                                     wall.setPicture(nullptr);
                                     wall.setGradient(scene::Gradient{colours.chat_top, colours.chat});
                                     wall.setPattern(telegram_pattern(), colours.pattern);
                                   },
                                   // As tdesktop's default background without its
                                   // pattern: its four colours, in Telegram's
                                   // freeform gradient (Data::DefaultWallPaper).
                                   [&](config::wallpaper::plain) {
                                     wall.setPicture(nullptr);
                                     wall.setGradient(std::nullopt);
                                     wall.setPattern(nullptr, 0);
                                     wall.setFreeform({skia::colorSetARGB(255, 219, 221, 187), skia::colorSetARGB(255, 107, 165, 135),
                                                       skia::colorSetARGB(255, 213, 216, 141), skia::colorSetARGB(255, 136, 184, 132)});
                                   },
                                   [&](const config::wallpaper::picture& at) {
                                     wall.setFreeform({});
                                     wall.setGradient(std::nullopt);
                                     wall.setPattern(nullptr, 0);
                                     wall.setPicture(wallpaper_picture(at.path));
                                   }},
                chosen);
}

// A press on what is in a message -- a picture, a file, a reply's quote,
// its sender -- as a click, wherever the message is shown: the timeline, a
// thread. The press in the space its bubble is laid out in.
// What a press in a bubble asks for: the picture or video seen, the file
// opened, a reaction, a link followed, the thread, the message replied to,
// the person.
using bubble_press = std::variant<::mux::ui::request::open_file, ::mux::ui::request::open_video, ::mux::ui::request::open_picture, ::mux::ui::request::play_audio, ::mux::ui::request::react, ::mux::ui::request::open_url,
                                  ::mux::ui::request::open_thread, ::mux::ui::request::jump_to_message, ::mux::ui::request::open_member_info>;
[[nodiscard]] std::optional<bubble_press> press_in_bubble(const legacy::message_bubble& one, float x, float y, const conversation* chat) {
  const struct {
    float x, y;
  } press{x, y};
  // A card of a link to a room or a message: followed.
  if (const auto url = message_link_at(one.parts.body.parts.blocks, press.x, press.y))
    return ::mux::ui::request::open_url{*url};
  // A thread's summary under its root: the thread, beside the chat.
  if (const auto& thread = one.parts.body.parts.thread; thread && thread->bounds().contains(press.x, press.y)) {
    return ::mux::ui::request::open_thread{one.message_id};
  }
  // A reaction shown as a line: pressed anywhere, to what it is on.
  if (one.said.service && one.said.replies_to && one.parts.body.bounds().contains(press.x, press.y) &&
      spl::visit(spl::overloaded{[](room_event::reactions) { return true; },
                                       [](room_event::unreactions) { return true; }, [](const auto&) { return false; }},
                 one.said.event_kind)) {
    return ::mux::ui::request::jump_to_message{*one.said.replies_to, std::nullopt, one.message_id};
  }
  // A quoted stretch of a reply's text -- the part of the message it
  // answers, as "> " quotes it: to that message, the part marked, as
  // the reply's own quote goes.
  if (const auto& text = one.parts.body.parts.text;
      one.said.replies_to && text.visible() && text.bounds().contains(press.x, press.y) && !text.hasSelection()) {
    // The quote pressed, of those the reply has: its own words marked.
    if (const auto quote = text.quoteAt(press.x, press.y)) {
      return ::mux::ui::request::jump_to_message{*one.said.replies_to,
                               trimmed_fragment(std::string_view(text.text()).substr(quote->first, quote->second - quote->first)),
                               one.message_id};
    }
  }
  // The reply's header: to the message it answers, as it is -- a part
  // marked there only by a click on the quoted stretch itself.
  // Where it is shown: beside a sticker it is moved there from its layout.
  if (one.parts.body.parts.quote && one.said.replies_to && one.parts.body.parts.quote->shownBounds().contains(press.x, press.y)) {
    // Where the header shows the quote itself, the quoted part marked.
    if (one.header_quote)
      return ::mux::ui::request::jump_to_message{*one.said.replies_to, one.header_quote, one.message_id};
    else
      return ::mux::ui::request::jump_to_message{*one.said.replies_to, std::nullopt, one.message_id};
  }
  // A forward's line: its sender's pill, their page; its words, the
  // original, where its link is.
  if (one.parts.body.parts.forwarded && one.said.forwarded &&
      one.parts.body.parts.forwarded->shownBounds().contains(press.x, press.y)) {
    if (proto::person_link(state_before(protocol_of(one.said.forwarded->from)), one.said.forwarded->from) &&
        std::get<1>(one.parts.body.parts.forwarded->fParts).bounds()
            .makeOffset(one.parts.body.parts.forwarded->fState.fShiftX, one.parts.body.parts.forwarded->fState.fShiftY)
            .contains(press.x, press.y)) {
      return ::mux::ui::request::open_member_info{one.said.forwarded->from};
    }
    if (!one.said.forwarded->link.empty()) {
      return ::mux::ui::request::open_url{one.said.forwarded->link};
    }
  }
  // The sender, by their avatar or their name: their page.
  if ((one.parts.face.visible() && one.parts.face.fState.fAlpha > 0.0f && one.parts.face.bounds().contains(press.x, press.y)) ||
      (one.parts.body.parts.name && one.parts.body.parts.name->bounds().contains(press.x, press.y))) {
    return ::mux::ui::request::open_member_info{one.sender};
  }
  return std::nullopt;
}

// What a message's menu offers, for a right press on it wherever it is
// shown: the timeline, a thread.
[[nodiscard]] menu_facts facts_of_bubble(const legacy::message_bubble& one, const conversation* chat, float x, float y) {
  const struct {
    float x, y;
  } press{x, y};
  menu_facts facts;
  facts.id = one.message_id;
  if (chat && !facts.id.empty() && !held_message(*chat, facts.id)) facts.event = one.said;
  facts.own = one.outgoing;
  facts.text = one.plain;
  facts.selection = one.parts.body.parts.text.hasSelection();
  if (facts.selection) facts.copied_fragment = one.parts.body.parts.text.selectedFragment();
  facts.copied = facts.copied_fragment ? facts.copied_fragment->text : one.plain;
  if (one.said.attachment) {
    facts.media = one.said.attachment->video.value_or(one.said.attachment->source);
    facts.media_name = one.said.attachment->name;
    facts.moving = moves(one.said.attachment->kind);
    if (is_picture(one.said.attachment->kind) || one.said.attachment->video)
      facts.picture = one.said.attachment->source;
    facts.captioned = is_picture(one.said.attachment->kind) && !one.said.attachment->video;
  }
  if (chat) {
    const message* held = held_message(*chat, one.message_id);
    const message& current = held ? *held : one.said;
    facts.pinned = std::ranges::contains(chat->pinned, one.message_id);
    const protocol_state_t now = protocol_state_of(*one.shared_, chat->id.account);
    // What its account does, offered only where anything may be done now.
    if (proto::available(now))
      facts.can = ops_of(*one.shared_, chat->id.account);
    facts.can.react = facts.can.react && proto::may_react(now, *chat, current);
    facts.pinnable = proto::can_pin(now, one.message_id);
    // Edited as its protocol's rule allows: any of one's own, or the last.
    facts.editable = !one.said.service && !one.said.reaction && proto::may_edit(now, *chat, one.said);
    facts.history = !one.said.versions.empty();
    // Delete as the protocol allows it -- Matrix: as the room's power levels do.
    facts.deletable = proto::may_delete(now, *chat, one.outgoing);
    facts.reaction_events = !current.reaction_events.empty();
    for (const auto& [key, who] : current.reactions)
      facts.reaction_count += who.size();
  }
  // The message's link, where its protocol has one.
  if (chat)
    if (auto link = proto::message_link(protocol_state_of(*one.shared_, chat->id.account), *chat, one.message_id))
      facts.link = std::move(*link);
  // The link the press was on: one in the text -- its text asked a menu
  // of its own with it, which this one is in place of -- or the
  // preview's.
  if (auto pressed = skiff::scene::pressedLink())
    facts.pressed_link = std::move(*pressed);
  else if (const auto& preview = one.parts.body.parts.preview;
           preview && preview->fState.fBounds.contains(press.x, press.y))
    facts.pressed_link = preview->fEvent.url;
  facts.emoji = copied_emoji_at(facts.pressed_link);
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

// Which of a list's messages are made into bubbles -- a chat's timeline, a
// thread's root and answers: a stretch of them, at most a few hundred, that
// slides with the reader -- more made above as the view nears its top, the
// far ones below let go -- and is kept by the messages at its ends, so that
// history coming in above does not move it. A whole long thread made at
// once -- every bubble wrapped, measured and laid out -- was what made one
// slow to open, and every change in it slow after.
// Slid a little at a time, well before the reader reaches its edge: sixty
// bubbles made in one frame was a frame of jank.
inline constexpr std::size_t kFirstMade = 80, kMostMade = 240, kMadeStep = 16;
struct made_range {
  std::optional<std::string> from, to;
  bool to_end = true;
  friend bool operator==(const made_range&, const made_range&) = default;
};
[[nodiscard]] inline std::pair<std::size_t, std::size_t> made_indices(const made_range& made, const std::vector<message>& all) {
  const auto index_of = [&](const std::optional<std::string>& id) -> std::optional<std::size_t> {
    if (!id)
      return std::nullopt;
    const auto it = std::ranges::find(all, *id, &message::id);
    return it == all.end() ? std::nullopt : std::optional<std::size_t>(static_cast<std::size_t>(it - all.begin()));
  };
  std::size_t to = all.size();
  if (!made.to_end)
    if (const auto at = index_of(made.to))
      to = *at + 1;
  std::size_t from = to > kFirstMade ? to - kFirstMade : 0;
  if (const auto at = index_of(made.from); at && *at <= to)
    from = *at;
  if (to - from > kMostMade)
    from = to - kMostMade;
  return {from, to};
}
// The stretch from `from` to `to`, kept by its ends: whether it is another.
[[nodiscard]] inline bool set_made(made_range& made, const std::vector<message>& all, std::size_t from, std::size_t to) {
  to = std::min(to, all.size());
  from = std::min(from, to);
  made_range next{from < all.size() ? std::optional<std::string>(all[from].id) : std::nullopt,
                  to > 0 ? std::optional<std::string>(all[to - 1].id) : std::nullopt, to == all.size()};
  const made_range was = std::exchange(made, std::move(next));
  return was != made;
}
// Slid as the view nears an edge of what is made, while a screen and a half
// is still made beyond it: up, where there is more above; down, where it is
// not at the end. Whether it moved.
[[nodiscard]] inline bool slide_made(made_range& made, const std::vector<message>& all, float at, float extent, float view) {
  auto [from, to] = made_indices(made, all);
  const float ahead = std::max(300.0f, view * 1.5f);
  if (at <= ahead && from > 0) {
    from = from > kMadeStep ? from - kMadeStep : 0;
    to = std::min(to, from + kMostMade);
    return set_made(made, all, from, to);
  }
  if (!made.to_end && at >= extent - ahead) {
    to = std::min(all.size(), to + kMadeStep);
    from = to > kMostMade && to - from > kMostMade ? to - kMostMade : from;
    return set_made(made, all, from, to);
  }
  return false;
}

// ---- the timeline as a window of its list ------------------------------------------
// What the timeline is told by the program (the shown model): which chat --
// or which thread of it, by its root -- how it shows it (its room events,
// readers, link previews, its first unread), and what to aim at.
struct timeline_told {
  std::optional<conversation_id> of;
  std::optional<std::string> thread;
  skiff::compose::WindowAim<std::string> aim;
  room_event_filter filter;
  bool receipts = false, previews = true;
  // What its rows are made with: the program's, handed down.
  const bubble_needs* needs = nullptr;
  std::optional<std::string> unread_from;
  double frost = 10.0;
  friend bool operator==(const timeline_told&, const timeline_told&) = default;
};
// Which of the two lists a list's answers are of.
struct chat_list_tag {};
struct thread_list_tag {};
// A row's item: its key, the facts it is made from, and what making it
// takes there and then.
using bubble_item = std::tuple<std::string, bubble_facts, const conversation*, const bubble_needs*, bool>;
// What the timeline makes its window of: a chat's timeline, or a thread's
// root and answers, read where the chats model keeps them; each window's rows
// worked out at once -- their runs, who has read up to each, what each one's
// quote says now.
struct timeline_rows {
  using Told = timeline_told;
  [[nodiscard]] static const conversation* chat_of(const chats_model& now, const Told& told) {
    if (!told.of)
      return nullptr;
    const auto* in = now.root().accounts.find(told.of->account);
    return in ? in->conversations.find(told.of->id) : nullptr;
  }
  // The thread's root and answers, or the chat's timeline: kept as they are,
  // seen, not copied -- the root found among what the chat holds.
  struct seen {
    const message* root = nullptr;
    const std::vector<message>* answers = nullptr;
    const std::vector<message>* timeline = nullptr;
    [[nodiscard]] std::size_t size() const {
      return timeline ? timeline->size() : (root ? 1u : 0u) + (answers ? answers->size() : 0u);
    }
    [[nodiscard]] const message& at(std::size_t i) const {
      if (timeline)
        return (*timeline)[i];
      if (root)
        return i == 0 ? *root : (*answers)[i - 1];
      return (*answers)[i];
    }
  };
  [[nodiscard]] static seen seen_of(const chats_model& now, const Told& told) {
    const conversation* chat = chat_of(now, told);
    if (!chat)
      return {};
    if (!told.thread)
      return {.timeline = &chat->timeline};
    const auto found = chat->threads.find(*told.thread);
    return {.root = held_message(*chat, *told.thread), .answers = found == chat->threads.end() ? nullptr : &found->second};
  }
  static skiff::model::Revision revision(const chats_model& now, const Told& told) {
    if (!told.of)
      return 0;
    const auto* in = now.root().accounts.findItem(told.of->account);
    const auto* one = in ? in->fValue.conversations.findItem(told.of->id) : nullptr;
    return one ? one->fRevision : 0;
  }
  static std::size_t count(const chats_model& now, const Told& told) { return seen_of(now, told).size(); }
  static std::string key(const chats_model& now, const Told& told, std::size_t i) { return seen_of(now, told).at(i).id; }
  static std::optional<std::size_t> index(const chats_model& now, const Told& told, const std::string& id) {
    const seen all = seen_of(now, told);
    const auto at = std::ranges::find(std::views::iota(std::size_t{0}, all.size()), id, [&](std::size_t i) { return all.at(i).id; });
    return *at < all.size() ? std::optional<std::size_t>(*at) : std::nullopt;
  }
  // The rooms each message names, worked out once for it: whether one has
  // come since is what makes its row again.
  [[nodiscard]] static const named_rooms& rooms_of(const conversation& in, const message& said, const model* now) {
    thread_local std::map<std::string, named_rooms> named;
    if (named.size() > 4096)
      named.clear();
    const auto found = named.find(said.id);
    if (found != named.end())
      return found->second;
    return named.emplace(said.id, rooms_named_in(in, said, now)).first->second;
  }
  static std::vector<bubble_item> items(const chats_model& now, const Told& told, std::size_t from, std::size_t to,
                                        const std::optional<std::string>& newest) {
    const conversation* chat = chat_of(now, told);
    const seen all = seen_of(now, told);
    if (!chat || told.needs == nullptr)
      return {};
    const model* program = told.needs->now;
    const auto shows = [&](const message& said) { return !said.service || told.filter.shows(said.event_kind); };
    const auto neighbour = [&](std::size_t i, bool forward) -> std::optional<std::size_t> {
      for (std::size_t j = i;;) {
        if (forward ? j + 1 >= all.size() : j == 0)
          return std::nullopt;
        j = forward ? j + 1 : j - 1;
        if (shows(all.at(j)))
          return j;
      }
    };
    const auto same = [&](std::size_t i, std::optional<std::size_t> j) {
      return j && !all.at(i).service && !all.at(*j).service && all.at(*j).sender == all.at(i).sender &&
             all.at(*j).outgoing == all.at(i).outgoing;
    };
    // Who has read up to where, where the chat shows it: each other person on
    // the message their receipt points at -- or, pointing at what is not shown
    // or not here, the nearest shown before it, by its time.
    std::map<std::string, std::vector<std::string>> readers;
    if (told.receipts) {
      std::map<std::string, std::size_t> place;
      for (std::size_t i = 0; i < all.size(); ++i)
        place.emplace(all.at(i).id, i);
      for (const auto& [user, event] : chat->read_by) {
        if (user == chat->id.account.address)
          continue;
        std::optional<std::size_t> at;
        if (const auto found = place.find(event); found != place.end())
          at = found->second;
        else if (const auto when = chat->receipt_times.find(user); when != chat->receipt_times.end())
          for (std::size_t j = all.size(); j-- > 0;)
            if (all.at(j).at <= when->second) {
              at = j;
              break;
            }
        while (at && !shows(all.at(*at)))
          at = *at == 0 ? std::nullopt : std::optional<std::size_t>(*at - 1);
        if (at)
          readers[all.at(*at).id].push_back(user);
      }
    }
    // Where the newest shown was: those after it have just come.
    std::optional<std::size_t> newest_at;
    if (newest)
      if (const auto at = index(now, told, *newest))
        newest_at = at;
    return std::views::iota(from, to) | std::views::transform([&](std::size_t i) {
             const message& said = all.at(i);
             bubble_facts facts{.said = said,
                                .first = !same(i, neighbour(i, false)),
                                .last = !same(i, neighbour(i, true)),
                                .events_shown = shows(said),
                                .previews_shown = told.previews,
                                .unread_start = told.unread_from && said.id == *told.unread_from,
                                .frost = told.frost};
             if (said.replies_to) {
               const message* quoted = held_message(*chat, *said.replies_to);
               facts.quote_said = quoted ? std::optional(quoted->body) : std::nullopt;
               facts.quote_known = quoted != nullptr;
             }
             if (const auto link = first_link_of(said); link && program)
               facts.preview_known = program->previews.contains(*link);
             if (const auto found = readers.find(said.id); found != readers.end())
               facts.readers = found->second;
             if (program)
               facts.rooms_come = rooms_of(*chat, said, program).come(*program);
             // Come in moving: newer than the newest it showed, shown -- not an
             // event the room hides -- and not one's own coming back under the
             // server's id.
             const bool acknowledged = said.outgoing && spl::visit(spl::overloaded{[](const delivery::sent&) { return true; },
                                                                                  [](const auto&) { return false; }},
                                                                   said.delivery);
             const bool arrives = newest_at && i > *newest_at && shows(said) && !acknowledged;
             return bubble_item{said.id, std::move(facts), chat, told.needs, arrives};
           }) |
           std::ranges::to<std::vector>();
  }
};
// A row of the window made from its item.
struct make_bubble_row {
  auto operator()(const bubble_item& item) const {
    const auto& [id, facts, in, needs, arrives] = item;
    return message_row(*needs, *in, facts, needs->now, arrives);
  }
};
// Where in a row the stretch of its text a quote of it pointed at is: what a
// jump with a quote aims at.
[[nodiscard]] inline auto& row_text_of(message_row_t& row) {
  auto& swiped = std::get<0>(row.fParts);
  auto& inner = std::get<0>(swiped.fParts);
  auto& body = std::get<1>(inner.fParts);
  return part_of<bubble_part::text>(body);
}
struct locate_in_row {
  std::optional<float> operator()(const message_row_t& row, const std::string& fragment) const {
    auto& text = row_text_of(const_cast<message_row_t&>(row));
    const auto at = std::string_view(text.text()).find(fragment);
    if (fragment.empty() || at == std::string_view::npos)
      return std::nullopt;
    return text.bounds().fTop - row.bounds().fTop + text.lineTopOf(at);
  }
};
// The messages, as a window of the list the program says: a chat's, or a
// thread's (Tag tells their answers apart). Want: the part of the shown model
// it is told by.
template <class Want, class Tag>
auto timeline_list() {
  namespace c = skiff::compose;
  auto list = c::WindowFrom<timeline_rows, chats_model, std::string, bubble_facts, message_row_t, make_bubble_row, locate_in_row, Tag>(
      make_bubble_row{});
  list.setCopiesOnScroll(false);
  std::get<0>(list.fChildren).apply({.fillX = true, .autoSize = scene::axes::kY,
                                      .padding = {8.0f, kListSide, 8.0f, kListSide}});
  return c::bound<Want>(std::move(list));
}

// Selection owns the gesture before any link, photo, or text child sees
// it. A tap toggles one message; a drag commits the displayed message range.
struct message_selection_drag {
  std::string first;
  float x, y;
  bool selected;
  bool moved = false;
  float last_x = 0.0f, last_y = 0.0f;
};
inline auto selection_row_at(auto& area, float x, float y) {
  return std::ranges::find_if(area.bubbles(), [&](const auto& row) {
    return !row.message_id.empty() && !row.said.redacted && area.parts.timeline.toView(row.bounds()).contains(x, y);
  });
}
inline bool selection_down(auto& area, const scene::pointer::down& press, scene::PointerReply& reply) {
  if (area.selected_ids.empty() || press.button > 1)
    return false;
  auto& rows = area.bubbles();
  const auto row = selection_row_at(area, press.x, press.y);
  if (row == rows.end())
    return false;
  area.selecting = message_selection_drag{row->message_id, press.x, press.y, !area.selected_ids.contains(row->message_id)};
  reply.capturePointer();
  reply.handle();
  return true;
}
inline bool selection_move(auto& area, const scene::pointer::move& at, scene::PointerReply& reply) {
  if (!area.selecting)
    return false;
  auto& drag = *area.selecting;
  drag.moved = drag.moved || std::hypot(at.x - drag.x, at.y - drag.y) > 6.0f;
  drag.last_x = at.x;
  drag.last_y = at.y;
  if constexpr (requires { area.set_selected(area.selected_ids); })
    area.set_selected(area.selected_ids);
  reply.handle();
  return true;
}
inline std::optional<request::selection_span> selection_up(auto& area, const scene::pointer::up& at, scene::PointerReply& reply) {
  const auto drag = std::exchange(area.selecting, std::nullopt);
  if (!drag)
    return std::nullopt;
  reply.releasePointer();
  reply.handle();
  auto& rows = area.bubbles();
  const auto first = std::ranges::find(rows, drag->first, [](const auto& row) { return row.message_id; });
  const auto last = selection_row_at(area, at.x, at.y);
  if (first == rows.end() || last == rows.end())
    return std::nullopt;
  if (!drag->moved && first != last)
    return std::nullopt;
  const auto [begin, end] = std::minmax(first, last);
  return request::selection_span{
      std::ranges::to<std::vector>(std::views::transform(
          std::views::filter(std::ranges::subrange(begin, end + 1), [](const auto& row) { return !row.said.redacted && !row.message_id.empty(); }),
          [](const auto& row) { return row.message_id; })), drag->selected};
}
inline void selection_cancel(auto& area, scene::PointerReply& reply) {
  if (std::exchange(area.selecting, std::nullopt))
    reply.releasePointer();
}

struct timeline_area : skiff::compose::Specced {
  // What a press on the messages asks for: a message selected or not, one
  // replied to by a swipe, the menu of one, or what a press in a bubble asks.
  using Answer = std::variant<::skiff::scene::Taken, ::mux::ui::request::toggle_selected, ::mux::ui::request::selection_span, ::mux::ui::request::reply_to, menu_facts, bubble_press>;
  // The loader's cross: the message jumped to no longer looked for.
  struct stop_jump {
    using Answer = ::mux::ui::request::stop_jump;
    ::mux::ui::request::stop_jump operator()() { return ::mux::ui::request::stop_jump{}; }
  };
  struct parts_t {
    // Behind the messages: the theme's gradient, Telegram's pattern over it.
    wallpaper_t wall;
    nodes::ScrollContainer<nodes::Flow<std::vector<legacy::message_bubble>>> timeline{
        nodes::Flow<std::vector<legacy::message_bubble>>({.spacingY = 0.0f, .wrap = false}, {})};
    jump_button_t jump;
    back_button_t back;
    mark_button_t mentions;
    mark_button_t reactions;
    // While a message jumped to is being fetched: turning in the middle,
    // its cross stopping the search.
    widgets::RadialLoader<stop_jump> loading;
  } parts;
  // What it was handed down, for the bubbles it makes.
  ui_needs needs_;
  timeline_area(const ui_needs &n)
      : Specced({}),
        parts{.jump = skiff::compose::visible(false,
                                              jump_button(*n.colours)),
              .back = back_button(*n.colours),
              .mentions =
                  mark_button(*n.colours, mark_kind::mention{}, "@"),
              .reactions = mark_button(
                  *n.colours, mark_kind::reaction{}, "\u2665"),
              .loading = skiff::compose::visible(
                  false, skiff::compose::styled(
                             {.place = scene::anchor::kCentre},
                             widgets::RadialLoader<stop_jump>(44.0f, {})))},
        needs_(n) {
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
         .padding = {8.0f, legacy::message_bubble::kListSide, 8.0f, legacy::message_bubble::kListSide}});
  }
  // The chat's background: the theme's gradient and Telegram's pattern, a
  // plain colour (what is behind showing), or a picture.
  // Not here where it is behind the whole window: the window's shows
  // through. Else at the window's opacity, as the panels are.
  void show_wallpaper(const config::wallpaper_t& chosen) {
    parts.wall.setVisible(!needs_.looks->window.behind);
    parts.wall.setOpacity(static_cast<float>(needs_.looks->window.opacity) / 100.0f);
    show_wallpaper_on(parts.wall, chosen, *needs_.colours, *needs_.looks);
  }
  // The bubbles in the list, as they are made.
  [[nodiscard]] std::vector<legacy::message_bubble>& bubbles() {
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
            [&](std::size_t i) { return all[i].id; }, [](const legacy::message_bubble& row) { return row.message_id; },
            [&](std::size_t i) {
              legacy::message_bubble made(spl::remapped<typename legacy::message_bubble::needs>(needs_), one, all[i], first_of_run(i), last_of_run(i), &now, shows(all[i]),
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
            [&](const legacy::message_bubble& row, std::size_t i) {
              const bool quote_known = !all[i].replies_to || one.quoted.contains(*all[i].replies_to) ||
                                       std::ranges::find(all, *all[i].replies_to, &message::id) != all.end();
              const auto link = first_link_of(all[i]);
              const bool preview_known = link && now.previews.contains(*link);
              return row.frost_shown == needs_.looks->window.frost &&
                     row.said == all[i] && row.quote_said == quote_body(i) && row.first == first_of_run(i) &&
                     row.last == last_of_run(i) &&
                     row.quote_known == quote_known && row.events_shown == shows(all[i]) && row.unread_start == (how.unread_from && all[i].id == *how.unread_from) &&
                     row.preview_known == preview_known && row.readers_shown == readers_of(i) &&
                     row.previews_shown == how.previews && !row.rooms_came(now);
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
  legacy::message_bubble* swiped() {
    if (!swiping)
      return nullptr;
    auto& entries = this->bubbles();
    const auto it = std::ranges::find(entries, *swiping, &legacy::message_bubble::message_id);
    return it == entries.end() ? nullptr : &*it;
  }
  void swipe_down(const scene::pointer::down& press) {
    swipe_armed = press.button <= 1;
    swipe_x = press.x;
    swipe_y = press.y;
    swipe_pressed = std::chrono::steady_clock::now();
  }
  void swipe_move(const scene::pointer::move& at, scene::PointerReply& reply) {
    if (legacy::message_bubble* one = this->swiped()) {
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
    for (legacy::message_bubble& one : this->bubbles())
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
  std::optional<Answer> swipe_up(scene::PointerReply& reply) {
    swipe_armed = false;
    legacy::message_bubble* one = this->swiped();
    if (!one)
      return std::nullopt;
    const bool replied = one->swipe.value() <= -legacy::message_bubble::kSwipeToReply;
    std::optional<Answer> asked;
    if (replied)
      asked = ::mux::ui::request::reply_to{one->message_id, one->plain};
    one->swipe.setTarget(0.0f);
    scene::work::mark(one->fState.fId);  // ticked back: nothing else asks for its frames
    swiping.reset();
    reply.releasePointer();
    reply.handle();
    return asked;
  }
  void swipe_cancel(scene::PointerReply& reply) {
    swipe_armed = false;
    if (legacy::message_bubble* one = this->swiped()) {
      one->swipe.setTarget(0.0f);
      scene::work::mark(one->fState.fId);  // ticked back: nothing else asks for its frames
      swiping.reset();
      reply.releasePointer();
    }
  }

  // The swipe is followed on the way down (capture) and at the list itself
  // (target): the same for both, one overload each.
  std::optional<message_selection_drag> selecting;
  void onPointer(scene::phase::capture, const scene::pointer::down& press, scene::PointerReply& reply) {
    if (!selection_down(*this, press, reply)) swipe_down(press);
  }
  void onPointer(scene::phase::target, const scene::pointer::down& press, scene::PointerReply& reply) {
    if (!selection_down(*this, press, reply)) swipe_down(press);
  }
  void onPointer(scene::phase::capture, const scene::pointer::move& at, scene::PointerReply& reply) {
    if (!selection_move(*this, at, reply)) swipe_move(at, reply);
  }
  void onPointer(scene::phase::target, const scene::pointer::move& at, scene::PointerReply& reply) {
    if (!selection_move(*this, at, reply)) swipe_move(at, reply);
  }
  std::optional<Answer> onPointer(scene::phase::capture, const scene::pointer::up& at, scene::PointerReply& reply) {
    if (selecting) {
      auto chosen = selection_up(*this, at, reply);
      return chosen ? std::optional<Answer>{*chosen} : std::nullopt;
    }
    return swipe_up(reply);
  }
  std::optional<Answer> onPointer(scene::phase::target, const scene::pointer::up& at, scene::PointerReply& reply) {
    if (selecting) {
      auto chosen = selection_up(*this, at, reply);
      return chosen ? std::optional<Answer>{*chosen} : std::nullopt;
    }
    return swipe_up(reply);
  }
  void onPointer(scene::phase::capture, const scene::pointer::cancel&, scene::PointerReply& reply) {
    selection_cancel(*this, reply);
    swipe_cancel(reply);
  }
  void onPointer(scene::phase::target, const scene::pointer::cancel&, scene::PointerReply& reply) {
    selection_cancel(*this, reply);
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
  // Messages selected: while there are, a press on a message selects it or
  // lets it go, rather than doing what a press there does.
  std::set<std::string> selected_ids;
  void set_selected(const std::set<std::string>& ids) {
    selected_ids = ids;
    auto preview = selected_ids;
    if (selecting && selecting->moved) {
      auto& rows = this->bubbles();
      const auto first = std::ranges::find(rows, selecting->first, &legacy::message_bubble::message_id);
      const auto last = selection_row_at(*this, selecting->last_x, selecting->last_y);
      if (first != rows.end() && last != rows.end()) {
        const auto [begin, end] = std::minmax(first, last);
        std::ranges::for_each(std::ranges::subrange(begin, end + 1), [&](const auto& row) {
          if (row.said.redacted || row.message_id.empty())
            return;
          if (!selecting->selected)
            preview.erase(row.message_id);
          else if (preview.size() < 100)
            preview.insert(row.message_id);
        });
      }
    }
    std::ranges::for_each(this->bubbles(), [&](auto& one) { one.select(preview.contains(one.message_id)); });
  }
  std::optional<Answer> onClick(float x, float y) {
    // In the space the rows are laid out in: the list draws them scrolled.
    const struct {
      float x, y;
    } press{x, y - parts.timeline.contentsShift()};
    const auto& bubbles = this->bubbles();
    // Selecting: the message pressed selected, or not.
    if (!selected_ids.empty()) {
      const auto pressed = std::ranges::find_if(bubbles, [&](const legacy::message_bubble& one) {
        return one.bounds().contains(press.x, press.y) && !one.message_id.empty();
      });
      if (pressed == std::ranges::end(bubbles))
        return std::nullopt;
      return ::mux::ui::request::toggle_selected{pressed->message_id};
    }
    // Else what the press asks for in the bubble it is in.
    for (const legacy::message_bubble& one : bubbles)
      if (auto asked = press_in_bubble(one, press.x, press.y, seen_chat_of()))
        return Answer{*asked};
    return std::nullopt;
  }
  std::optional<Answer> onPointer(scene::phase::bubble, const scene::pointer::down& press, scene::PointerReply& reply) {
    if (press.button != 3)
      return std::nullopt;  // the main button's presses come as clicks, above
    // Whichever message's row the press is in -- its text, its bubble or the
    // room beside it. What Copy takes is what is selected in it, if anything
    // is, and all of it if not.
    for (const legacy::message_bubble& one : this->bubbles())
      if (parts.timeline.toView(one.bounds()).contains(press.x, press.y)) {
        menu_facts facts = facts_of_bubble(one, seen_chat_of(), press.x, press.y);
        facts.seen = this->seen_by(one.message_id, one.sender);
        reply.handle();
        return Answer{std::move(facts)};
      }
    return std::nullopt;
  }
};

}  // namespace mux::ui
