// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.spaces.app -- What a space is to the chat list
// (contract::chat_folders, asked by mux.app.chat_list): a folder, not a chat
// -- but where it is shown as a forum; the rooms its folder shows, its own
// and those of the spaces in it, down every level; what Home leaves out of
// them, where it is chosen so; the spaces as tabs over the list where there
// are no bars; and the bars' icons -- Home, Direct messages and the
// account's spaces, each in the bars it is placed in, under the space
// chosen its own spaces, smaller a level down.
export module mux.plugin.spaces.app;

import std;
import splice;
import mux.composition;
import mux.core;
import mux.config;
import mux.ui;
import mux.app.kept;
import mux.app.network;
import mux.app.services;
import mux.plugin.spaces.types;

export namespace mux::plugins::spaces {

// A space: a folder, not a chat listed -- but where it opens as a list of
// chats (a forum).
inline bool is_folder(contract::chat_folders, type_tag<feature>, const ui::folder_context& ctx, const conversation& one) {
  return one.space && !ctx.scopes.contains(one.id.id);
}
// The rooms a space's folder shows: its own, and those of the spaces in
// it, down every level -- each space once. None for a folder not a space's.
inline std::optional<std::set<std::string>> folder_rooms(contract::chat_folders, type_tag<feature>, const ui::folder_context& ctx,
                                                         const ui::folder_t& folder) {
  const auto chosen_room = ui::space_of(folder);
  if (!chosen_room || !ctx.in)
    return std::nullopt;
  std::set<std::string> in_space, seen;
  std::vector<std::string> todo{*chosen_room};
  while (!todo.empty()) {
    const std::string at = std::move(todo.back());
    todo.pop_back();
    if (!seen.insert(at).second)
      continue;
    if (const auto* found = ctx.in->conversations.find(at); found != nullptr) {
      in_space.insert_range(found->children);
      todo.append_range(found->children);
    }
  }
  return in_space;
}
// What the account's spaces hold, out of Home: where it is chosen so, or
// the space is hidden from it.
inline std::set<std::string> out_of_home(contract::chat_folders, type_tag<feature>, const ui::folder_context& ctx) {
  if (!ctx.in)
    return {};
  return ctx.in->conversations.values() | std::views::filter([&](const conversation& each) {
           return each.space && (ctx.home_hides_spaced || ctx.hidden_from_home.contains(each.id));
         }) |
         std::views::transform([](const conversation& one) -> const std::vector<std::string>& { return one.children; }) | std::views::join |
         std::ranges::to<std::set>();
}
// The spaces as tabs over the list, where there are no bars.
inline void folder_tabs(contract::chat_folders, type_tag<feature>, const ui::folder_context& ctx,
                        std::vector<std::pair<std::string, ui::folder_t>>& out) {
  if (!ctx.in || ctx.spaces_on)
    return;
  for (const conversation& one : ctx.in->conversations.values() | std::views::filter(&conversation::space))
    out.emplace_back(ui::display_name(one), ui::folder::space{one.id.id});
}
// Whether the bars hold the folder chosen: Direct messages, or a space
// still there -- kept chosen, where the tabs do not list it.
inline bool keeps_folder(contract::chat_folders, type_tag<feature>, const ui::folder_context& ctx, const ui::folder_t& folder) {
  return ctx.spaces_on && spl::visit(spl::overloaded{[](const ui::folder::direct&) { return true; },
                                                     [&](const ui::folder::space& s) {
                                                       const auto* found = ctx.in ? ctx.in->conversations.find(s.room) : nullptr;
                                                       return found != nullptr && found->space;
                                                     },
                                                     [](const auto&) { return false; }},
                                     folder);
}
// The bars: Home, Direct messages and the account's spaces, each in the
// bars it is placed in (the side one, unplaced); under the space chosen --
// or one holding it -- its own spaces, smaller a level down. And them all,
// for the settings to list.
inline void space_bars(contract::chat_folders, type_tag<feature>, const ui::folder_context& ctx, const ui::list_shown& list,
                       ui::side_shown& out, ui::ui_shared& shared) {
    struct entry {
      mux::config::space_item_t item;
      mux::ui::folder_t shows;
      std::string id, name;
    };
    std::vector<entry> all{{mux::config::space_item::home{}, mux::ui::folder::all{}, "", "Home"},
                           {mux::config::space_item::direct{}, mux::ui::folder::direct{}, "", "Direct messages"}};
    const std::string address = list.account ? list.account->address : std::string();
    const auto* chats = ctx.in ? &ctx.in->conversations : nullptr;
    // Spaces in spaces: each under the first space found holding it.
    std::map<std::string, std::string> parent_of;
    std::map<std::string, std::vector<const mux::conversation*>> spaces_in;
    if (chats)
      for (const mux::conversation& one : chats->values() | std::views::filter(&mux::conversation::space)) {
        for (const std::string& child : one.children) {
          const auto* found = chats->find(child);
          if (found != nullptr && found->space && !ctx.scopes.contains(found->id.id) && child != one.id.id && !parent_of.contains(child)) {
            parent_of.emplace(child, one.id.id);
            spaces_in[one.id.id].push_back(found);
          }
        }
      }
    // The space chosen, and those holding it: open.
    std::set<std::string> open;
    if (const auto chosen_room = mux::ui::space_of(list.folder))
      for (std::string at = *chosen_room; open.insert(at).second;) {
        const auto up = parent_of.find(at);
        if (up == parent_of.end())
          break;
        at = up->second;
      }
    if (chats)
      for (const mux::conversation& one : chats->values() | std::views::filter([&](const mux::conversation& one) {
                              return one.space && !parent_of.contains(one.id.id) && !ctx.scopes.contains(one.id.id);
                            })) {
                              all.push_back({mux::config::space_item::space{one.id.id}, mux::ui::folder::space{one.id.id}, one.id.id, mux::ui::display_name(one)});
                            }
    const auto mine = ctx.space_places | std::views::filter([&](const mux::config::space_placed& p) { return p.account == address; }) | std::ranges::to<std::vector>();
    const auto in_bar = [&](const entry& one, const mux::config::space_bar_t& bar) {
      if (std::ranges::none_of(mine, [&](const auto& p) { return p.item == one.item; }))
        return bar == mux::config::space_bar_t{mux::config::space_bar::side{}};
      return std::ranges::any_of(mine, [&](const auto& p) { return p.item == one.item && p.bar == bar; });
    };
    const auto bar_of = [&](const mux::config::space_bar_t& bar) {
      auto ranked = all | std::views::enumerate | std::views::filter([&](const auto& one) { return in_bar(std::get<1>(one), bar); }) | std::views::transform([&](const auto& one) {
                      const auto& [i, each] = one;
                      const auto at = std::ranges::find_if(mine, [&](const auto& p) { return p.item == each.item && p.bar == bar; });
                      return std::pair<std::size_t, const entry*>{at == mine.end() ? mine.size() + static_cast<std::size_t>(i) : static_cast<std::size_t>(at - mine.begin()), &each};
                    }) | std::ranges::to<std::vector>();
      std::ranges::stable_sort(ranked, {}, &std::pair<std::size_t, const entry*>::first);
      return ranked | std::views::values | std::ranges::to<std::vector>();
    };
    // A bar's icons: its items, and under an open space its own spaces, a
    // level at a time -- smaller each level down.
    const auto icons_of = [&](const std::vector<const entry*>& items, float size) {
      std::vector<mux::ui::space_icon_shown> icons;
      for (const entry* one : items) {
        icons.push_back({one->item, one->shows, one->id, one->name, size, one->shows == list.folder});
        const auto room = mux::ui::space_of(one->shows);
        if (!room || !open.contains(*room))
          continue;
        std::vector<std::pair<const mux::conversation*, int>> todo;
        const auto push_children = [&](const std::string& of, int depth) {
          if (const auto found = spaces_in.find(of); found != spaces_in.end())
            for (const mux::conversation* sub : found->second | std::views::reverse) { todo.emplace_back(sub, depth); }
        };
        push_children(*room, 1);
        std::set<std::string> seen{*room};
        while (!todo.empty()) {
          const auto [sub, depth] = todo.back();
          todo.pop_back();
          if (!seen.insert(sub->id.id).second)
            continue;
          const mux::ui::folder_t shows = mux::ui::folder::space{sub->id.id};
          icons.push_back({mux::config::space_item::space{sub->id.id}, shows, sub->id.id, mux::ui::display_name(*sub),
                           std::max(20.0f, size - 6.0f * static_cast<float>(depth)), shows == list.folder});
          if (open.contains(sub->id.id))
            push_children(sub->id.id, depth + 1);
        }
      }
      return icons;
    };
    const auto side_items = bar_of(mux::config::space_bar::side{});
    out.side_bar = icons_of(side_items, 40.0f);
    out.top_bar = icons_of(bar_of(mux::config::space_bar::top{}), list.big_bars ? 50.0f : 30.0f);
    out.big = list.big_bars;
    out.top_on = ctx.spaces_on && ctx.top_bar_on;
    out.side_on = ctx.spaces_on && !side_items.empty();
    // For the settings to list them.
    shared.space_account = address;
    shared.space_items = all | std::views::transform([&](const entry& one) {
                           return mux::ui::space_item_shown{one.item, one.name, in_bar(one, mux::config::space_bar::side{}),
                                                            in_bar(one, mux::config::space_bar::top{})};
                         }) | std::ranges::to<std::vector>();
  }

// The spaces' part of the program: the bars as a drag leaves them, an
// item's bars as its menu chooses them, and Home without what spaces hold
// (or the direct chats), at a level.
struct part {
  app::services* s_ = nullptr;

