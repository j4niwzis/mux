// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:conversations_show -- The chats screen's biggest members, defined: the frame's update, the screen and a chat shown, the space bars, the banners, its keys and presses.
export module mux.ui:conversations_show;

import std;
import mux.logic.text;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes.box;
import skiff.nodes.flow;
import skiff.nodes.scroll;
import skiff.nodes.text;
import skiff.widgets.button;
import skiff.widgets.pill;
import skiff.widgets.textarea;
import mux.core;
import mux.protocols;
import mux.config;
import :base;
import :controls;
import :themes;
import :names;
import :chat_list;
import :message;
import :header;
import :info;
import :composer;
import :timeline;
import :conversations_side;
import :conversations_screen;

export namespace mux::ui {

template <class Actions>
void conversations_screen<Actions>::onKey(scene::phase::bubble, const scene::key::down& press, scene::Reply& reply) {
  namespace keys = scene::keys;
  if (press.key == keys::kEscape && this->close_space_menu()) {
    reply.handle();
    return;
  }
  const bool control = press.modifiers.template has<scene::modifier::control>();
  const bool any = control || press.modifiers.template has<scene::modifier::shift>() ||
                   press.modifiers.template has<scene::modifier::alt>();
  // Ctrl+1 to Ctrl+9: the folder in that place, as tdesktop's.
  static constexpr std::array kFolderKeys{keys::k1, keys::k2, keys::k3, keys::k4, keys::k5,
                                          keys::k6, keys::k7, keys::k8, keys::k9};
  if (const auto digit = std::ranges::find(kFolderKeys, press.key); control && digit != kFolderKeys.end()) {
    const auto& tabs = std::get<0>(side.folders.fChildren);
    if (const auto place = static_cast<std::size_t>(digit - kFolderKeys.begin()); place < tabs.size())
      this->choose_folder(tabs[place].which);
    reply.handle();
    return;
  }
  // A forum gone into, no chat of it open: Esc back out to the chats.
  if (press.key == keys::kEscape && !any && !chosen && forum_open) {
    this->close_forum();
    reply.handle();
    return;
  }
  if (!chosen && !pointed)
    return;
  // Only a forum gone to: Alt+Up and Alt+Down go on from it, nothing else.
  if (!chosen && !((press.key == keys::kUp || press.key == keys::kDown) && press.modifiers.template has<scene::modifier::alt>()))
    return;
  if (press.key == keys::kF && control) {
    actions->open_search();
  } else if (press.key == keys::kK && control) {
    actions->ask_link();
  } else if (press.key == keys::kUp && control) {
    actions->reply_step(true);
  } else if (press.key == keys::kDown && control) {
    actions->reply_step(false);
  } else if (press.key == keys::kUp && !any && line.text().empty()) {
    actions->edit_last();
  } else if (press.key == keys::kC && control) {
    auto& bubbles = std::get<0>(std::get<0>(timeline.fChildren).fChildren);
    const auto selected = std::ranges::find_if(bubbles, [](message_bubble<Actions>& one) { return one.parts.body.parts.text.hasSelection(); });
    // Not in a message: what any text shows selected -- View source's.
    if (selected == bubbles.end()) {
      if (!scene::selectedText().empty())
        skiff::scene::setClipboardText(scene::selectedText());
      return;
    }
    skiff::scene::setClipboardText(selected->parts.body.parts.text.selected());
  } else if (press.key == keys::kEscape && !any && chat.parts.selection.visible()) {
    // Messages selected, the focus elsewhere than their bar: let go first.
    actions->selection_cancel();
  } else if (press.key == keys::kEscape && !any && search.visible()) {
    actions->close_search();
  } else if (press.key == keys::kEscape && !any && parts.threads.answering) {
    parts.threads.stop_answering();
  } else if (press.key == keys::kEscape && !any && line.answering()) {
    actions->cancel_compose();
  } else if ((press.key == keys::kTab && control) ||
             ((press.key == keys::kUp || press.key == keys::kDown) && press.modifiers.template has<scene::modifier::alt>())) {
    // To the next chat in the list, or the one before: Ctrl+Tab and
    // Ctrl+Shift+Tab, Alt+Down and Alt+Up.
    const bool back = press.key == keys::kUp || press.modifiers.template has<scene::modifier::shift>();
    // Over every chat listed, not only the rows made.
    const std::vector<conversation_id>& rows = order;
    // From the forum gone to, where one is; else from the chat open.
    const auto at = std::ranges::find(rows, pointed ? *pointed : *chosen);
    if (at == rows.end() || rows.empty())
      return;
    const auto index = static_cast<std::size_t>(at - rows.begin());
    const std::size_t to = back ? (index == 0 ? rows.size() - 1 : index - 1) : (index + 1) % rows.size();
    // A forum: gone to, lit, not opened -- Alt+Right opens it. A chat: opened.
    if (is_forum(rows[to])) {
      pointed = rows[to];
      if (last_model)
        this->show(*last_model, false);
    } else {
      pointed.reset();
      actions->choose(rows[to]);
    }
  } else if (press.key == keys::kPageUp || press.key == keys::kPageDown) {
    // A page of the messages, most of what is in view.
    const float page = timeline.bounds().height() * 0.9f;
    timeline.scrollTo(std::max(0.0f, timeline.current() + (press.key == keys::kPageUp ? -page : page)));
  } else if (press.key == keys::kEnd && control) {
    actions->jump_to_end();
  } else if (press.key == keys::kEscape && !any) {
    // Nothing else to cancel: a step back -- the threads or the info shut,
    // else the chat closed, as tdesktop's Esc closes it.
    this->step_back();
  } else {
    return;
  }
  reply.handle();
}

template <class Actions>
void conversations_screen<Actions>::show_space_bars(const model& now) {
  struct entry {
    config::space_item_t item;
    folder_t shows;
    std::string id;
    std::string name;
  };
  std::vector<entry> all{{config::space_item::home{}, folder::all{}, "", "Home"},
                         {config::space_item::direct{}, folder::direct{}, "", "Direct messages"}};
  const std::string address = current ? current->address : std::string();
  // Spaces in spaces: each under the first space found holding it. Only
  // those in none are items of the bars; the others show under theirs
  // while it -- or one in it, at any depth -- is the one chosen.
  std::map<std::string, std::string> parent_of;
  std::map<std::string, std::vector<const conversation*>> spaces_in;
  const auto* chats = current ? &now.accounts().at(*current).conversations : nullptr;
  if (chats)
    for (const auto& [key, one] : *chats)
      if (one.space)
        for (const std::string& child : one.children)
          if (const auto found = chats->find(child);
              found != chats->end() && found->second.space && !this->shown_as_forum(found->second) && child != one.id.id &&
              !parent_of.contains(child)) {
            parent_of.emplace(child, one.id.id);
            spaces_in[one.id.id].push_back(&found->second);
          }
  std::set<std::string> open;
  if (const std::optional<std::string> chosen_room = space_of(folder))
    for (std::string at = *chosen_room; open.insert(at).second;) {
      const auto up = parent_of.find(at);
      if (up == parent_of.end())
        break;
      at = up->second;
    }
  if (chats)
    for (const auto& [key, one] : *chats)
      // A space shown as a forum is a chat in the list, not an item of a bar.
      if (one.space && !parent_of.contains(one.id.id) && !this->shown_as_forum(one))
        all.push_back({config::space_item::space{one.id.id}, folder::space{one.id.id}, one.id.id, display_name(one)});
  // A bar's icons: its items, and under an open space its own spaces --
  // smaller, a level at a time.
  struct shown_icon {
    const entry* top = nullptr;
    const conversation* sub = nullptr;
    int depth = 0;
  };
  std::vector<entry> subs_made;
  const auto expanded = [&](const std::vector<const entry*>& items) {
    std::vector<shown_icon> out;
    for (const entry* one : items) {
      out.push_back({one, nullptr, 0});
      const std::optional<std::string> room = spl::visit(
          spl::overloaded{[](const config::space_item::space& it) { return std::optional<std::string>(it.room); },
                             [](const auto&) { return std::optional<std::string>(); }},
          one->item);
      if (!room || !open.contains(*room))
        continue;
      std::vector<std::pair<const conversation*, int>> todo;
      const auto push_children = [&](const std::string& of, int depth) {
        if (const auto found = spaces_in.find(of); found != spaces_in.end())
          for (const conversation* sub : std::views::reverse(found->second))
            todo.emplace_back(sub, depth);
      };
      push_children(*room, 1);
      std::set<std::string> seen{*room};
      while (!todo.empty()) {
        const auto [sub, depth] = todo.back();
        todo.pop_back();
        if (!seen.insert(sub->id.id).second)
          continue;
        out.push_back({nullptr, sub, depth});
        if (open.contains(sub->id.id))
          push_children(sub->id.id, depth + 1);
      }
    }
    return out;
  };
  const std::vector<config::space_placed> mine = std::ranges::to<std::vector>(std::views::filter(space_places, [&](const config::space_placed& p) { return p.account == address; }));
  const auto in_bar = [&](const entry& one, const config::space_bar_t& bar) {
    const bool placed = std::ranges::any_of(mine, [&](const auto& p) { return p.item == one.item; });
    if (!placed)
      return bar == config::space_bar_t{config::space_bar::side{}};
    return std::ranges::any_of(mine, [&](const auto& p) { return p.item == one.item && p.bar == bar; });
  };
  const auto bar_of = [&](const config::space_bar_t& bar) {
    std::vector<std::pair<std::size_t, const entry*>> ranked;
    for (std::size_t i = 0; i < all.size(); ++i)
      if (in_bar(all[i], bar)) {
        const auto at = std::ranges::find_if(mine, [&](const auto& p) { return p.item == all[i].item && p.bar == bar; });
        ranked.emplace_back(at == mine.end() ? mine.size() + i : static_cast<std::size_t>(at - mine.begin()), &all[i]);
      }
    std::ranges::sort(ranked, {}, &std::pair<std::size_t, const entry*>::first);
    return std::ranges::to<std::vector>(std::views::values(ranked));
  };
  const std::vector<const entry*> side_items = bar_of(config::space_bar::side{});
  const std::vector<const entry*> top_items = bar_of(config::space_bar::top{});
  // For the settings to list them.
  needs_.shared->space_account = address;
  needs_.shared->space_items = std::ranges::to<std::vector>(std::views::transform(all, [&](const entry& one) {
                        return space_item_shown{one.item, one.name, in_bar(one, config::space_bar::side{}),
                                                in_bar(one, config::space_bar::top{})};
                      }));
  side.account = address;
  // Made again only where they changed.
  std::vector<std::string> made;
  const std::vector<shown_icon> side_shown = expanded(side_items);
  const std::vector<shown_icon> top_shown = expanded(top_items);
  for (const auto& [items, mark] : {std::pair{&side_shown, "s"}, std::pair{&top_shown, "t"}})
    for (const shown_icon& one : *items)
      // Joined, not std::format: clang 23 crashed instantiating these
      // format strings in the UI test's build (TemplateArgument::
      // isPackExpansion, in SubstType of the format_string).
      made.push_back(one.top ? std::string(mark) + '|' + std::string(config::word_of(one.top->item)) + '|' + one.top->name + '|' +
                                   (avatar_images().has(one.top->id) ? "1" : "0")
                             : std::string(mark) + '|' + one.sub->id.id + '|' + display_name(*one.sub) + '|' +
                                   std::to_string(one.depth) + '|' + (avatar_images().has(one.sub->id.id) ? "1" : "0"));
  made.push_back(std::string(spaces_on ? "1" : "0") + (top_bar_on ? "1" : "0") + (big_spaces ? "1" : "0"));
  if (made != shown_bars) {
    shown_bars = made;
    auto& side_icons = std::get<0>(side.side_line.fChildren);
    auto& top_icons = std::get<0>(side.top_line.fChildren);
    side_icons.clear();
    top_icons.clear();
    // A space in a space: smaller for each level down.
    const auto emit = [&](auto& icons, const std::vector<shown_icon>& shown, const config::space_bar_t& bar, float size) {
      for (const shown_icon& one : shown) {
        if (one.top) {
          icons.emplace_back(*needs_.colours, one.top->item, one.top->shows, bar, one.top->id, one.top->name, one.top->shows == folder, size,
                             pick_folder{this});
          continue;
        }
        const folder_t shows = folder::space{one.sub->id.id};
        icons.emplace_back(*needs_.colours, config::space_item::space{one.sub->id.id}, shows, bar, one.sub->id.id, display_name(*one.sub),
                           shows == folder, std::max(20.0f, size - 6.0f * static_cast<float>(one.depth)), pick_folder{this});
      }
    };
    emit(side_icons, side_shown, config::space_bar::side{}, 40.0f);
    emit(top_icons, top_shown, config::space_bar::top{}, big_spaces ? 50.0f : 30.0f);
    side.side_line.invalidateLayout();
    side.top_line.invalidateLayout();
    if (side.landing)
      for (auto* icons : {&side_icons, &top_icons})
        for (auto& one : *icons)
          if (one.item == *side.landing)
            one.fState.setAlpha(0.0f);
    // A space come or gone: the bars painted again whole.
    side.side_bar.markDamaged();
    side.top_bar.markDamaged();
  }
  // Which is chosen: its ring, on the icons as they are.
  for (auto* icons : {&std::get<0>(side.side_line.fChildren), &std::get<0>(side.top_line.fChildren)})
    for (auto& one : *icons)
      one.set_chosen(one.which == folder);
  // The top bar there unless turned off; the side one where it holds any.
  side.top_bar.setVisible(spaces_on && top_bar_on);
  if (!side.drag || !side.drag->moving)
    side.side_bar.setVisible(spaces_on && !side_items.empty());
}

template <class Actions>
void conversations_screen<Actions>::onPointer(scene::phase::capture, const scene::pointer::down& press, scene::PointerReply& reply) {
  if (side.menu_up() && !side.menu_has(press.x, press.y))
    side.close_menu();
  // The spaces made big: any press off them makes them as they were.
  if (big_spaces && press.button <= 1 && !side.top_bar.fState.fDrawnBounds.contains(press.x, press.y)) {
    big_spaces = false;
    side.set_big_spaces(false);
    if (last_model)
      this->show_space_bars(*last_model);
  }
  // Single: a swipe across the chat begun here -- or across the chats,
  // none open, which pulls the drawer out, as Telegram's apps do.
  swipe_from.reset();
  swipe_row.reset();
  if (single && !chosen && press.button <= 1 && side.visible() && side.bounds().contains(press.x, press.y)) {
    swipe_from = skia::SkPoint{press.x, press.y};
    for (const conversation_row<Actions>& row : std::get<0>(std::get<0>(list.fChildren).fChildren))
      if (list.toView(row.bounds()).contains(press.x, press.y))
        swipe_row = row.id;
  }
  if (single && chosen && press.button <= 1 &&
      ((chat.visible() && chat.bounds().contains(press.x, press.y)) ||
       (info.visible() && info.bounds().contains(press.x, press.y)) ||
       (parts.threads.visible() && parts.threads.bounds().contains(press.x, press.y))))
    swipe_from = skia::SkPoint{press.x, press.y};
  // Single: a long press -- a right press, as the host makes one of it --
  // on the spaces along the top, or near them: they grow for a finger, or
  // go back to as they were. Not their menu.
  if (single && press.button == 3 && side.top_bar.visible() &&
      (side.parts.head.bounds().makeOutset(0.0f, 16.0f).contains(press.x, press.y) ||
       side.top_bar.fState.fDrawnBounds.makeOutset(0.0f, 16.0f).contains(press.x, press.y))) {
    big_spaces = !big_spaces;
    side.set_big_spaces(big_spaces);
    if (last_model)
      this->show_space_bars(*last_model);
    reply.handle();
  }
}

template <class Actions>
void conversations_screen<Actions>::onPointer(scene::phase::capture, const scene::pointer::up& lift, scene::PointerReply& reply) {
  const std::optional<skia::SkPoint> from = std::exchange(swipe_from, std::nullopt);
  if (!from || !single)
    return;
  const float dx = lift.x - from->fX;
  const float dy = lift.y - from->fY;
  if (dx > 90.0f && std::abs(dy) < dx * 0.5f) {
    if (chosen)
      this->step_back();
    else
      actions->open_drawer();
    reply.handle();
  } else if (const auto row = std::exchange(swipe_row, std::nullopt); row && !chosen && -dx > 90.0f && std::abs(dy) < -dx * 0.5f) {
    actions->toggle_mute_of(*row);
    reply.handle();
  }
}

template <class Actions>
void conversations_screen<Actions>::update(double now_ms) {
  if (slide_wait > 0 && --slide_wait == 0)
    list_in.setTarget(1.0f);
  if (list_in.step(now_ms))
    this->place_list();
  this->follow_docked();
  if (pane_in.step(now_ms)) {
    this->place_pane();
    this->markDamaged();
  }
  // The older asked long ago and not come: asked again.
  if (this->history_pending() && now_ms - history_asked_ms > kHistoryPatienceMs)
    history_asked.reset();
  // The info's members near their end, with more: the next few made; or a
  // member opened past those made, theirs worked out.
  if (last_model && chosen && info.visible() && (info.wants_show || info.wants_more()))
    if (const conversation* one = last_model->find(*chosen)) {
      if (!info.wants_show)
        info.members_made += info.kMembersStep;
      info.wants_show = false;
      info.show(*one, *last_model, muted.contains(one->id));
    }
  // The list scrolled past the rows made: those for where it is now made,
  // those far from it let go.
  if (last_model && list.visible() && this->chats_window() != std::pair{chats_from, chats_made})
    this->show(*last_model, false);
  // The panels' opacity on its way to the chat's.
  if (auto& ease = needs_.paint->ease; ease.t.step(now_ms)) {
    needs_.paint->panel.opacity = ease.from + (ease.to - ease.from) * ease.t.value();
    this->markDamaged();
  }
  // A room a bubble names has come -- its picture, or word that it is
  // there: the bubbles made again.
  if (last_model &&
      (std::ranges::any_of(rooms_waiting, [](const std::string& key) { return avatar_images().has(key); }) ||
       std::ranges::any_of(rooms_unfound, [&](const std::string& key) { return last_model->rooms_found.contains(key); })))
    this->show_conversation(*last_model);
  // The pin the bar shows, as the view moves: the one above it. Not while
  // a jump goes on -- where it lands decides.
  // Worked out again only where the view, the messages or the pins moved:
  // it looks each pin up in the whole timeline.
  if (!jumping_to && !aiming && chosen && last_model)
    if (const conversation* one = last_model->find(*chosen); one && !one->pinned.empty()) {
      const pin_inputs now{*chosen, timeline.current(), one->timeline.size(), one->pinned.size(),
                           one->timeline.empty() ? std::string() : one->timeline.front().id};
      if (now != pin_worked_out) {
        pin_worked_out = now;
        if (const std::size_t want = this->pin_above(*one); want != std::min(pinned_step, one->pinned.size() - 1)) {
          pinned_step = want;
          this->show_pinned(one);
        }
      }
    }
  // A message jumped to: made into a bubble where it is loaded, paged back
  // to where it is not -- page after page, as long as there is history --
  // and once it is laid out, brought into view and flashed.
  if (jumping_to && chosen && last_model) {
    auto& entries = std::get<0>(std::get<0>(timeline.fChildren).fChildren);
    auto it = std::ranges::find(entries, *jumping_to, &message_bubble<Actions>::message_id);
    const conversation* one = last_model->find(*chosen);
    // Begun, or more of the chat come: the jump got somewhere.
    if (jump_since_ms < 0.0 || (one && one->timeline.size() != jump_held)) {
      jump_since_ms = now_ms;
      jump_held = one ? one->timeline.size() : 0;
    }
    // A reply to something done in the room -- a join, a rename -- whose
    // line is hidden, as the chat's settings say: landed on the nearest
    // shown after it, else before it, as its place.
    if (it != entries.end() && !it->visible()) {
      auto shown = std::find_if(it, entries.end(), [](const message_bubble<Actions>& row) { return row.visible(); });
      if (shown == entries.end()) {
        const auto back = std::find_if(std::make_reverse_iterator(it), entries.rend(),
                                       [](const message_bubble<Actions>& row) { return row.visible(); });
        shown = back == entries.rend() ? entries.end() : std::prev(back.base());
      }
      if (shown != entries.end()) {
        jumping_to = shown->message_id;
        it = shown;
      }
    }
    if (trace_jumps() && jump_tries % 30 == 0)
      std::cerr << "[jump] " << *jumping_to << (it == entries.end() ? " not made" : it->bounds().isEmpty() ? " made, not laid out" : " laid out")
                << (one && std::ranges::contains(one->timeline, *jumping_to, &message::id) ? ", held" : ", not held") << '\n';
    if (it != entries.end() && !it->bounds().isEmpty()) {
      aiming = std::exchange(jumping_to, std::nullopt);
      aim_quiet = jump_quiet;
      aim_frames = 0;
      aimed_at = -1.0f;
    } else if (one == nullptr) {
      jumping_to.reset();
    } else if (const auto found = std::ranges::find(one->timeline, *jumping_to, &message::id);
               found != one->timeline.end()) {
      // Made around it, where it is not made already.
      const auto at = static_cast<std::size_t>(found - one->timeline.begin());
      const auto [from, to] = this->made_indices(one->timeline);
      if (at < from || at >= to) {
        this->set_made(one->timeline, at > 40 ? at - 40 : 0, at + 40);
        this->show_conversation(*last_model);
      } else if (it != entries.end() && it->visible() && !timeline.moving()) {
        // Made, but not laid out: more than a screen from the view, where
        // the list lays nothing out. The view stepped a screen toward it,
        // until it is laid out and aimed at.
        const auto laid = std::ranges::find_if(entries, [](const message_bubble<Actions>& row) { return !row.bounds().isEmpty(); });
        if (laid != entries.end()) {
          const bool above = it < laid;
          const float page = timeline.bounds().height();
          timeline.setCurrent(std::max(0.0f, timeline.current() + (above ? -page : page)));
        }
      }
    } else if (const message* held = held_message(*one, *jumping_to); held && held->thread) {
      // Come with the context, but as an answer in a thread: kept with its
      // thread, never in the timeline -- paged back for, the whole chat was
      // fetched to the beginning and it was not found. Its thread opened.
      actions->open_thread(*held->thread);
      this->stop_jump();
    } else if (proto::offers(protocol_state_of(*needs_.shared, chosen->account), proto::feature::history_context{}) && !jump_paging) {
      // Not here: a window of the history around it, from the server --
      // not all of it from here to there. Where that does not bring it,
      // paged back to, as far as the chat's limit.
      if (context_asked != jumping_to) {
        context_asked = jumping_to;
        jump_since_ms = now_ms;
        actions->load_context(*chosen, *jumping_to);
      } else if (now_ms - jump_since_ms > kContextPatienceMs) {
        jump_paging = true;
        jump_tries = 0;
      }
    } else if (history_from && history_asked != history_from) {
      // Paged back, as far as the chat's limit says: past it, given up.
      if (!jump_base)
        jump_base = one->timeline.size();
      const auto limit = jump_limits.find(*chosen);
      const std::int64_t most = limit == jump_limits.end() ? 5000 : limit->second;
      if (most > 0 && static_cast<std::int64_t>(one->timeline.size() - std::min(*jump_base, one->timeline.size())) >= most) {
        this->stop_jump();
      } else {
        history_asked = history_from;
        history_asked_ms = now_ms;
        jump_since_ms = now_ms;
        actions->load_older(*chosen, *history_from);
      }
    } else if (!history_from && now_ms - jump_since_ms > 2000.0) {
      jumping_to.reset();  // the beginning, and it was not there
    }
  }
  // A message jumped to, aimed at until it stays: in the middle of the
  // view, as tdesktop brings one (its top, where it is taller than the
  // view) -- or, opened at what was read, near the top with the unread
  // below. Aimed again each frame while what is above it moves it; flashed
  // once the list is still, where the flash is seen.
  if (aiming) {
    auto& entries = std::get<0>(std::get<0>(timeline.fChildren).fChildren);
    const auto it = std::ranges::find(entries, *aiming, &message_bubble<Actions>::message_id);
    if (it == entries.end() || it->bounds().isEmpty() || ++aim_frames > 180) {
      aiming.reset();
    } else {
      const skia::SkRect view = timeline.bounds();
      const skia::SkRect box = timeline.toView(it->bounds());
      const float above = aim_quiet ? (unread_from && *aiming == *unread_from ? 0.0f : 60.0f)
                          : box.height() < view.height() ? (view.height() - box.height()) * 0.5f
                                                         : 0.0f;
      float to = std::max(0.0f, timeline.current() + (box.fTop - view.fTop) - above);
      // Where a reply quoted a part of it: that part marked, and its line
      // brought to the view's upper middle -- the right place of a message
      // taller than the view.
      if (aimed_at < 0.0f && jump_fragment) {
        if (const auto at = it->mark(*jump_fragment)) {
          const auto& text = it->parts.body.parts.text;
          const float line = timeline.toView(text.bounds()).fTop + text.lineTopOf(*at);
          to = std::max(0.0f, timeline.current() + (line - view.fTop) - view.height() * 0.4f);
        }
        jump_fragment.reset();
      }
      // The first aim: flashed at once, where it is -- not once all above
      // it has settled, which can be never, and the flash was lost. Far
      // away -- a window just loaded around it -- put there at once, not
      // glided to through what was loaded; near, glided to.
      if (aimed_at < 0.0f) {
        if (std::abs(to - timeline.current()) > view.height() * 2.0f)
          timeline.setCurrent(to);
        else
          timeline.scrollTo(to);
        aimed_at = to;
        if (!aim_quiet) {
          it->flash.jump(1.0f);
          it->flash.setTarget(0.0f);
          it->markDamaged();
        }
      } else if (std::abs(to - aimed_at) > 1.0f) {
        timeline.setCurrent(to);
        aimed_at = to;
      } else if (!timeline.moving()) {
        aiming.reset();
      }
    }
  }
  // A jump on its way for more than a few frames -- fetched, or paged back
  // to -- shows the loader turning in the middle of the list.
  jump_age = jumping_to ? jump_age + 1 : 0;
  // No time limit: a jump goes on as long as there is history to page
  // back through, as far as the chat's own limit (none, where it says 0) --
  // or until it is stopped by hand, at the loader.
  // And while the older, asked at the top, are on their way.
  if (const bool loading = jump_age > 6 || (this->history_pending() && timeline.current() <= 300.0f);
      loading != chat.area.parts.loading.visible())
    chat.area.parts.loading.setVisible(loading);
  this->find_mentions();
  this->find_emoji();
  // What is in the composer: typing while there is text in it.
  if (const bool has_text = !line.text().empty(); has_text != was_typing || (has_text && line.text() != typed_last)) {
    was_typing = has_text;
    typed_last = line.text();
    actions->typing(has_text);
  }
  if (side.search.field.text() != searched && last_model) {
    searched = side.search.field.text();
    this->show(*last_model, false);
  }
  // Each message to come back to, as tdesktop lets one go
  // (checkReplyReturns, at every scroll but its own scroll to a message):
  // once the view has come down to it -- its top above the view's middle,
  // or the view at the very end -- or past it, where it is not made and
  // older than the newest made. The one under it next, if not passed.
  if (chosen && !jumping_to && !aiming)
    if (const auto found = returns.find(*chosen); found != returns.end()) {
      auto& stack = found->second;
      const auto& entries = std::get<0>(std::get<0>(timeline.fChildren).fChildren);
      const skia::SkRect view = timeline.bounds();
      const conversation* one = last_model ? last_model->find(*chosen) : nullptr;
      const auto newest = std::ranges::find_if(entries.rbegin(), entries.rend(),
                                               [](const message_bubble<Actions>& row) { return !row.message_id.empty(); });
      while (!stack.empty()) {
        const auto it = std::ranges::find(entries, stack.back(), &message_bubble<Actions>::message_id);
        bool passed = false;
        if (it != entries.end() && !it->bounds().isEmpty()) {
          passed = timeline.atEnd(0.5f) || timeline.toView(it->bounds()).fTop < view.centerY();
        } else if (one && newest != entries.rend()) {
          const auto at = std::ranges::find(one->timeline, stack.back(), &message::id);
          const auto last = std::ranges::find(one->timeline, newest->message_id, &message::id);
          passed = at != one->timeline.end() && last != one->timeline.end() && at < last;
        }
        if (!passed)
          break;
        stack.pop_back();
      }
    }
  const bool away = this->away();
  if (away != chat.area.parts.jump.visible())
    chat.area.parts.jump.setVisible(away);
  // The @ and the heart, stacked over "↓" where it is up.
  if (const conversation* here = chosen && last_model ? last_model->find(*chosen) : nullptr) {
    int slot = chat.area.parts.jump.visible() ? 1 : 0;
    // Come back to the chat a jump left by hand: that way back is done.
    while (!chat_returns.empty() && chat_returns.back() == *chosen)
      chat_returns.pop_back();
    chat.area.parts.back.show(!chat_returns.empty(), slot);
    if (!chat_returns.empty())
      ++slot;
    chat.area.parts.mentions.show(here->unread_mentions.size(), slot);
    if (!here->unread_mentions.empty())
      ++slot;
    chat.area.parts.reactions.show(here->unread_reactions.size(), slot);
  }
  if (!away && unseen != 0) {
    unseen = 0;
    chat.area.parts.jump.set_unseen(0);
  }
  // Near the top: the stretch made slides up -- the far ones below let
  // go -- and past all that is loaded, the history is paged back. Near
  // the bottom, where it is not at the end, it slides down.
  if (chosen && last_model && !jumping_to && !timeline.glidingToEnd())
    if (const conversation* one = last_model->find(*chosen)) {
      auto [from, to] = this->made_indices(one->timeline);
      // Slid while a screen and a half is still made beyond the view.
      const float ahead = std::max(300.0f, timeline.bounds().height() * 1.5f);
      if (timeline.current() <= ahead && from > 0) {
        from = from > kMadeStep ? from - kMadeStep : 0;
        to = std::min(to, from + kMostMade);
        this->set_made(one->timeline, from, to);
        this->show_conversation(*last_model);
      } else if (timeline.current() <= 4.0f && from == 0 && history_from && history_asked != history_from) {
        history_asked = history_from;
        history_asked_ms = now_ms;
        actions->load_older(*chosen, *history_from);
      } else if (made.to_end && one->detached && one->future_from && newer_asked != one->future_from &&
                 timeline.current() >= timeline.extent() - 300.0f) {
        // At the end of a window: paged forward, toward the newest.
        newer_asked = one->future_from;
        actions->load_newer(*chosen, *one->future_from);
      } else if (!made.to_end && timeline.current() >= timeline.extent() - ahead) {
        to = std::min(one->timeline.size(), to + kMadeStep);
        from = to > kMostMade && to - from > kMostMade ? to - kMostMade : from;
        this->set_made(one->timeline, from, to);
        this->show_conversation(*last_model);
      }
    }
}

template <class Actions>
void conversations_screen<Actions>::show(const model& now, bool with_chat) {
  last_model = &now;
  // Files attached where the chat's account sends them, and its protocol
  // allows it now: no paperclip otherwise, in the chat or its thread.
  if (chosen) {
    const bool files = may_send_files(*needs_.shared, chosen->account);
    line.parts.input.parts.attach.setVisible(files);
    parts.threads.parts.line.parts.input.parts.attach.setVisible(files);
  }
  if (wanted && now.accounts().contains(*wanted)) {
    current = std::exchange(wanted, std::nullopt);
  } else if (!current || !now.accounts().contains(*current)) {
    current = now.accounts().empty() ? std::nullopt : std::optional<account_id>(now.accounts().begin()->first);
  }
  auto& rows = std::get<0>(std::get<0>(list.fChildren).fChildren);
  std::vector<const conversation*> chats;
  // What is searched for, in any case: in a name or an address.
  constexpr auto lower = mux::logic::folded;
  const std::string wanted = lower(side.search.field.text());
  // The folders the account has: its spaces, then its groups.
  std::vector<std::pair<std::string, folder_t>> folders{{"All", folder::all{}}};
  if (current) {
    std::set<std::string> groups;
    for (const auto& [key, one] : now.accounts().at(*current).conversations) {
      // Spaces in their bars, where there are bars; else as tabs.
      if (one.space && !spaces_on)
        folders.emplace_back(display_name(one), folder::space{one.id.id});
      groups.insert(one.groups.begin(), one.groups.end());
    }
    for (const std::string& name : groups)
      folders.emplace_back(name, folder::group{name});
  }
  // The folder chosen kept while it is still there: a tab's, or a bar's --
  // Direct messages, or a space the account has (the bars' are no tabs).
  const bool in_bars = spaces_on && spl::visit(
      spl::overloaded{[](const folder::direct&) { return true; },
                         [&](const folder::space& s) {
                           if (!current)
                             return false;
                           const auto& chats = now.accounts().at(*current).conversations;
                           const auto found = chats.find(s.room);
                           return found != chats.end() && found->second.space;
                         },
                         [](const auto&) { return false; }},
      folder);
  if (!in_bars && std::ranges::find(folders, folder, &std::pair<std::string, folder_t>::second) == folders.end())
    folder = folder::all{};
  // Made again only where they changed: made at every change in the model,
  // new tabs were a full walk and the bar laid out and painted again.
  auto& tabs = std::get<0>(side.folders.fChildren);
  if (folders != shown_folders) {
    tabs.clear();
    for (auto& [name, which] : folders)
      tabs.emplace_back(*needs_.colours, name, which, which == folder, pick_folder{this});
    shown_folders = folders;
  }
  for (auto& tab : tabs)
    tab.set_chosen(tab.which == folder);
  shown_folder = folder;
  side.folders.setVisible(folders.size() > 1);
  this->show_space_bars(now);
  // Whether a chat is in the folder chosen. A space is a folder, not a
  // chat: it is never listed.
  const account* in = current ? &now.accounts().at(*current) : nullptr;
  // The rooms of the space chosen: its own, and those of the spaces in it,
  // down every level -- each space once, however they hold each other.
  std::set<std::string> in_space;
  if (in)
    if (const std::optional<std::string> chosen_room = space_of(folder)) {
      std::set<std::string> seen;
      std::vector<std::string> todo{*chosen_room};
      while (!todo.empty()) {
        const std::string at = std::move(todo.back());
        todo.pop_back();
        if (!seen.insert(at).second)
          continue;
        if (const auto found = in->conversations.find(at); found != in->conversations.end())
          for (const std::string& child : found->second.children) {
            in_space.insert(child);
            todo.push_back(child);
          }
      }
    }
  // Forums: each listed as one chat; their rooms in them, not beside them.
  std::set<std::string> in_forums;
  if (in)
    std::ranges::for_each(std::views::filter(std::views::values(in->conversations), [&](const conversation& one) { return this->shown_as_forum(one); }),
                          [&](const conversation& one) { in_forums.insert(one.children.begin(), one.children.end()); });
  // The forum open: still one; its rooms, the list.
  if (forum_open && (!current || !this->is_forum(conversation_id{*current, *forum_open})))
    forum_open.reset();
  const conversation* forum = forum_open && in && in->conversations.contains(*forum_open) ? &in->conversations.at(*forum_open) : nullptr;
  side.forum_head.setVisible(forum != nullptr);
  if (forum)
    side.forum_head.parts.name.setText(display_name(*forum));
  // What the account's spaces hold: out of Home, where it is chosen so --
  // but direct messages.
  std::set<std::string> in_spaces;
  // Every space's, where Home hides all that spaces hold; else those of
  // the spaces that hide theirs.
  if (in && folder == folder_t{folder::all{}})
    for (const auto& [key, each] : in->conversations)
      if (each.space && (home_hides_spaced || hidden_from_home.contains(each.id)))
        in_spaces.insert(each.children.begin(), each.children.end());
  const auto direct = [](const conversation& one) {
    return spl::visit(spl::overloaded{[](conversation_kind::direct) { return true; }, [](const auto&) { return false; }}, one.kind);
  };
  const auto in_folder = [&](const conversation& one) {
    if (!one.space && in_spaces.contains(one.id.id) && !direct(one))
      return false;
    if (home_hides_spaced && home_hides_direct && folder == folder_t{folder::all{}} && direct(one))
      return false;
    if (forum)
      return !one.space && std::ranges::contains(forum->children, one.id.id);
    // A space is a folder, not a chat -- but a forum is one chat.
    if (one.space && !this->shown_as_forum(one))
      return false;
    if (!one.space && in_forums.contains(one.id.id))
      return false;
    return spl::visit(spl::overloaded{[](const folder::all&) { return true; },
                                 [&](const folder::space&) { return in_space.contains(one.id.id); },
                                 [&](const folder::group& g) { return std::ranges::contains(one.groups, g.name); },
                                 [&](const folder::direct&) {
                                   return spl::visit(spl::overloaded{[](conversation_kind::direct) { return true; },
                                                                           [](const auto&) { return false; }},
                                                        one.kind);
                                 }},
                      folder);
  };
  // Its own chats -- not those moved to another account's list -- and the
  // other accounts' listed in it.
  const auto found_by = [&](const conversation& one) {
    // Upgraded away, its new room here: only the new one listed, as Element
    // -- where the two agree: its tombstone names the new room, and the new
    // room's creation names it as what it continues. One side alone is not
    // believed (as matrix-js-sdk's CVE-2025-59160, the other way round):
    // a room could otherwise hide another from the list.
    if (const auto successor = proto::successor_of(protocol_state_of(*needs_.shared, one.id.account), one); successor && in)
      if (const auto next = in->conversations.find(*successor);
          next != in->conversations.end() &&
          proto::predecessor_of(protocol_state_of(*needs_.shared, one.id.account), next->second) == one.id.id)
        return false;
    return in_folder(one) && (wanted.empty() || lower(display_name(one)).contains(wanted) || lower(one.id.id).contains(wanted));
  };
  if (in)
    for (const auto& [key, one] : in->conversations)
      if (!moved_out.contains(one.id) && found_by(one))
        chats.push_back(&one);
  if (in && current)
    if (const auto extra = listed_in.find(*current); extra != listed_in.end())
      for (const conversation_id& id : extra->second)
        if (const conversation* one = now.find(id); one && found_by(*one))
          chats.push_back(one);
  // The strip of a chat listed here from another account.
  const auto strip_for = [&](const conversation* one) -> std::optional<skia::SkColor> {
    if (!current || one->id.account == *current)
      return std::nullopt;
    const auto found = strips.find(one->id);
    return found == strips.end() ? std::nullopt : std::optional<skia::SkColor>(found->second);
  };
  // Each chat's room events as it shows them: what it hides is not its
  // newest, nor counted.
  const auto events_of = [&](const conversation* one) {
    const auto found = event_filters.find(one->id);
    return found == event_filters.end() ? room_event_filter{} : found->second;
  };
  // A forum's row, as tdesktop's: the newest of its topics, and their
  // unread summed -- not the space's own, which says what was done to it.
  std::map<conversation_id, conversation> forum_shown;
  if (in)
    for (const conversation*& one : chats)
      if (this->shown_as_forum(*one)) {
        conversation made = *one;
        made.timeline.clear();
        made.read_up_to.reset();
        made.detached = false;
        made.unread = 0;
        made.highlights = 0;
        const message* best = nullptr;
        std::string best_in;
        for (const std::string& child : one->children)
          if (const auto found = in->conversations.find(child); found != in->conversations.end() && !found->second.space) {
            const conversation& topic = found->second;
            made.unread += topic.unread_here(events_of(&topic));
            made.highlights += topic.highlights;
            if (const message* last = newest(topic, events_of(&topic)); last && (!best || last->at > best->at)) {
              best = last;
              best_in = topic.id.id;
            }
          }
        if (best) {
          made.timeline.push_back(*best);
          // Said as tdesktop says a forum's newest: in which topic, by whom.
          if (const auto found = in->conversations.find(best_in); found != in->conversations.end()) {
            made.forum_topic = display_name(found->second);
            made.members = found->second.members;
          }
        }
        one = &forum_shown.insert_or_assign(one->id, std::move(made)).first->second;
      }
  // Invites first, as Element lists them; then by their newest.
  std::ranges::sort(chats, std::ranges::greater{}, [&](const conversation* one) {
    const message* last = newest(*one, events_of(one));
    return std::pair{one->invite.has_value(), last ? last->at : std::chrono::sys_time<std::chrono::milliseconds>{}};
  });
  // The rows, as a function of the chats: those whose chat shows the same
  // are kept as they are.
  // The chat open -- or, where Alt+Up or Alt+Down went to a forum, that.
  const auto is_chosen = [&](const conversation* one) {
    return pointed ? *pointed == one->id : chosen && *chosen == one->id;
  };
  const std::vector<conversation_id> listed_before =
      std::ranges::to<std::vector>(std::views::transform(rows, [](const conversation_row<Actions>& row) { return row.id; }));
  chats_listed = chats.size();
  order = std::ranges::to<std::vector>(std::views::transform(chats, [](const conversation* one) { return one->id; }));
  tops.assign(1, 0.0f);
  tops.reserve(chats.size() + 1);
  for (const conversation* one : chats)
    tops.push_back(tops.back() + conversation_row<Actions>::height_of(*one));
  std::tie(chats_from, chats_made) = this->chats_window();
  const auto made = std::views::take(std::views::drop(chats, chats_from), chats_made - chats_from);
  // The rows not made, above and below: as much room as they would take.
  std::get<0>(list.fChildren).apply({.padding = scene::Margin{tops[chats_from], 0.0f, tops.back() - tops[chats_made], 0.0f}});
  {
    const std::set<conversation_id> listed = std::ranges::to<std::set>(std::views::transform(made, [](const conversation* one) { return one->id; }));
    for (conversation_row<Actions>& row : rows)
      if (!listed.contains(row.id)) {
        const conversation_id id = row.id;
        rows_kept.insert_or_assign(id, std::move(row));
      }
    while (rows_kept.size() > kRowsKept)
      rows_kept.erase(rows_kept.begin());
  }
  if (nodes::reconcile(
          rows, made, [](const conversation* one) { return one->id; },
          [](const conversation_row<Actions>& row) { return row.id; },
          [&](const conversation* one) {
            if (const auto kept = rows_kept.find(one->id); kept != rows_kept.end()) {
              const bool same = kept->second.shown == conversation_row<Actions>::view_of(*needs_.shared, *one, is_chosen(one), muted.contains(one->id),
                                                                                         draft_of(one->id), events_of(one), strip_for(one));
              if (same) {
                conversation_row<Actions> back = std::move(kept->second);
                rows_kept.erase(kept);
                return back;
              }
              rows_kept.erase(kept);
            }
            return conversation_row<Actions>(needs_, *one, is_chosen(one), muted.contains(one->id), draft_of(one->id),
                                             events_of(one), strip_for(one));
          },
          [&](const conversation_row<Actions>& row, const conversation* one) {
            return row.shown ==
                   conversation_row<Actions>::view_of(*needs_.shared, *one, is_chosen(one), muted.contains(one->id), draft_of(one->id),
                                                      events_of(one), strip_for(one));
          })) {
    list.invalidateLayout();
    // A chat come or gone -- or moved to another place: the whole list
    // painted again, not only what says it moved.
    if (!std::ranges::equal(listed_before, rows, {}, {}, [](const conversation_row<Actions>& row) { return row.id; }))
      list.markDamaged();
  }
  const bool none = now.accounts().empty();
  // The messages' area, not only its list: hidden, it no longer takes the
  // column's height and pushes what is said instead to the bottom.
  // A chat open: its head, messages and composer. None chosen: the hint.
  const bool open = !none && chosen.has_value();
  for (scene::Node* shown : std::initializer_list<scene::Node*>{&header, &chat.area, &line})
    shown->setVisible(open);
  chat.hint.setVisible(!none && !chosen.has_value());
  // Nothing joined matching what is searched -- two letters or more --
  // the server is asked for rooms and people that do, once for each
  // thing typed.
  const std::string& typed = side.search.field.text();
  const bool elsewhere = !none && !wanted.empty() && chats.empty() && typed.size() >= 2;
  if (elsewhere && typed != asked_elsewhere) {
    asked_elsewhere = typed;
    rooms_elsewhere.clear();
    people_elsewhere.clear();
    rooms_came = people_came = false;
    actions->search_elsewhere(typed);
    this->show_elsewhere();
  } else if (!elsewhere && !asked_elsewhere.empty()) {
    asked_elsewhere.clear();
    rooms_elsewhere.clear();
    people_elsewhere.clear();
    this->show_elsewhere();
  }
  side.elsewhere.setVisible(elsewhere);
  no_chats.setVisible(!none && chats.empty() && !elsewhere);
  chat.empty.setVisible(none);
  this->show_info();
  // Laid out again, repainting only what moves: what changed repaints
  // itself. Invalidated, the whole window was painted at every change in
  // the model -- a hidden event's too.
  fState.relayoutQuietly();
  if (with_chat)
    this->show_conversation(now);
}

template <class Actions>
void conversations_screen<Actions>::show_banners(const conversation* one, const model& now) {
  const auto banners = one ? proto::composer_banners(protocol_state_of(*needs_.shared, one->id.account), *one, now)
                           : std::vector<proto::any_banner>{};
  const std::string said = std::ranges::to<std::string>(std::views::join_with(std::views::transform(banners, &proto::any_banner::text), '\n'));
  // The first's button, where it has one.
  const std::string label = banners.empty() ? std::string() : banners.front().button;
  chat.banner_asks = banners.empty() ? std::nullopt : banners.front().asks;
  auto& button = chat.parts.banner_button;
  if (label.empty() == button.has_value()) {
    if (label.empty())
      button.reset();
    else {
      button.emplace(needs_.colours->widgets, label, banner_press{actions, &chat.banner_asks});
      button->apply({.width = 120.0f, .height = 30.0f, .alignSelf = scene::align::kEnd,
                     .margin = {4.0f, 14.0f, 6.0f, 14.0f}});
    }
    chat.invalidateLayout();
  } else if (button)
    button->setLabel(label);
  // The protocol's own node under the head, made again for the chat.
  chat.parts.their_head.reset();
  if (one)
    spl::visit(
        [&](const auto& now) {
          using head_view_defaults::make_head_view;
          this->place_head_view(make_head_view(now, *one, type_tag<Actions>{}));
        },
        protocol_state_of(*needs_.shared, one->id.account));
  // The protocol's own node over the composer, made again for the chat.
  chat.parts.their_view.reset();
  if (one)
    spl::visit(
        [&](const auto& now) {
          using composer_view_defaults::make_composer_view;
          this->place_composer_view(make_composer_view(now, *one, type_tag<Actions>{}));
        },
        protocol_state_of(*needs_.shared, one->id.account));
  auto& bar = chat.parts.trust_warning;
  if (!banners.empty())
    bar.apply({.background = (tone_colour(*needs_.colours, banners.front().tone) & 0x00FFFFFFu) | (0x22u << 24)});
  if (bar.text() != said)
    bar.setText(said);
  if (bar.visible() != !said.empty()) {
    bar.setVisible(!said.empty());
    chat.invalidateLayout();
  }
}

template <class Actions>
void conversations_screen<Actions>::show_conversation(const model& now) {
  // Whether the reader was at the newest: then the view follows it; and
  // where the view was, for the chat being left.
  const bool was_at_end = timeline.atEnd(40.0f);
  const float left_at = timeline.current();
  auto& entries = std::get<0>(std::get<0>(timeline.fChildren).fChildren);
  const conversation* one = chosen ? now.find(*chosen) : nullptr;
  head_shown = chat_header<Actions>::view_of(*needs_.shared, one, now);
  head_shown.back = single;
  header.show(head_shown, [this](const auto& shown) { return chat_header<Actions>(needs_, shown); });
  this->show_banners(one, now);
  if (pinned_of != chosen) {
    pinned_of = chosen;
    pinned_step = std::numeric_limits<std::size_t>::max();
  }
  this->show_pinned(one);
  history_from = one ? one->history_from : std::nullopt;
  this->show_info();
  if (!one) {
    entries.clear();
    return;
  }
  if (bubbles != needs_.looks->bubbles) {
    needs_.looks->bubbles = bubbles;
    entries.clear();
  }
  // Only while it is open: its members were made at every switch of chat,
  // the panel shut or not. Opened, it is shown then (toggle_info).
  if (info.visible())
    info.show(*one, now, muted.contains(one->id));
  chat.area.show_wallpaper(wallpaper);
  if (parts.threads.visible())
    parts.threads.show(*one, &now);
  // Whether the reader may post here, as the chat's protocol says (Matrix:
  // its power levels); and whether any message of theirs here was not sent.
  {
    const proto::part::chat_rights may = proto::chat_rights(protocol_state_of(*needs_.shared, one->id.account), *one);
    chat.line.set_can_post(may.post);
    chat.line.set_replaced(proto::successor_of(protocol_state_of(*needs_.shared, one->id.account), *one).has_value());
    // Those knocking, for whoever may invite.
    chat.line.show_knocks(actions, one->knocking, may.invite);
    chat.line.show_unsent(std::ranges::any_of(one->timeline, [](const message& said) {
      return said.outgoing &&
             spl::visit(spl::overloaded{[](const delivery::failed&) { return true; }, [](const auto&) { return false; }},
                           said.delivery);
    }));
  }
  chat.area.seen_model = &now;
  chat.area.seen_chat = one->id;
  const auto& all = one->timeline;
  // Where each message is in its sender's run: the first has the name,
  // the last the avatar.
  // A room's event -- a join, an address set -- is a line of its own and
  // ends a run: the message after it has its sender's name again. Where
  // the room's events are hidden, a run goes on past them.
  const auto filter_found = event_filters.find(one->id);
  const room_event_filter filter = filter_found == event_filters.end() ? room_event_filter{} : filter_found->second;
  const auto shows = [&](const message& said) { return !said.service || filter.shows(said.event_kind); };
  // A chat shown anew: its stretch as it was left, or its newest.
  if (shown_chat != chosen)
    parts.threads.open.reset();
  if (shown_chat != chosen) {
    if (shown_chat)
      made_of[*shown_chat] = made;
    const auto kept = made_of.find(*chosen);
    made = kept == made_of.end() ? made_range{} : kept->second;
  }
  const auto [first_made, last_made] = this->made_indices(all);
  this->set_made(all, first_made, last_made);
  // A bubble comes in moving only as its message comes: one newer than the
  // newest this chat showed, shown -- not an event the room hides -- and
  // seen, the reader being at the newest. Anything else made -- a row made
  // again as it changed, history paged in, a chat opened, rows scrolled
  // back into what is made -- is there at once. One's own, once the
  // server has it, is the same message under its new id: not again.
  const bool same_chat = shown_chat == chosen;
  const auto newest_before = std::ranges::find(all, shown_last, &message::id);
  const std::size_t new_from =
      newest_before == all.end() ? all.size() : static_cast<std::size_t>(newest_before - all.begin()) + 1;
  const auto arrives = [&](std::size_t i) {
    const bool acknowledged = all[i].outgoing && spl::visit(spl::overloaded{[](const delivery::sent&) { return true; },
                                                                       [](const auto&) { return false; }},
                                                            all[i].delivery);
    return same_chat && was_at_end && i >= new_from && shows(all[i]) && !acknowledged &&
           appeared.insert(all[i].id).second;
  };
  // The bubbles, as a function of the messages: the timeline's, as a
  // thread's are made.
  chat.area.show_messages(*one, all, first_made, last_made, now,
                          shown_how{.filter = filter,
                                    .receipts = receipts_in.contains(one->id),
                                    .previews = !previews_off.contains(one->id) && !one->encrypted,
                                    .unread_from = unread_from},
                          arrives, rooms_wanted);
  rooms_waiting.clear();
  rooms_unfound.clear();
  for (const message_bubble<Actions>& row : entries) {
    rooms_waiting.insert(row.rooms_waiting.begin(), row.rooms_waiting.end());
    rooms_unfound.insert(row.rooms_unknown.begin(), row.rooms_unknown.end());
  }
  // The newest is at the bottom: the view follows it where the reader was
  // there or the chat is new to the view; otherwise what came after the
  // last one seen is counted on the way down.
  const std::string last = all.empty() ? std::string() : all.back().id;
  if (shown_chat != chosen) {
    // Another chat shown: the ways back from jumps in the one left gone,
    // as tdesktop clears its reply returns (showHistory).
    returns.clear();
    if (shown_chat)
      scrolled[*shown_chat] = was_at_end ? -1.0f : left_at;
    const auto kept = scrolled.find(*chosen);
    // A jump asked for in another chat is let go; one asked with this
    // chat's opening -- a link to a message in it -- decides where the
    // view goes: not the unread, nor where it was left, first.
    if (jumping_to && jump_chat != chosen)
      jumping_to.reset();
    unread_from.reset();
    if (jumping_to) {
      timeline.scrollToEnd(false);
    } else if (const std::optional<std::string> first = first_unread(*one, all)) {
      // Unread in it: opened at its first unread, at the view's top under
      // tdesktop's bar, and read on from there as it is seen -- where the
      // message read up to is not here, first what is around it.
      timeline.scrollToEnd(false);
      unread_from = first_unread_here(*one, all);
      jumping_to = *first;
      jump_chat = chosen;
      jump_quiet = true;
      jump_tries = 0;
      context_asked.reset();
    } else if (kept == scrolled.end() || kept->second < 0.0f)
      timeline.scrollToEnd(false);  // a chat opened starts at its newest
    else
      timeline.setCurrent(kept->second);
    unseen = 0;
  } else if (was_at_end && !jumping_to && !aiming) {
    // At the newest, it follows what comes -- not while a jump goes
    // elsewhere: a refresh each time the model changes put the view back
    // at the end under a jump on its way up, and it never got there.
    // Glided only to a newer message: rows above made again or grown kept
    // at the end where they are, at once -- glided, every message slid by
    // and settled.
    timeline.scrollToEnd(last != shown_last);
    unseen = 0;
  } else if (last != shown_last && !one->detached && !shown_detached) {
    // A jump far back holds the timeline from there: the pages after it
    // loaded as the reader goes down are not new, and 30 of them were
    // counted as new under the button -- nor the page that joins
    // it to the newest again.
    // What came after the newest shown before: others' messages the view
    // shows. Where that one is not here any more -- its id changed as the
    // server acknowledged it, or it went -- nothing is counted: every
    // message held was, and a few new ones said 64. Nor the reader's own,
    // nor a room event the view hides.
    int after = 0;
    if (const auto was = std::ranges::find(all, shown_last, &message::id); was != all.end())
      for (auto it = std::next(was); it != all.end(); ++it)
        if (!it->outgoing && this->shown_in(*chosen, *it))
          ++after;
    unseen += after;
  }
  chat.area.parts.jump.set_unseen(unseen);
  shown_chat = chosen;
  shown_last = last;
  shown_detached = one->detached;
}

}  // namespace mux::ui
