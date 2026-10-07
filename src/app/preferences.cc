// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.preferences: what the user sets for an account and for a chat --
// muted, its strip and colour, its notifications, its room events, where a
// chat is placed in other accounts' lists, the spaces' bars, receipts,
// typing, link previews, an account's proxy -- kept, and shown where it is
// set. A part of the program: it reaches the settings it is given and the
// rest through the services.
export module mux.app.preferences;

import std;
import splice;
import mux.core;
import mux.config;
import mux.protocols;
import mux.logic.room_events;
import mux.ui;
import mux.ui.proto;
import mux.app.network;
import mux.app.kept;
import mux.app.services;
import mux.app.requests;
import mux.app.proxies;

export namespace mux::app {

class preferences_part {
 public:
  using accounts = mux::ui::accounts_panel<actions>;
  preferences_part(services& shared, kept_settings& kept, proxies_part& proxying)
      : s_(&shared), k_(&kept), proxying_(&proxying) {}
  preferences_part(const preferences_part&) = delete;
  preferences_part& operator=(const preferences_part&) = delete;

  void apply(const request::toggle_mute&) {
    auto& screen = s_->root().main();
    if (!screen.chosen)
      return;
    if (!k_->muted.erase(*screen.chosen))
      k_->muted.insert(*screen.chosen);
    (void)k_->write();
    s_->refresh_due = true;
  }
  void apply(const request::toggle_mute_of& one) {
    if (!k_->muted.erase(one.which))
      k_->muted.insert(one.which);
    (void)k_->write();
    s_->refresh_due = true;
  }
  void apply(const request::flip_only_verified&) {
    s_->with_chosen_account([&](accounts& panel, mux::config::account_t& account) {
      auto* kept = mux::config::only_verified_in(account);
      if (kept == nullptr)
        return;
      *kept = !kept->value_or(false);
      // Shown on its page, where the page shown is one that shows it.
      panel.tell_shown([&](auto& page) -> decltype(void(page.show_only_verified(true))) { page.show_only_verified(**kept); });
      // Told to that account, running, where its client can: its sessions'
      // keys go so from now.
      s_->net->set_only_verified(kept_settings::id_of(account), **kept);
      (void)k_->write();
    });
  }
  // Its read mentions shared with its other sessions, or sealed there: the
  // one switched, kept, shown on its page, told to it running.
  void flip_mentions(mux::config::account_t& account, std::optional<bool>& kept, accounts& panel) {
    kept = !kept.value_or(false);
    const auto now = mux::config::mentions_choice_of(account);
    if (!now)
      return;
    if (auto* page = panel.privacy())
      page->show_mentions(*now);
    s_->net->set_mentions_sharing(kept_settings::id_of(account), now->shared, now->sealed);
    (void)k_->write();
  }
  void apply(const request::flip_account_mentions_shared&) {
    s_->with_chosen_account([&](accounts& panel, mux::config::account_t& account) {
      if (std::optional<bool>* kept = mux::config::mentions_shared_in(account))
        this->flip_mentions(account, *kept, panel);
    });
  }
  void apply(const request::flip_account_mentions_sealed&) {
    s_->with_chosen_account([&](accounts& panel, mux::config::account_t& account) {
      if (std::optional<bool>* kept = mux::config::mentions_sealed_in(account))
        this->flip_mentions(account, *kept, panel);
    });
  }
  void apply(const request::flip_account_receipts&) {
    s_->with_chosen_account([&](accounts& panel, mux::config::account_t& account) {
      auto& kept = mux::config::read_receipts_in(account);
      kept = !kept.value_or(true);
      if (auto* page = panel.privacy())
        page->show(*kept);
      (void)k_->write();
    });
  }
  // An account's colour chosen, and its strip on its chats in other lists:
  // kept, and the lists shown again.
  void apply(const request::set_account_colour& one) {
    s_->with_chosen_account([&](accounts& panel, mux::config::account_t& account) {
      mux::config::colour_in(account) = std::string(spl::visit([](const auto& each) { return mux::config::word_of(each); }, one.colour));
      if (auto* page = panel.chats_page())
        page->show_colour(mux::config::colour_of(account), mux::config::strip_of(account));
      (void)k_->write();
    });
    s_->refresh_due = true;
  }
  void apply(const request::flip_account_strip&) {
    s_->with_chosen_account([&](accounts& panel, mux::config::account_t& account) {
      auto& kept = mux::config::strip_in(account);
      kept = !kept.value_or(true);
      if (auto* page = panel.chats_page())
        page->show_colour(mux::config::colour_of(account), *kept);
      (void)k_->write();
    });
    s_->refresh_due = true;
  }
  void apply(const request::place_chat& one) {
    (void)s_->root().main().close_space_menu();
    if (one.to == one.chat.account)
      return;
    // Moved: out of every other list it was moved to, into this one.
    if (one.moved)
      std::erase_if(k_->placements, [&](const mux::config::chat_placement& each) {
        return each.account == one.chat.account.address && each.conversation == one.chat.id && each.moved;
      });
    if (auto* kept = this->placement_of(one.chat, one.to))
      kept->moved = one.moved;
    else
      k_->placements.push_back({.account = one.chat.account.address, .conversation = one.chat.id, .listed_in = one.to.address,
                            .moved = one.moved});
    (void)k_->write();
    s_->refresh_due = true;
  }
  void apply(const request::unplace_chat& one) {
    (void)s_->root().main().close_space_menu();
    std::erase_if(k_->placements, [&](const mux::config::chat_placement& each) {
      return each.account == one.chat.account.address && each.conversation == one.chat.id && each.listed_in == one.from.address;
    });
    (void)k_->write();
    s_->refresh_due = true;
  }
  void apply(const request::flip_chat_strip& one) {
    (void)s_->root().main().close_space_menu();
    if (auto* kept = this->placement_of(one.chat, one.in)) {
      const auto own = k_->find(one.chat.account.address);
      const bool now = kept->strip.value_or(own == k_->saved.end() || mux::config::strip_of(*own));
      kept->strip = !now;
      (void)k_->write();
    }
    s_->refresh_due = true;
  }
  void apply(const request::set_chat_strip_colour& one) {
    (void)s_->root().main().close_space_menu();
    if (auto* kept = this->placement_of(one.chat, one.in)) {
      kept->strip_colour = std::string(spl::visit([](const auto& each) { return mux::config::word_of(each); }, one.colour));
      kept->strip = true;
      (void)k_->write();
    }
    s_->refresh_due = true;
  }
  // A notification setting at a level: every chat's -- said, the client's
  // own --, the chosen account's, or the chat or space being managed.
  void apply(const request::set_notify_choice& one) {
    spl::visit([&](auto which) { this->set_notify(one.level, which, one.value); }, one.which);
    (void)k_->write();
    s_->refresh_due = true;
  }
  template <class Setting>
  void set_notify(const mux::choice_level_t& level, Setting which, std::optional<bool> value) {
    spl::visit(spl::overloaded{[&](mux::choice_level::everywhere) { Setting::set(k_->notifications, value.value_or(Setting::unsaid)); },
                                     [&](mux::choice_level::account) {
                                       s_->with_chosen_account([&](accounts&, mux::config::account_t& account) {
                                         account.shared.*Setting::account = value;
                                       });
                                     },
                                     [&](mux::choice_level::chat) {
                                       if (const auto chosen = s_->managed())
                                         this->set_chat_notify(*chosen, which, value);
                                     }},
                  level);
  }
  // A chat's (or space's): kept with its others, and none kept where all
  // are as the level above.
  template <class Setting>
  void set_chat_notify(const mux::conversation_id& chat, Setting, std::optional<bool> value) {
    auto& own = k_->notify_in[chat];
    own.*Setting::chat = value;
    if (own == mux::config::notify_choices{})
      k_->notify_in.erase(chat);
  }
  // Notifications off is muted -- the chat list's mute, the same.
  void set_chat_notify(const mux::conversation_id& chat, mux::config::notify_setting::on which, std::optional<bool> value) {
    if (value == false)
      k_->muted.insert(chat);
    else
      k_->muted.erase(chat);
    this->set_chat_notify<mux::config::notify_setting::on>(chat, which, value == true ? value : std::nullopt);
  }

