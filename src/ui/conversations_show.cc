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
import mux.ui.base;
import mux.plugins.ui.top;
import mux.ui.proto;
import :chat_list;
import mux.ui.messages;
import :header;
import :info;
import :conversations_side;
import :conversations_screen;

export namespace mux::ui {

auto conversations_screen::onKey(scene::phase::bubble, const scene::key::down& press, scene::Reply& reply) -> std::optional<Answer> {
  std::optional<Answer> asked;
  namespace keys = scene::keys;
  if (press.key == keys::kEscape && this->close_space_menu()) {
    reply.handle();
    return asked;
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
    return asked;
  }
  // A forum gone into, no chat of it open: Esc back out to the chats.
  if (press.key == keys::kEscape && !any && !chosen && forum_open) {
    this->close_forum();
    reply.handle();
    return asked;
  }
  if (!chosen && !pointed)
    return asked;
  // Only a forum gone to: Alt+Up and Alt+Down go on from it, nothing else.
  if (!chosen && !((press.key == keys::kUp || press.key == keys::kDown) && press.modifiers.template has<scene::modifier::alt>()))
    return asked;
  if (press.key == keys::kF && control) {
    asked = ::mux::ui::request::open_search{};
  } else if (press.key == keys::kK && control) {
    asked = ::mux::ui::request::ask_link{};
  } else if (press.key == keys::kUp && control) {
    asked = ::mux::ui::request::reply_step{true};
  } else if (press.key == keys::kDown && control) {
    asked = ::mux::ui::request::reply_step{false};
  } else if (press.key == keys::kUp && !any && composer_text(line).empty()) {
    asked = ::mux::ui::request::edit_last{};
  } else if (press.key == keys::kC && control && chat.parts.selection.visible()) {
    asked = ::mux::ui::request::selection_copy{};
  } else if (press.key == keys::kDelete && !any && chat.parts.selection.visible()) {
    asked = ::mux::ui::request::selection_delete{};
  } else if (press.key == keys::kC && control) {
    // Text nodes publish an owned copy of their selection as it changes.
    // Copy that snapshot without walking message widgets that may have
    // been replaced by a model refresh since the selection was made.
    const std::string selected = scene::selectedText();
    if (selected.empty())
      return asked;
    skiff::scene::setClipboardText(selected);
  } else if (press.key == keys::kEscape && !any && chat.parts.selection.visible()) {
    // Messages selected, the focus elsewhere than their bar: let go first.
    asked = ::mux::ui::request::selection_cancel{};
  } else if (press.key == keys::kEscape && !any && search.visible()) {
    asked = ::mux::ui::request::close_search{};
  } else if (press.key == keys::kEscape && !any && parts.threads.answering) {
    parts.threads.stop_answering();
  } else if (press.key == keys::kEscape && !any && composer_answering(line)) {
    asked = ::mux::ui::request::cancel_compose{};
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
      return asked;
    const auto index = static_cast<std::size_t>(at - rows.begin());
    const std::size_t to = back ? (index == 0 ? rows.size() - 1 : index - 1) : (index + 1) % rows.size();
    // A forum: gone to, lit, not opened -- Alt+Right opens it. A chat: opened.
    if (is_forum(rows[to])) {
      pointed = rows[to];
      if (last_model)
        this->show(*last_model, false);
    } else {
      pointed.reset();
      asked = ::mux::ui::request::choose{rows[to]};
    }
  } else if (press.key == keys::kPageUp || press.key == keys::kPageDown) {
    // A page of the messages, most of what is in view.
    auto& list = this->messages();
    const float page = list.bounds().height() * 0.9f;
    list.scrollTo(std::max(0.0f, list.current() + (press.key == keys::kPageUp ? -page : page)));
  } else if (press.key == keys::kEnd && control) {
    asked = ::mux::ui::request::jump_to_end{};
  } else if (press.key == keys::kEscape && !any) {
    // Nothing else to cancel: a step back -- the threads or the info shut,
    // else the chat closed, as tdesktop's Esc closes it.
    asked = this->step_back();
  } else {
    return asked;
  }
  reply.handle();
  return asked;
}

void conversations_screen::show_space_bars(const model& now) {
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
              found != nullptr && found->space && !this->shown_as_forum(*found) && child != one.id.id &&
              !parent_of.contains(child)) {
            parent_of.emplace(child, one.id.id);
            spaces_in[one.id.id].push_back(found);
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

void conversations_screen::onPointer(scene::phase::capture, const scene::pointer::down& press, scene::PointerReply& reply) {
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
    for (const auto& kept : std::get<0>(list.fChildren).fRows)
      if (list.toView(kept.fRow.bounds()).contains(press.x, press.y))
        swipe_row = kept.fKey;
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

auto conversations_screen::onPointer(scene::phase::capture, const scene::pointer::up& lift, scene::PointerReply& reply) -> std::optional<Answer> {
  std::optional<Answer> asked;
  const std::optional<skia::SkPoint> from = std::exchange(swipe_from, std::nullopt);
  if (!from || !single)
    return asked;
  const float dx = lift.x - from->fX;
  const float dy = lift.y - from->fY;
  if (dx > 90.0f && std::abs(dy) < dx * 0.5f) {
    if (chosen)
      asked = this->step_back();
    else
      asked = ::mux::ui::request::open_drawer{};
    reply.handle();
  } else if (const auto row = std::exchange(swipe_row, std::nullopt); row && !chosen && -dx > 90.0f && std::abs(dy) < -dx * 0.5f) {
    asked = ::mux::ui::request::toggle_mute_of{*row};
    reply.handle();
  }
  return asked;
}

void conversations_screen::update(double now_ms) {
  if (slide_wait > 0 && --slide_wait == 0)
    list_in.setTarget(1.0f);
  if (list_in.step(now_ms))
    this->place_list();
  this->follow_docked();
  if (pane_in.step(now_ms)) {
    this->place_pane();
    this->markDamaged();
  }
  // The info's members near their end, with more: the next few made; or a
  // member opened past those made, theirs worked out.
  if (last_model && chosen && info.visible() && (info.wants_show || info.wants_more()))
    if (const conversation* one = last_model->find(*chosen)) {
      if (!info.wants_show)
        info.members_made += info.kMembersStep;
      info.wants_show = false;
      info.show(*one, *last_model, muted.contains(one->id));
    }
  // The restored offset is unclamped until the destination list's first
  // layout; virtualize at that offset rather than the previous list's limit.
  if (restoring_list_at && list.fState.fLayoutValid)
    restoring_list_at.reset();
  // The list scrolled past the rows made: those for where it is now made,
  // those far from it let go.
  if (last_model && list.visible() && !restoring_list_at && this->chats_window() != std::pair{chats_from, chats_made})
    this->show_rows(*last_model);
  // The panels' opacity on its way to the chat's.
  if (auto& ease = needs_.paint->ease; ease.t.step(now_ms)) {
    needs_.paint->panel.opacity = ease.from + (ease.to - ease.from) * ease.t.value();
    this->markDamaged();
  }
  // The pin the bar shows, as the view moves: the one above it. Worked out
  // again only where the view, the messages or the pins moved: it looks
  // each pin up in the whole timeline.
  if (chosen && last_model)
    if (const conversation* one = last_model->find(*chosen); one && !one->pinned.empty()) {
      const pin_inputs now{*chosen, this->messages().current(), one->timeline.size(), one->pinned.size(),
                           one->timeline.empty() ? std::string() : one->timeline.front().id};
      if (now != pin_worked_out) {
        pin_worked_out = now;
        if (const std::size_t want = this->pin_above(*one); want != std::min(pinned_step, one->pinned.size() - 1)) {
          pinned_step = want;
          this->show_pinned(one);
        }
      }
    }
  this->find_mentions();
  this->find_emoji();
  // What is in the composer: typing while there is text in it.
  if (const bool has_text = !composer_text(line).empty(); has_text != was_typing || (has_text && composer_text(line) != typed_last)) {
    was_typing = has_text;
    typed_last = composer_text(line);
    if (wants_) want(*wants_, ::mux::ui::request::typing{has_text});
  }
  if (side.search.field.text() != searched && last_model) {
    searched = side.search.field.text();
    this->show(*last_model, false);
  }
  // The @ and the heart, the way back and "\u2193": the list's corner, as the
  // program says (chat_ways). Come back to the chat a jump left by hand:
  // that way back is the program's to let go.
}

// What the list shows, from the model and what is chosen: the chats of
// the account shown in the folder chosen -- or the forum open -- with the
// other accounts' placed here, those searched for, invites first, then by
// their newest.
chat_listing conversations_screen::listing_of(const model& now) const {
  chat_listing out;
  constexpr auto lower = mux::logic::folded;
  const std::string wanted = lower(side.search.field.text());
  const account* in = current ? &now.accounts().at(*current) : nullptr;
  if (in == nullptr)
    return out;
  // The rooms of the space chosen: its own, and those of the spaces in it,
  // down every level -- each space once, however they hold each other.
  std::set<std::string> in_space;
  if (const std::optional<std::string> chosen_room = space_of(folder)) {
    std::set<std::string> seen;
    std::vector<std::string> todo{*chosen_room};
    while (!todo.empty()) {
      const std::string at = std::move(todo.back());
      todo.pop_back();
      if (!seen.insert(at).second)
        continue;
      if (const auto* found = in->conversations.find(at); found != nullptr) {
        in_space.insert_range(found->children);
        todo.append_range(found->children);
      }
    }
  }
  // Forums: each listed as one chat; their rooms in them, not beside them.
  const auto rooms_of = [](const conversation& one) -> const std::vector<std::string>& { return one.children; };
  const std::set<std::string> in_forums = in->conversations.values() |
                                          std::views::filter([&](const conversation& one) { return this->shown_as_forum(one); }) |
                                          std::views::transform(rooms_of) | std::views::join | std::ranges::to<std::set>();
  const conversation* forum = forum_open ? in->conversations.find(*forum_open) : nullptr;
  // What the account's spaces hold: out of Home, where it is chosen so --
  // but direct messages. Every space's, where Home hides all that spaces
  // hold; else those of the spaces that hide theirs.
  const bool home = folder == folder_t{folder::all{}};
  const std::set<std::string> in_spaces = in->conversations.values() | std::views::filter([&](const conversation& each) {
                                            return home && each.space && (home_hides_spaced || hidden_from_home.contains(each.id));
                                          }) |
                                          std::views::transform(rooms_of) | std::views::join | std::ranges::to<std::set>();
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
                                      [&](const folder::direct&) { return direct(one); }},
                      folder);
  };
  // Upgraded away, its new room here: only the new one listed, as Element
  // -- where the two agree: its tombstone names the new room, and the new
  // room's creation names it as what it continues. One side alone is not
  // believed (as matrix-js-sdk's CVE-2025-59160, the other way round): a
  // room could otherwise hide another from the list.
  const auto found_by = [&](const conversation& one) {
    if (const auto successor = proto::successor_of(protocol_state_of(*needs_.shared, one.id.account), one))
      if (const auto* next = in->conversations.find(*successor);
          next != nullptr && proto::predecessor_of(protocol_state_of(*needs_.shared, one.id.account), *next) == one.id.id)
        return false;
    return in_folder(one) && (wanted.empty() || lower(display_name(one)).contains(wanted) || lower(one.id.id).contains(wanted));
  };
  // Its own chats -- not those moved to another account's list -- and the
  // other accounts' listed in it.
  std::vector<const conversation*> chats = in->conversations.values() |
                                           std::views::filter([&](const conversation& one) { return !moved_out.contains(one.id) && found_by(one); }) |
                                           std::views::transform([](const conversation& one) { return &one; }) | std::ranges::to<std::vector>();
  if (const auto extra = listed_in.find(*current); extra != listed_in.end())
    std::ranges::copy(extra->second | std::views::transform([&](const conversation_id& id) { return now.find(id); }) |
                          std::views::filter([&](const conversation* one) { return one != nullptr && found_by(*one); }),
                      std::back_inserter(chats));
  // The strip of a chat listed here from another account.
  const auto strip_for = [&](const conversation* one) -> std::optional<skia::SkColor> {
    if (one->id.account == *current)
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
  std::ranges::for_each(chats, [&](const conversation*& one) {
    if (!this->shown_as_forum(*one))
      return;
    conversation made = *one;
    made.timeline.clear();
    made.read_up_to.reset();
    made.detached = false;
    made.unread = 0;
    made.highlights = 0;
    auto topics = one->children | std::views::transform([&](const std::string& child) { return in->conversations.find(child); }) |
                        std::views::filter([](const conversation* topic) { return topic != nullptr && !topic->space; });
    made.unread = std::ranges::fold_left(topics | std::views::transform([&](const conversation* topic) { return topic->unread_here(events_of(topic)); }),
                                         std::int64_t{0}, std::plus{});
    made.highlights = std::ranges::fold_left(topics | std::views::transform(&conversation::highlights), decltype(made.highlights){0}, std::plus{});
    // The newest of its topics, and which.
    const auto newest_in = [&](const conversation* topic) { return newest(*topic, events_of(topic)); };
    auto with_one = topics | std::views::filter([&](const conversation* topic) { return newest_in(topic) != nullptr; });
    if (const auto best = std::ranges::max_element(with_one, {}, [&](const conversation* topic) { return newest_in(topic)->at; });
        best != std::ranges::end(with_one)) {
      made.timeline.push_back(*newest_in(*best));
      // Said as tdesktop says a forum's newest: in which topic, by whom.
      made.forum_topic = display_name(**best);
      made.members = (*best)->members;
    }
    one = &out.forums.insert_or_assign(one->id, std::move(made)).first->second;
  });
  // Invites first, as Element lists them; then by their newest.
  std::ranges::sort(chats, std::ranges::greater{}, [&](const conversation* one) {
    const message* last = newest(*one, events_of(one));
    return std::pair{one->invite.has_value(), last ? last->at : std::chrono::sys_time<std::chrono::milliseconds>{}};
  });
  // The chat open -- or, where Alt+Up or Alt+Down went to a forum, that.
  const auto is_chosen = [&](const conversation* one) { return pointed ? *pointed == one->id : chosen && *chosen == one->id; };
  out.chats = chats | std::views::transform([&](const conversation* one) {
                return listed_chat{.id = one->id,
                                            .view = conversation_row::view_of(*needs_.shared, *one, is_chosen(one), muted.contains(one->id),
                                                                                       draft_of(one->id), events_of(one), strip_for(one)),
                                            .events = events_of(one),
                                            .strip = strip_for(one),
                                            .height = conversation_row::height_of(*one)};
              }) |
              std::ranges::to<std::vector>();
  return out;
}

// The rows for where the list is now: those a screen above the view to two
// below it made from the listing, the rest stand-ins of their height (the
// rows' padding above and below).
void conversations_screen::show_rows(const model& now) {
  auto& rows = std::get<0>(list.fChildren);
  const auto key_of = [](const auto& kept) { return kept.fKey; };
  const std::vector<conversation_id> listed_before = rows.fRows | std::views::transform(key_of) | std::ranges::to<std::vector>();
  chats_listed = listing.chats.size();
  order = listing.chats | std::views::transform(&listed_chat::id) | std::ranges::to<std::vector>();
  const std::vector<float> heights = listing.chats | std::views::transform(&listed_chat::height) | std::ranges::to<std::vector>();
  tops.assign(1, 0.0f);
  std::inclusive_scan(heights.begin(), heights.end(), std::back_inserter(tops));
  std::tie(chats_from, chats_made) = this->chats_window();
  rows.apply({.padding = scene::Margin{tops[chats_from], 0.0f, tops.back() - tops[chats_made], 0.0f}});
  const auto items = listing.chats | std::views::drop(chats_from) | std::views::take(chats_made - chats_from) |
                     std::views::transform([&](const listed_chat& one) {
                       return chat_item{one.id, one.view, listing.find(now, one.id), &one, &needs_};
                     });
  if (rows.read(items)) {
    list.invalidateLayout();
    // A chat come or gone -- or moved to another place: the whole list
    // painted again, not only what says it moved.
    if (!std::ranges::equal(listed_before, rows.fRows | std::views::transform(key_of)))
      list.markDamaged();
  }
}

void conversations_screen::show(const model& now, bool with_chat) {
  const auto previous_account = current;
  last_model = &now;
  this->read_chat_settings(now);
  // Files attached where the chat's account sends them, and its protocol
  // allows it now: no paperclip otherwise, in the chat or its thread.
  if (chosen) {
    const bool files = may_send_files(*needs_.shared, chosen->account);
    input_attach(composer_input(line)).setVisible(files);
    input_attach(composer_input(parts.threads.parts.line)).setVisible(files);
  }
  if (wanted && now.accounts().contains(*wanted)) {
    current = std::exchange(wanted, std::nullopt);
  } else if (!current || !now.accounts().contains(*current)) {
    current = now.accounts().empty() ? std::nullopt : std::optional<account_id>(now.accounts().keyAt(0));
  }
  if (current != previous_account) {
    asked_elsewhere.clear();
    rooms_elsewhere.clear();
    people_elsewhere.clear();
  }
  this->show_connections(now);
  // What the chat list's menus offer: the account shown, and the accounts
  // there are to move a chat to.
  side.current_account = current;
  side.accounts_known = now.accounts().keys() | std::ranges::to<std::vector>();
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
                           return found != nullptr && found->space;
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
  // The forum open: still one; its rooms, the list.
  if (forum_open && (!current || !this->is_forum(conversation_id{*current, *forum_open})))
    forum_open.reset();
  const account* in = current ? &now.accounts().at(*current) : nullptr;
  const conversation* forum = forum_open && in ? in->conversations.find(*forum_open) : nullptr;
  side.forum_head.setVisible(forum != nullptr);
  if (forum)
    side.forum_head.parts.name.setText(display_name(*forum));
  this->restore_list_position();
  // The chats listed, as the model has them now, made into rows.
  listing = this->listing_of(now);
  this->show_rows(now);
  const bool none = now.accounts().empty();
  // The messages' area, not only its list: hidden, it no longer takes the
  // column's height and pushes what is said instead to the bottom.
  // A chat open: its head, messages and composer. None chosen: the hint.
  const bool open = !none && chosen.has_value();
  for (scene::Node* shown : std::initializer_list<scene::Node*>{&header, &chat.area, &line})
    shown->setVisible(open);
  chat.hint.setVisible(!none && !chosen.has_value());
  // A search of two letters or more also asks the selected account's server --
  // the server is asked for rooms and people that do, once for each
  // thing typed.
  const std::string& typed = side.search.field.text();
  const bool find_rooms = current && proto::offers(protocol_state_of(*needs_.shared, *current), proto::feature::room_directory{});
  const bool find_people = current && proto::offers(protocol_state_of(*needs_.shared, *current), proto::feature::people_directory{});
  const bool elsewhere = (find_rooms || find_people) && !wanted.empty() && typed.size() >= 2;
  if (elsewhere && typed != asked_elsewhere) {
    asked_elsewhere = typed;
    rooms_elsewhere.clear();
    people_elsewhere.clear();
    rooms_came = !find_rooms;
    people_came = !find_people;
    if (wants_) want(*wants_, ::mux::ui::request::search_elsewhere{typed, current});
    this->show_elsewhere();
  } else if (!elsewhere && !asked_elsewhere.empty()) {
    asked_elsewhere.clear();
    rooms_elsewhere.clear();
    people_elsewhere.clear();
    this->show_elsewhere();
  }
  side.elsewhere.setVisible(elsewhere);
  side.list.setVisible(!listing.chats.empty() || !elsewhere);
  no_chats.setVisible(!none && listing.chats.empty() && !elsewhere);
  chat.empty.setVisible(none);
  this->show_info();
  // Laid out again, repainting only what moves: what changed repaints
  // itself. Invalidated, the whole window was painted at every change in
  // the model -- a hidden event's too.
  fState.relayoutQuietly();
  if (with_chat)
    this->show_conversation(now);
}

void conversations_screen::show_banners(const conversation* one, const model& now) {
  const auto banners = one ? proto::composer_banners(protocol_state_of(*needs_.shared, one->id.account), *one, now)
                           : std::vector<proto::part::banner>{};
  const std::string said = std::ranges::to<std::string>(std::views::join_with(std::views::transform(banners, &proto::part::banner::text), '\n'));
  // The first's button, where it has one.
  const std::string label = banners.empty() ? std::string() : banners.front().button;
  auto& bar = chat.parts.banner;
  auto& button = bar.parts.verify;
  if (label.empty() == button.has_value()) {
    if (label.empty())
      button.reset();
    else {
      button.emplace(needs_.colours->widgets, label, banner_press{this});
      button->apply({.width = 112.0f, .height = 36.0f, .alignSelf = scene::align::kMiddle});
    }
    bar.invalidateLayout();
    chat.invalidateLayout();
  } else if (button)
    button->setLabel(label);
  // The protocol's own node under the head, made again for the chat.
  chat.parts.their_head.reset();
  if (one)
    spl::visit(
        [&](const auto& now) {
          using head_view_defaults::make_head_view;
          this->place_head_view(make_head_view(now, *one));
        },
        protocol_state_of(*needs_.shared, one->id.account));
  // The protocol's own node over the composer, made again for the chat.
  chat.parts.their_view.reset();
  if (one)
    spl::visit(
        [&](const auto& now) {
          using composer_view_defaults::make_composer_view;
          this->place_composer_view(make_composer_view(now, *one));
        },
        protocol_state_of(*needs_.shared, one->id.account));
  if (!banners.empty())
    bar.apply({.background = (tone_colour(*needs_.colours, banners.front().tone) & 0x00FFFFFFu) | (0x22u << 24)});
  if (bar.parts.text.text() != said)
    bar.parts.text.setText(said);
  if (one && said.empty())
    dismissed_banners.erase(one->id);
  const auto dismissed = one ? dismissed_banners.find(one->id) : dismissed_banners.end();
  const bool visible = !said.empty() && (dismissed == dismissed_banners.end() || dismissed->second != said);
  if (bar.visible() != visible) {
    bar.setVisible(visible);
    chat.invalidateLayout();
  }
}

void conversations_screen::show_conversation(const model& now) {
  this->show_connections(now);
  const conversation* one = chosen ? now.find(*chosen) : nullptr;
  header.choose(now, chosen, single);
  this->show_banners(one, now);
  if (pinned_of != chosen) {
    pinned_of = chosen;
    pinned_step = std::numeric_limits<std::size_t>::max();
  }
  this->show_pinned(one);
  this->show_info();
  if (!one)
    return;
  // Only while it is open: its members were made at every switch of chat,
  // the panel shut or not. Opened, it is shown then (toggle_info).
  info.choose(now, chosen, muted.contains(one->id));
  this->show_wallpaper();
  // Another chat shown: its thread let go.
  if (shown_chat != chosen)
    parts.threads.open.reset();
  parts.threads.choose(now, chosen);
  shown_chat = chosen;
  // The messages themselves: the list's, told by the program (chat_told)
  // and read from the chats model where it keeps them.
}
}  // namespace mux::ui
