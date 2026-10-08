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
    k_->flip<&mux::app::chat_choices::muted>(*screen.chosen);
    (void)k_->write();
    s_->refresh_due = true;
  }
  void apply(const request::toggle_mute_of& one) {
    k_->flip<&mux::app::chat_choices::muted>(one.which);
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
      mux::config::colour_in(account) = mux::config::said_of<mux::config::accent_said_t>(one.colour);
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
    s_->root().main().close_space_menu_soon();
    if (one.to == one.chat.account)
      return;
    // Moved: out of every other list it was moved to, into this one.
    k_->change_part<std::vector<mux::config::chat_placement>>([&](auto& all) {
      if (one.moved)
        std::erase_if(all, [&](const mux::config::chat_placement& each) {
          return each.account == one.chat.account.address && each.conversation == one.chat.id && each.moved;
        });
      if (auto* kept = placement_in(all, one.chat, one.to))
        kept->moved = one.moved;
      else
        all.push_back({.account = one.chat.account.address, .conversation = one.chat.id, .listed_in = one.to.address,
                       .moved = one.moved});
    });
    (void)k_->write();
    s_->refresh_due = true;
  }
  void apply(const request::unplace_chat& one) {
    s_->root().main().close_space_menu_soon();
    k_->change_part<std::vector<mux::config::chat_placement>>([&](auto& all) {
      std::erase_if(all, [&](const mux::config::chat_placement& each) {
        return each.account == one.chat.account.address && each.conversation == one.chat.id && each.listed_in == one.from.address;
      });
    });
    (void)k_->write();
    s_->refresh_due = true;
  }
  void apply(const request::flip_chat_strip& one) {
    s_->root().main().close_space_menu_soon();
    const auto* own = k_->settings_of(one.chat.account.address);
    k_->change_part<std::vector<mux::config::chat_placement>>([&](auto& all) {
      if (auto* kept = placement_in(all, one.chat, one.in))
        kept->strip = !kept->strip.value_or(own == nullptr || mux::config::strip_of(*own));
    });
    s_->refresh_due = true;
  }
  void apply(const request::set_chat_strip_colour& one) {
    s_->root().main().close_space_menu_soon();
    k_->change_part<std::vector<mux::config::chat_placement>>([&](auto& all) {
      if (auto* kept = placement_in(all, one.chat, one.in)) {
        kept->strip_colour = mux::config::said_of<mux::config::accent_said_t>(one.colour);
        kept->strip = true;
      }
    });
    s_->refresh_due = true;
  }

  // A bar's order, as a drag left it: its items put there in that order --
  // the one moved taken out of the bar it came from, and from hidden.
  void apply(const request::place_spaces& one) {
    const auto mine = [&](const mux::config::space_placed& p) { return p.account == one.account; };
    auto places = k_->appearance().space_places;
    std::erase_if(places, [&](const mux::config::space_placed& p) {
      return mine(p) && (p.bar == one.bar || (one.moved && p.item == *one.moved && (p.bar == mux::config::space_bar_t{mux::config::space_bar::hidden{}} ||
                                                                                (one.from && p.bar == *one.from))));
    });
    // Where the item came from the side bar by default -- put nowhere -- the
    // rest of the side bar is put too, so it stays as it was.
    std::ranges::copy(std::views::transform(one.order, [&](const mux::config::space_item_t& item) {
                        return mux::config::space_placed{one.account, item, one.bar};
                      }),
                      std::back_inserter(places));
    k_->choose_field<&mux::config::look_settings::space_places>(std::move(places));
    (void)k_->write();
    s_->refresh_due = true;
  }
  // Home without what spaces hold, at a level.
  void apply(const request::set_home_hides& one) {
    spl::visit(spl::overloaded{[&](mux::choice_level::everywhere) {
                                       k_->choose_field<&mux::config::look_settings::home_hides_spaced>(one.on.value_or(false));
                                       s_->looks.window.home_hides = k_->appearance().home_hides_spaced;
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
      up->show_appearance(k_->appearance().theme, k_->appearance().accent);
  }
  void apply(const request::set_home_direct& one) {
    spl::visit(spl::overloaded{[&](mux::choice_level::everywhere) {
                                       k_->choose_field<&mux::config::look_settings::home_hides_direct>(one.on.value_or(false));
                                       s_->looks.window.home_direct = k_->appearance().home_hides_direct;
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
      up->show_appearance(k_->appearance().theme, k_->appearance().accent);
  }
  // An item's bars, as chosen: the side, the top, both, or none -- hidden.
  void apply(const request::set_space_bars& one) {
    s_->root().main().close_space_menu_soon();
    auto places = k_->appearance().space_places;
    std::erase_if(places, [&](const mux::config::space_placed& p) { return p.account == one.account && p.item == one.item; });
    if (one.side)
      places.push_back({one.account, one.item, mux::config::space_bar::side{}});
    if (one.top)
      places.push_back({one.account, one.item, mux::config::space_bar::top{}});
    if (!one.side && !one.top)
      places.push_back({one.account, one.item, mux::config::space_bar::hidden{}});
    k_->choose_field<&mux::config::look_settings::space_places>(std::move(places));
    (void)k_->write();
    s_->refresh_due = true;
    if (auto* up = s_->root().settings_up(); up && up->appearance())
      up->show_appearance(k_->appearance().theme, k_->appearance().accent);
  }
  // Room events, for the chosen account's chats: shown or not from now on,
  // whatever every account's is.
  void apply(const request::flip_account_room_events&) {
    s_->with_chosen_account([&](accounts& panel, mux::config::account_t& account) {
      auto& kept = mux::config::room_events_in(account);
      kept = !kept.value_or(k_->history().show_room_events);

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
    k_->choose<&mux::app::chat_choices::room_events>(*chosen, !now);
    (void)k_->write();
    s_->notice("Room events", !now ? "Joins, renames and other room events are shown in this chat."
                                            : "Room events are hidden in this chat.");
    s_->refresh_due = true;
  }
  void apply(const request::choose_account_proxy& one) {
    s_->with_chosen_account([&](accounts& panel, mux::config::account_t& account) {
      auto& kept = mux::config::proxy_in(account);
      if (one.index < 0 || static_cast<std::size_t>(one.index) >= k_->proxies().size())
        kept.reset();
      else
        kept = k_->proxies()[static_cast<std::size_t>(one.index)].name;
      (void)k_->write();
      proxying_->reconnect(account);
      panel.show_page(mux::ui::account_page::proxy{}, account, *s_->model, k_->proxies(), k_->appearance().theme);
    });
  }

 private:
  // Chats in other accounts' lists: placed, taken out, their strips.
  static mux::config::chat_placement* placement_in(std::vector<mux::config::chat_placement>& all, const mux::conversation_id& chat, const mux::account_id& in) {
    const auto found = std::ranges::find_if(all, [&](const mux::config::chat_placement& one) {
      return one.account == chat.account.address && one.conversation == chat.id && one.listed_in == in.address;
    });
    return found == all.end() ? nullptr : &*found;
  }

  services* s_;
  kept_settings* k_;
  proxies_part* proxying_;
};

}  // namespace mux::app
