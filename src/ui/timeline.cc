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
  // The chat's looks, by their number: the rows made again as they change.
  std::uint64_t looks = 0;
  // What the program has fetched -- link previews, rooms found to be there:
  // the window read again as more come, and the rows whose came made again.
  std::size_t fetched = 0;
  friend bool operator==(const timeline_told&, const timeline_told&) = default;
};
// What the chat's list is told, and the thread's: the program's, in the
// shown model, each its own part.
struct chat_told : timeline_told {};
struct thread_told : timeline_told {};
// What the corner over the chat's list shows besides the way down: a way
// back to where a jump in this chat came from, one to the chat a jump
// left, and the loader while a jump is on its way.
struct chat_ways {
  bool back_here = false, back_to_chat = false, loading = false;
  // A window of its history away from the newest: the way down wanted.
  bool detached = false;
  // The mentions of the user, and reactions to theirs, not seen yet.
  std::size_t mentions = 0, reactions = 0;
  friend bool operator==(const chat_ways&, const chat_ways&) = default;
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
  // Come while the reader was away from the end, counted on the way down:
  // another's message the chat shows, not one's own nor an event it hides.
  // A chat a window away from its newest counts nothing: what comes after
  // its window is a page of what is older than the newest, not new.
  static bool counts(const chats_model& now, const Told& told, std::size_t i) {
    if (const conversation* chat = chat_of(now, told); chat && chat->detached && !told.thread)
      return false;
    const message& said = seen_of(now, told).at(i);
    return !said.outgoing && (!said.service || told.filter.shows(said.event_kind));
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
                                .frost = told.frost,
                                .looks = told.looks};
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
  // Its parts: held by what takes its presses while rows are selected; the
  // swiped row in its column; its face and body in that.
  auto& column = std::get<0>(row.fParts);
  auto& swiped = std::get<0>(column.fParts);
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
// Rows being selected: a press on one selects it or lets it go, a drag
// across them a span of them (the list's ListSpan).
inline constexpr auto selecting = [](const message_marks& marks) { return !marks.selected.empty(); };
template <class Want, class Tag>
auto timeline_list() {
  namespace c = skiff::compose;
  auto list = c::WindowFrom<timeline_rows, chats_model, std::string, bubble_facts, message_row_t, make_bubble_row, locate_in_row, Tag>(
      make_bubble_row{});
  list.setCopiesOnScroll(false);
  std::get<0>(list.fChildren).apply({.fillX = true, .autoSize = scene::axes::kY,
                                      .padding = {8.0f, kListSide, 8.0f, kListSide}});
  return c::spanned_for<message_marks>(selecting, c::bound<Want>(std::move(list)));
}

// The loader's cross: the message jumped to no longer looked for.
struct stop_jump_act {
  using Answer = ::mux::ui::request::stop_jump;
  ::mux::ui::request::stop_jump operator()() const { return {}; }
};
// The buttons over the list's bottom right corner, stacked: Telegram's
// heart and @ -- reactions to the user's and mentions of them not seen
// yet, pressed the oldest gone to, pressed with the other button all
// listed -- the way back to the chat a jump left, and "\u2193", back to the
// newest (or a way back in this chat first), with how many came meanwhile.
// What shows each is read from what the program says (chat_ways); "\u2193"
// is told by the list (with_way_down: its fourth part).
inline auto list_corner(const palette& colours) {
  namespace c = skiff::compose;
  const scene::Spec plate{.width = 42.0f, .height = 42.0f, .cornerRadius = 21.0f, .background = colours.sidebar,
                          .hoverBackground = colours.chosen, .border = scene::Border{colours.band, 1.0f}};
  const auto mark = [&](mark_kind_t which, std::string glyph, auto count) {
    return c::shown_for<chat_ways>([count](const chat_ways& now) { return count(now) > 0; },
        c::onRightPress(request::list_marks{which},
            c::onClick(request::jump_to_mark{which},
                c::column(c::vbox(0.0f, plate),
                          c::styled({.place = scene::anchor::kCentre}, nodes::Text(std::move(glyph), 18.0f, colours.text, true)),
                          count_badge_for<chat_ways>(colours, count)))));
  };
  return c::column(c::vbox(10.0f, {.place = scene::anchor::kBottomRight, .x = -18.0f, .y = -12.0f, .autoSize = scene::axes::kBoth}),
                   mark(mark_kind::reaction{}, "\u2665", [](const chat_ways& now) { return now.reactions; }),
                   mark(mark_kind::mention{}, "@", [](const chat_ways& now) { return now.mentions; }),
                   c::shown_for<chat_ways>([](const chat_ways& now) { return now.back_to_chat; },
                       c::onClick(request::return_to_chat{},
                                  c::column(c::vbox(0.0f, plate), c::styled({.fill = true}, nodes::Icon(shape_of(icon::back{}), colours.text))))),
                   c::visible(false, c::onClick(request::jump_to_end{},
                                                c::column(c::vbox(0.0f, plate),
                                                          c::styled({.fill = true}, nodes::Icon(shape_of(icon::down{}), colours.text)),
                                                          c::visible(false, count_badge(colours))))));
}
// The chat's messages over its wallpaper -- the theme's gradient and
// Telegram's pattern, a colour, a picture -- with the corner over them, and
// the loader turning in the middle while a jump is on its way. The list is
// told by the program and answers it (timeline_list); the wallpaper is put
// by what shows the chat (show_wallpaper_on, its first part).
inline auto chat_area(const ui_needs& n) {
  namespace c = skiff::compose;
  wallpaper_t wall;
  wall.apply({.place = scene::anchor::kTopLeft, .fill = true});
  auto list = c::projected<chat_ways>([](const chat_ways& now) { return now.back_here || now.detached; },
      c::with_way_down(c::vbox(0.0f, {.fill = true}), timeline_list<chat_told, chat_list_tag>(), list_corner(*n.colours),
                       [](auto& corner, bool wanted, std::size_t unseen) {
                         auto& down = std::get<3>(corner.fParts);
                         down.setVisible(wanted);
                         auto& badge = std::get<1>(down.fParts);
                         show_count(badge, unseen);
                         badge.setVisible(unseen > 0);
                       }));
  auto loading = c::shown_for<chat_ways>([](const chat_ways& now) { return now.loading; },
      c::styled({.place = scene::anchor::kCentre}, widgets::RadialLoader<stop_jump_act>(44.0f, {})));
  return c::column(c::vbox(0.0f, {.fillX = true, .grow = scene::axes::kY}), std::move(wall), std::move(list), std::move(loading));
}
using chat_area_t = decltype(chat_area(std::declval<const ui_needs&>()));

}  // namespace mux::ui