  // Which room events show, as chosen at a level: all of them, or one kind --
  // none said, as the level under says.
  void apply(const request::set_room_event_kind& one) {
    const auto set_kind = [&](std::optional<mux::config::room_event_kinds>& kinds) {
      if (!kinds)
        kinds.emplace();
      mux::logic::choice_in(*kinds, *one.kind) = one.show;
    };
    spl::visit(spl::overloaded{[&](mux::choice_level::everywhere) {
                                 if (one.kind)
                                   set_kind(k_->history.room_event_kinds);
                                 else
                                   k_->history.show_room_events = one.show.value_or(true);
                               },
                               [&](mux::choice_level::account) {
                                 s_->with_chosen_account([&](accounts&, mux::config::account_t& account) {
                                   if (one.kind)
                                     set_kind(mux::config::room_event_kinds_in(account));
                                   else
                                     mux::config::room_events_in(account) = one.show;
                                 });
                               },
                               [&](mux::choice_level::chat) {
                                 const auto chosen = s_->managed();
                                 if (!chosen)
                                   return;
                                 if (one.kind)
                                   mux::logic::choice_in(k_->room_event_kinds[*chosen], *one.kind) = one.show;
                                 else if (one.show)
                                   k_->room_events.insert_or_assign(*chosen, *one.show);
                                 else
                                   k_->room_events.erase(*chosen);
                               }},
               one.level);
    (void)k_->write();
    s_->refresh_due = true;
  }
  // A bar's order, as a drag left it: its items put there in that order --
  // the one moved taken out of the bar it came from, and from hidden.
  void apply(const request::place_spaces& one) {
    const auto mine = [&](const mux::config::space_placed& p) { return p.account == one.account; };
    std::erase_if(k_->space_places, [&](const mux::config::space_placed& p) {
      return mine(p) && (p.bar == one.bar || (one.moved && p.item == *one.moved && (p.bar == mux::config::space_bar_t{mux::config::space_bar::hidden{}} ||
                                                                                (one.from && p.bar == *one.from))));
    });
    // Where the item came from the side bar by default -- put nowhere -- the
    // rest of the side bar is put too, so it stays as it was.
    std::ranges::copy(std::views::transform(one.order, [&](const mux::config::space_item_t& item) {
                        return mux::config::space_placed{one.account, item, one.bar};
                      }),
                      std::back_inserter(k_->space_places));
    (void)k_->write();
    s_->refresh_due = true;
  }
  // Home without what spaces hold, at a level.
  void apply(const request::set_home_hides& one) {
    spl::visit(spl::overloaded{[&](mux::choice_level::everywhere) {
                                       k_->home_hides_spaced = one.on.value_or(false);
                                       s_->looks.window.home_hides = k_->home_hides_spaced;
                                     },
                                     [&](mux::choice_level::account) {
                                       s_->with_chosen_account([&](accounts&, mux::config::account_t& account) {
                                         mux::config::home_hides_in(account) = one.on;
                                       });
                                     },
                                     [](mux::choice_level::chat) {}},
                  one.level);
    (void)k_->write();
    s_->refresh_due = true;
    if (auto* up = s_->root().settings_up(); up && up->appearance())
      up->show_appearance(k_->theme, k_->accent);
  }
  void apply(const request::set_home_direct& one) {
    spl::visit(spl::overloaded{[&](mux::choice_level::everywhere) {
                                       k_->home_hides_direct = one.on.value_or(false);
                                       s_->looks.window.home_direct = k_->home_hides_direct;
                                     },
                                     [&](mux::choice_level::account) {
                                       s_->with_chosen_account([&](accounts&, mux::config::account_t& account) {
                                         mux::config::home_direct_in(account) = one.on;
                                       });
                                     },
                                     [](mux::choice_level::chat) {}},
                  one.level);
    (void)k_->write();
    s_->refresh_due = true;
    if (auto* up = s_->root().settings_up(); up && up->appearance())
      up->show_appearance(k_->theme, k_->accent);
  }
  // An item's bars, as chosen: the side, the top, both, or none -- hidden.
  void apply(const request::set_space_bars& one) {
    (void)s_->root().main().close_space_menu();
    std::erase_if(k_->space_places, [&](const mux::config::space_placed& p) { return p.account == one.account && p.item == one.item; });
    if (one.side)
      k_->space_places.push_back({one.account, one.item, mux::config::space_bar::side{}});
    if (one.top)
      k_->space_places.push_back({one.account, one.item, mux::config::space_bar::top{}});
    if (!one.side && !one.top)
      k_->space_places.push_back({one.account, one.item, mux::config::space_bar::hidden{}});
    (void)k_->write();
    s_->refresh_due = true;
    if (auto* up = s_->root().settings_up(); up && up->appearance())
      up->show_appearance(k_->theme, k_->accent);
  }
  // How a level shows room events, as a whole: what it holds replaced.
  void apply(const request::set_room_events& one) {
    spl::visit(spl::overloaded{[&](mux::choice_level::everywhere) {
                                       k_->history.show_room_events = one.all.value_or(true);
                                       k_->history.room_event_kinds = one.kinds;
                                     },
                                     [&](mux::choice_level::account) {
                                       s_->with_chosen_account([&](accounts&, mux::config::account_t& account) {
                                         mux::config::room_events_in(account) = one.all;
                                         mux::config::room_event_kinds_in(account) = one.kinds;
                                       });
                                     },
                                     [&](mux::choice_level::chat) {
                                       const auto chosen = s_->managed();
                                       if (!chosen)
                                         return;
                                       if (one.all)
                                         k_->room_events.insert_or_assign(*chosen, *one.all);
                                       else
                                         k_->room_events.erase(*chosen);
                                       if (one.kinds)
                                         k_->room_event_kinds.insert_or_assign(*chosen, *one.kinds);
                                       else
                                         k_->room_event_kinds.erase(*chosen);
                                     }},
                  one.level);
    (void)k_->write();
    s_->refresh_due = true;
  }
  // How far a jump's search pages back, at a level.
  void apply(const request::set_jump_search& one) {
    spl::visit(spl::overloaded{[&](mux::choice_level::everywhere) { k_->history.jump_search = one.most.value_or(5000); },
                               [&](mux::choice_level::account) {
                                 s_->with_chosen_account([&](accounts&, mux::config::account_t& account) {
                                   mux::config::jump_search_in(account) = one.most;
                                 });
                               },
                               [&](mux::choice_level::chat) {
                                 const auto chosen = s_->managed();
                                 if (!chosen)
                                   return;
                                 if (one.most)
                                   k_->jump_search_in.insert_or_assign(*chosen, *one.most);
                                 else
                                   k_->jump_search_in.erase(*chosen);
                               }},
               one.level);
    (void)k_->write();
    s_->refresh_due = true;
  }
  // Link previews, at a level.
  void apply(const request::set_link_previews& one) {
    spl::visit(spl::overloaded{[&](mux::choice_level::everywhere) { k_->history.link_previews = one.show.value_or(true); },
                               [&](mux::choice_level::account) {
                                 s_->with_chosen_account([&](accounts&, mux::config::account_t& account) {
                                   mux::config::link_previews_in(account) = one.show;
                                 });
                               },
                               [&](mux::choice_level::chat) {
                                 const auto chosen = s_->managed();
                                 if (!chosen)
                                   return;
                                 if (one.show)
                                   k_->previews_shown_in.insert_or_assign(*chosen, *one.show);
                                 else
                                   k_->previews_shown_in.erase(*chosen);
                               }},
               one.level);
    (void)k_->write();
    s_->refresh_due = true;
  }
  // Where link previews come from, at a level.
  void apply(const request::set_previews_direct& one) {
    spl::visit(spl::overloaded{[&](mux::choice_level::everywhere) { k_->history.previews_direct = one.direct.value_or(false); },
                                     [&](mux::choice_level::account) {
                                       s_->with_chosen_account([&](accounts&, mux::config::account_t& account) {
                                         mux::config::previews_direct_in(account) = one.direct;
                                       });
                                     },
                                     [&](mux::choice_level::chat) {
                                       const auto chosen = s_->managed();
                                       if (!chosen)
                                         return;
                                       if (one.direct)
                                         k_->previews_direct_in.insert_or_assign(*chosen, *one.direct);
                                       else
                                         k_->previews_direct_in.erase(*chosen);
                                     }},
                  one.level);
    (void)k_->write();
    s_->refresh_due = true;
  }
  // Whether others are told one is typing, at a level.
  void apply(const request::set_typing_sent& one) {
    spl::visit(spl::overloaded{[&](mux::choice_level::everywhere) { k_->history.send_typing = one.send.value_or(true); },
                                     [&](mux::choice_level::account) {
                                       s_->with_chosen_account([&](accounts&, mux::config::account_t& account) {
                                         mux::config::send_typing_in(account) = one.send;
                                       });
                                     },
                                     [&](mux::choice_level::chat) {
                                       const auto chosen = s_->managed();
                                       if (!chosen)
                                         return;
                                       if (one.send)
                                         k_->typing_sent_in.insert_or_assign(*chosen, *one.send);
                                       else
                                         k_->typing_sent_in.erase(*chosen);
                                     }},
                  one.level);
    (void)k_->write();
    s_->refresh_due = true;
  }
  // Who has read up to where, as faces, at a level.
  void apply(const request::set_receipts_shown& one) {
    spl::visit(spl::overloaded{[&](mux::choice_level::everywhere) { k_->history.show_receipts = one.show.value_or(false); },
                               [&](mux::choice_level::account) {
                                 s_->with_chosen_account([&](accounts&, mux::config::account_t& account) {
                                   mux::config::show_receipts_in(account) = one.show;
                                 });
                               },
                               [&](mux::choice_level::chat) {
                                 const auto chosen = s_->managed();
                                 if (!chosen)
                                   return;
                                 if (one.show)
                                   k_->receipts_shown_in.insert_or_assign(*chosen, *one.show);
                                 else
                                   k_->receipts_shown_in.erase(*chosen);
                               }},
               one.level);
    (void)k_->write();
    s_->refresh_due = true;
  }
  // Room events, for the chosen account's chats: shown or not from now on,
  // whatever every account's is.
  void apply(const request::flip_account_room_events&) {
    s_->with_chosen_account([&](accounts& panel, mux::config::account_t& account) {
      auto& kept = mux::config::room_events_in(account);
      kept = !kept.value_or(k_->history.show_room_events);

      (void)k_->write();
      s_->refresh_due = true;
    });
  }
  // Room events, for the chat being read, whatever its account's are.
  void apply(const request::flip_chat_room_events&) {
    const auto chosen = s_->managed();
    if (!chosen)
      return;
    const bool now = k_->room_events_shown(*chosen);
    k_->room_events.insert_or_assign(*chosen, !now);
    (void)k_->write();
    s_->root().show_message("Room events", !now ? "Joins, renames and other room events are shown in this chat."
                                            : "Room events are hidden in this chat.");
    s_->refresh_due = true;
  }
  void apply(const request::choose_account_proxy& one) {
    s_->with_chosen_account([&](accounts& panel, mux::config::account_t& account) {
      auto& kept = mux::config::proxy_in(account);
      if (one.index < 0 || static_cast<std::size_t>(one.index) >= k_->proxies.size())
        kept.reset();
      else
        kept = k_->proxies[static_cast<std::size_t>(one.index)].name;
      (void)k_->write();
      proxying_->reconnect(account);
      panel.show_page(mux::ui::account_page::proxy{}, account, *s_->model, k_->proxies, k_->theme);
    });
  }

 private:
  // Chats in other accounts' lists: placed, taken out, their strips.
  mux::config::chat_placement* placement_of(const mux::conversation_id& chat, const mux::account_id& in) {
    const auto found = std::ranges::find_if(k_->placements, [&](const mux::config::chat_placement& one) {
      return one.account == chat.account.address && one.conversation == chat.id && one.listed_in == in.address;
    });
    return found == k_->placements.end() ? nullptr : &*found;
  }

  services* s_;
  kept_settings* k_;
  proxies_part* proxying_;
};

}  // namespace mux::app