  // Leaving a space: Element's box first, for which of its rooms to leave
  // with it; then they and it left.
  void apply(const ui::request::open_leave_space& one) {
    const conversation* space = s_->model->find(one.space);
    if (!space)
      return;
    leave_space_facts facts{.space = one.space, .name = space->name.empty() ? one.space.id : space->name};
    for (const std::string& child : space->children)
      if (const conversation* room = s_->model->find(conversation_id{one.space.account, child}))
        facts.rooms.push_back({child, room->name.empty() ? child : room->name});
    ui::show(*s_->showing, std::optional(std::move(facts)));
  }
  void apply(const leave_space& one) {
    ui::show<leave_space_facts>(*s_->showing, std::nullopt);
    if (s_->demo())
      return;
    for (const std::string& room : one.rooms)
      s_->net->leave(conversation_id{one.space.account, room});
    s_->net->leave(one.space);
    ui::change_shown<ui::chat_shown>(*s_->showing, [](ui::chat_shown& now) { now.info_open = false; });
  }
  void apply(const ui::request::close_shown<leave_space_facts>&) { ui::show<leave_space_facts>(*s_->showing, std::nullopt); }

  // A bar's order, as a drag left it: its items put there in that order --
  // the one moved taken out of the bar it came from, and from hidden.
  void apply(const ui::request::place_spaces& one) {
    const auto mine = [&](const mux::config::space_placed& p) { return p.account == one.account; };
    auto places = s_->kept->appearance().space_places;
    std::erase_if(places, [&](const mux::config::space_placed& p) {
      return mine(p) && (p.bar == one.bar || (one.moved && p.item == *one.moved && (p.bar == mux::config::space_bar_t{mux::config::space_bar::hidden{}} ||
                                                                                (one.from && p.bar == *one.from))));
    });
    // Where the item came from the side bar by default -- put nowhere -- the
    // rest of the side bar is put too, so it stays as it was.
    std::ranges::copy(one.order | std::views::transform([&](const mux::config::space_item_t& item) {
                        return mux::config::space_placed{one.account, item, one.bar};
                      }),
                      std::back_inserter(places));
    s_->kept->choose_field<&mux::config::look_settings::space_places>(std::move(places));
    (void)s_->kept->write();
    s_->refresh_due = true;
  }
  // Home without what spaces hold, at a level.
  void apply(const ui::request::set_home_hides& one) {
    spl::visit(spl::overloaded{[&](mux::choice_level::everywhere) {
                                       s_->kept->choose_field<&mux::config::look_settings::home_hides_spaced>(one.on.value_or(false));
                                       s_->looks.window.home_hides = s_->kept->appearance().home_hides_spaced;
                                     },
                                     [&](mux::choice_level::account) {
                                       s_->with_chosen_account([&](mux::config::account_t& account) {
                                         mux::config::home_hides_in(account) = one.on;
                                       });
                                     },
                                     [](mux::choice_level::chat) {}},
                  one.level);
    (void)s_->kept->write();
    s_->refresh_due = true;
    if (s_->on_settings_page<mux::ui::settings_page::appearance>())
      s_->settings_page(mux::ui::settings_page::appearance{s_->kept->appearance().theme, s_->kept->appearance().accent});
  }
  void apply(const ui::request::set_home_direct& one) {
    spl::visit(spl::overloaded{[&](mux::choice_level::everywhere) {
                                       s_->kept->choose_field<&mux::config::look_settings::home_hides_direct>(one.on.value_or(false));
                                       s_->looks.window.home_direct = s_->kept->appearance().home_hides_direct;
                                     },
                                     [&](mux::choice_level::account) {
                                       s_->with_chosen_account([&](mux::config::account_t& account) {
                                         mux::config::home_direct_in(account) = one.on;
                                       });
                                     },
                                     [](mux::choice_level::chat) {}},
                  one.level);
    (void)s_->kept->write();
    s_->refresh_due = true;
    if (s_->on_settings_page<mux::ui::settings_page::appearance>())
      s_->settings_page(mux::ui::settings_page::appearance{s_->kept->appearance().theme, s_->kept->appearance().accent});
  }
  // An item's bars, as chosen: the side, the top, both, or none -- hidden.
  void apply(const ui::request::set_space_bars& one) {
    auto places = s_->kept->appearance().space_places;
    std::erase_if(places, [&](const mux::config::space_placed& p) { return p.account == one.account && p.item == one.item; });
    if (one.side)
      places.push_back({one.account, one.item, mux::config::space_bar::side{}});
    if (one.top)
      places.push_back({one.account, one.item, mux::config::space_bar::top{}});
    if (!one.side && !one.top)
      places.push_back({one.account, one.item, mux::config::space_bar::hidden{}});
    s_->kept->choose_field<&mux::config::look_settings::space_places>(std::move(places));
    (void)s_->kept->write();
    s_->refresh_due = true;
    if (s_->on_settings_page<mux::ui::settings_page::appearance>())
      s_->settings_page(mux::ui::settings_page::appearance{s_->kept->appearance().theme, s_->kept->appearance().accent});
  }
};

// Its part, among the program's (mux.app.program).
inline part extend(contract::program_parts, type_tag<feature>, app::services& shared) { return part{&shared}; }

}  // namespace mux::plugins::spaces
