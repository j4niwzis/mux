// SPDX-License-Identifier: AGPL-3.0-only
// The program: accounts, their pages, privacy and proxies.
module mux.app.program;

import std;
import splice;
import knot;
import skia;
import mux.core;
import mux.logic.links;
import mux.logic.room_events;
import mux.config;
import mux.net;
import mux.xmpp;
import mux.matrix;
import mux.media;
import mux.host;
import mux.ui;
import skiff.paint;
import skiff.scene;
import mux.app.network;
import mux.app.demo;
import mux.app.store;
import mux.app.requests;
import mux.app.words;

namespace mux::app {

// A person's info: a box in the middle of the window, as tdesktop's.
void app::apply(const request::open_member_info& one) {
  const auto chosen = this->managed();
  if (!chosen)
    return;
  const mux::conversation* in = model->find(*chosen);
  root().open_person(chosen->account, one.id, mux::ui::person_of(in, *model, chosen->account, one.id));
}

void app::apply(const request::close_person_info&) { root().close_person(); }

// The input's emoji panel: opened over the chat above its button, or closed.
void app::apply(const request::toggle_emoji&) {
  if (root().emoji_open()) {
    root().close_emoji();
    return;
  }
  emoji_into_ = request::writing::chat{};
  const auto at = root().main().line.parts.input.parts.emoji.bounds();
  this->open_emoji_at(at.fRight, at.fTop);
}
// The thread's: the same panel, over its button, writing in its field.
void app::apply(const request::toggle_thread_emoji&) {
  if (root().emoji_open()) {
    root().close_emoji();
    return;
  }
  emoji_into_ = request::writing::thread{};
  const auto at = root().main().parts.threads.parts.line.parts.input.parts.emoji.bounds();
  this->open_emoji_at(at.fRight, at.fTop);
}
void app::open_emoji_at(float right, float top) {
  const auto chosen = this->managed();
  const mux::conversation* chat = chosen ? model->find(*chosen) : nullptr;
  mux::ui::chat_emotes() = chat ? chat->emotes : std::vector<mux::emote>{};
  mux::ui::chat_stickers() = chat ? chat->stickers : std::vector<mux::emote>{};
  root().open_emoji(right, top - 6.0f);
}
void app::apply(const request::close_emoji&) { root().close_emoji(); }

// A new chat: its box; a direct chat or a group asked of the account whose
// chats are listed -- or, for a direct chat with someone it already has
// one with, that chat shown.
// Element's Start chat: those one has direct chats with to begin with, and
// one's own link to send.
void app::apply(const request::open_new_chat&) {
  const auto& current = root().main().current;
  std::vector<mux::found_person> known;
  std::string link;
  if (current) {
    if (const auto found = model->accounts().find(*current); found != model->accounts().end())
      for (const auto& [key, chat] : found->second.conversations)
        if (!mux::ui::is_group(chat))
          known.push_back({.id = mux::ui::contact_of(chat), .name = mux::ui::display_name(chat), .avatar = chat.avatar});
    link = mux::is_matrix(current->speaks) ? "https://matrix.to/#/" + current->address : "xmpp:" + current->address;
  }
  std::ranges::sort(known, {}, &mux::found_person::name);
  root().open_new_chat(std::move(known), std::move(link));
}
void app::apply(const request::find_people& one) {
  const auto by = this->matrix_account();
  if (!by || shared.demo())
    return;
  net->search_people(*by, one.query);
}
void app::apply(const request::open_new_room&) {
  const auto by = this->matrix_account();
  root().open_new_room(by ? by->address.substr(by->address.find(':') + 1) : std::string());
}
void app::apply(const request::close_new_room&) { root().close_new_room(); }

// A chat's background, at a level: every chat's, the chosen account's (on
// its page), or the chat's own -- as the level over it says, the theme's,
// plain, or a picture chosen, copied into mux's data so that it stays.
void app::apply(const request::open_wallpaper& one) { root().open_wallpaper(one.level); }
void app::apply(const request::close_wallpaper&) { root().close_wallpaper(); }
void app::apply(const request::set_wallpaper& one) {
  const auto set = [&](std::optional<mux::config::wallpaper_t> chosen) {
    splice::visit(splice::overloaded{[&](mux::choice_level::everywhere) { wallpaper = chosen; },
                                     [&](mux::choice_level::account) {
                                       this->with_chosen_account([&](accounts&, mux::config::account_t& account) {
                                         mux::config::wallpaper_in(account) =
                                             chosen ? std::optional<std::string>(mux::config::word_of(*chosen)) : std::nullopt;
                                       });
                                     },
                                     [&](mux::choice_level::chat) {
                                       const auto chosen_chat = this->managed();
                                       if (!chosen_chat)
                                         return;
                                       if (chosen)
                                         wallpaper_in.insert_or_assign(*chosen_chat, *chosen);
                                       else
                                         wallpaper_in.erase(*chosen_chat);
                                     }},
                  one.level);
    (void)this->write();
    root().close_wallpaper();
    this->refresh();
    // What shows the choice, shown again.
    if (auto* up = root().settings_up(); up && up->appearance())
      up->show_appearance(theme, accent);
    if (auto* managing = root().manage_up())
      managing->show_tab(managing->tab);
  };
  splice::visit(splice::overloaded{[&](mux::config::wallpaper_pick::inherit) { set(std::nullopt); },
                                   [&](mux::config::wallpaper_pick::theme) { set(mux::config::wallpaper::theme{}); },
                                   [&](mux::config::wallpaper_pick::plain) { set(mux::config::wallpaper::plain{}); },
                                   [&](mux::config::wallpaper_pick::picture) {
                                     picking_wallpaper = one.level;
                                     mux::host::choose_files();
                                   }},
                one.pick);
}
// Bubbles, at a level: a look, or as the level over it says.
void app::apply(const request::set_bubbles& one) {
  // Where each level keeps the look asked for: the bubbles', or the panels'.
  using look_t = std::optional<mux::config::bubble_look>;
  auto& everywhere = splice::visit(splice::overloaded{[&](mux::config::look_part::bubbles) -> look_t& { return bubbles; },
                                                      [&](mux::config::look_part::panels) -> look_t& { return panels; }},
                                   one.part);
  auto& known = splice::visit(
      splice::overloaded{[](mux::config::look_part::bubbles) -> mux::config::bubble_look& { return mux::ui::bubble_look_everywhere(); },
                         [](mux::config::look_part::panels) -> mux::config::bubble_look& { return mux::ui::panel_look_everywhere(); }},
      one.part);
  auto& per_chat = splice::visit(
      splice::overloaded{[&](mux::config::look_part::bubbles) -> std::map<mux::conversation_id, mux::config::bubble_look>& { return bubbles_in; },
                         [&](mux::config::look_part::panels) -> std::map<mux::conversation_id, mux::config::bubble_look>& { return panels_in; }},
      one.part);
  splice::visit(splice::overloaded{[&](mux::choice_level::everywhere) {
                                     everywhere = one.look;
                                     known = one.look.value_or(mux::config::bubble_look{});
                                   },
                                   [&](mux::choice_level::account) {
                                     this->with_chosen_account([&](accounts&, mux::config::account_t& account) {
                                       auto& kept = splice::visit(
                                           splice::overloaded{[&](mux::config::look_part::bubbles) -> std::optional<std::string>& { return mux::config::bubbles_in(account); },
                                                              [&](mux::config::look_part::panels) -> std::optional<std::string>& { return mux::config::panels_in(account); }},
                                           one.part);
                                       kept = one.look ? std::optional<std::string>(mux::config::word_of(*one.look)) : std::nullopt;
                                     });
                                   },
                                   [&](mux::choice_level::chat) {
                                     const auto chosen = this->managed();
                                     if (!chosen)
                                       return;
                                     if (one.look)
                                       per_chat.insert_or_assign(*chosen, *one.look);
                                     else
                                       per_chat.erase(*chosen);
                                   }},
                one.level);
  (void)this->write();
  this->refresh();
  // Appearance up: its choice marked again.
  if (auto* up = root().settings_up(); up && up->appearance())
    up->show_appearance(theme, accent);
  if (auto* managing = root().manage_up())
    managing->show_tab(managing->tab);
}
// The picture chosen for a background: copied into mux's data, by a name
// its bytes give, and set at the level it was chosen for.
void app::wallpaper_file(const std::string& path) {
  const auto level = *picking_wallpaper;
  std::ifstream in(path, std::ios::binary);
  if (!in)
    return;
  std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  const auto type = mux::media::picture_of(bytes);
  if (!type || !skia::decodeImage(bytes.data(), bytes.size())) {
    root().show_message("Chat background", "That file is not a picture mux can show.");
    return;
  }
  // Kept under its own name -- shown where it is chosen -- in a folder its
  // bytes name: two pictures of one name are two files.
  const auto folder = mux::config::state_path("wallpapers") / std::format("{:016x}", std::hash<std::string>{}(bytes));
  std::error_code failed;
  std::filesystem::create_directories(folder, failed);
  const std::filesystem::path given = std::filesystem::path(path).filename();
  const auto kept = folder / (given.empty() ? std::filesystem::path(std::format("picture.{}", mux::media::extension_of(*type))) : given);
  std::ofstream(kept, std::ios::binary) << bytes;
  const mux::config::wallpaper_t chosen = mux::config::wallpaper::picture{kept.string()};
  splice::visit(splice::overloaded{[&](mux::choice_level::everywhere) { wallpaper = chosen; },
                                   [&](mux::choice_level::account) {
                                     this->with_chosen_account([&](accounts&, mux::config::account_t& account) {
                                       mux::config::wallpaper_in(account) = mux::config::word_of(chosen);
                                     });
                                   },
                                   [&](mux::choice_level::chat) {
                                     // The chat Manage is for: a space, where its settings are open.
                                     if (const auto chat = this->managed())
                                       wallpaper_in.insert_or_assign(*chat, chosen);
                                   }},
                level);
  picking_wallpaper.reset();
  (void)this->write();
  root().close_wallpaper();
  this->refresh();
  // What shows the choice, shown again: the picture chosen in it.
  if (auto* up = root().settings_up(); up && up->appearance())
    up->show_appearance(theme, accent);
  if (auto* managing = root().manage_up())
    managing->show_tab(managing->tab);
  this->with_chosen_account([&](accounts& panel, mux::config::account_t& account) {
    if (panel.chats_page())
      panel.show_page(3, account, *model, proxies, theme);
  });
}

// Threads, as Element's panel: opened in place of the chat's info, the
// room's listed by the server as it opens; one opened, its answers fetched
// (and its root, where it is not held); an answer sent in the one open --
// falling back, for clients without threads, to its latest event.
void app::apply(const request::toggle_threads&) {
  auto& screen = root().main();
  if (screen.toggle_threads() && screen.chosen && !shared.demo())
    net->list_threads(*screen.chosen);
  this->refresh();
}
void app::apply(const request::open_thread& one) {
  auto& screen = root().main();
  if (!screen.chosen)
    return;
  screen.open_thread(one.root);
  if (!shared.demo()) {
    net->load_thread(*screen.chosen, one.root);
    if (const mux::conversation* chat = model->find(*screen.chosen);
        chat && !chat->quoted.contains(one.root) &&
        std::ranges::find(chat->timeline, one.root, &mux::message::id) == chat->timeline.end())
      net->fetch_quoted(*screen.chosen, one.root);
  }
  this->refresh();
}
void app::apply(const request::close_thread&) {
  root().main().close_thread();
  this->refresh();
}
void app::apply(const request::send_in_thread& one) {
  const auto chosen = this->managed();
  const mux::conversation* chat = chosen ? model->find(*chosen) : nullptr;
  if (!chat || shared.demo())
    return;
  std::string latest = one.root;
  if (const auto found = chat->threads.find(one.root); found != chat->threads.end() && !found->second.empty())
    latest = found->second.back().id;
  net->send_in_thread(*chosen, one.text, one.root, latest, one.reply_to);
}

// Emojis & Stickers, as Cinny has them: one's own pack, from Settings; the
// room's, from its settings -- editable where one's power there is what the
// room's state asks.
void app::apply(const request::open_packs&) {
  packs_account = this->matrix_account();
  root().open_packs(std::nullopt, true);
  if (packs_account && !shared.demo())
    net->list_packs(*packs_account, std::nullopt);
}
void app::apply(const request::open_room_packs&) {
  const auto chosen = this->managed();
  const mux::conversation* chat = chosen ? model->find(*chosen) : nullptr;
  if (!chat || !mux::is_matrix(chat->id.account.speaks))
    return;
  packs_account = chat->id.account;
  const auto mine = chat->powers.find(chat->id.account.address);
  const std::int64_t level = mine != chat->powers.end() ? mine->second : chat->power_default;
  const auto asked = chat->needs.events.find("im.ponies.room_emotes");
  const std::int64_t needs = asked != chat->needs.events.end() ? asked->second : chat->needs.state_default;
  root().open_packs(chat->id.id, level >= needs);
  if (!shared.demo())
    net->list_packs(*packs_account, chat->id.id);
}
void app::apply(const request::close_packs&) {
  root().close_packs();
  mux::ui::pack_pictures_shown().clear();
}
void app::apply(const request::save_pack& one) {
  if (packs_account && !shared.demo())
    net->save_pack(*packs_account, one.pack);
}
void app::apply(const request::delete_pack& one) {
  if (packs_account && !shared.demo())
    net->delete_pack(*packs_account, one.room, one.state_key);
}
void app::apply(const request::pick_pack_images&) {
  picking_pack_images = true;
  mux::host::choose_files();
}
// Images chosen for the pack open: each a picture, uploaded -- its name
// its shortcode to begin with, its size and type said in the pack.
void app::pack_files(const std::vector<std::string>& paths) {
  if (!packs_account || shared.demo())
    return;
  for (const std::string& path : paths) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
      continue;
    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto type = mux::media::picture_of(bytes);
    if (!type)
      continue;
    mux::pack_picture one{.shortcode = std::filesystem::path(path).stem().string(),
                          .body = std::filesystem::path(path).filename().string(),
                          .mimetype = std::string(splice::visit([](auto kind) { return mux::media::mimetype_of(kind); }, *type)),
                          .size = static_cast<std::int64_t>(bytes.size())};
    if (auto image = skia::decodeImage(bytes.data(), bytes.size())) {
      one.width = image->width();
      one.height = image->height();
    }
    net->upload_pack_picture(*packs_account, std::move(one), std::move(bytes));
  }
}
void app::apply(const request::copy_text& one) {
  skiff::scene::setClipboardText(one.text);
  root().close_text_menu();
}
void app::apply(const request::close_new_chat&) { root().close_new_chat(); }
void app::apply(const request::start_direct& one) {
  const auto& current = root().main().current;
  if (!current || shared.demo())
    return;
  root().close_new_chat();
  for (const auto& [key, chat] : model->accounts().at(*current).conversations)
    if (!mux::ui::is_group(chat) && mux::ui::contact_of(chat) == one.user) {
      this->open_chat(chat.id, std::nullopt);
      return;
    }
  net->create_direct(*current, one.user);
  root().show_message("New chat", "Starting a chat with " + one.user + "…");
}
std::optional<mux::account_id> app::matrix_account() {
  if (const auto& current = root().main().current; current && mux::is_matrix(current->speaks))
    return current;
  for (const auto& [id, account] : model->accounts())
    if (mux::is_matrix(id.speaks))
      return id;
  return std::nullopt;
}
// Explore rooms: opened on the account's own server.
void app::apply(const request::open_explore&) {
  const auto by = this->matrix_account();
  const std::string own = by ? by->address.substr(by->address.find(':') + 1) : std::string();
  root().open_explore(own);
  // What the server lists, at once, as Cinny opens its explorer: its
  // directory with nothing searched.
  if (by && !shared.demo()) {
    root().explore_loading();
    net->search_directory(*by, own, std::string());
  }
}
void app::apply(const request::close_explore&) { root().close_explore(); }
// A space's rooms and spaces, in Explore: asked of its account.
void app::apply(const request::explore_space& one) {
  (void)root().main().close_space_menu();
  const auto by = root().main().current;
  if (!by || shared.demo())
    return;
  root().open_explore(by->address.substr(by->address.find(':') + 1));
  // Said as the space's: its name and picture over what it holds.
  std::string name = one.name;
  if (const auto& chats = model->accounts().at(*by).conversations; chats.contains(one.room))
    name = mux::ui::display_name(chats.at(one.room));
  root().explore_as_space(one.room, name.empty() ? one.room : name);
  root().explore_loading();
  net->explore_space(*by, one.room);
}
// A search: an address typed in is gone to, as a link to it would be --
// its card, or the room where joined; else the directory asked.
void app::apply(const request::search_rooms& one) {
  if (auto link = mux::logic::matrix_id_of(one.query)) {
    root().close_explore();
    this->follow(*link);
    return;
  }
  const auto by = this->matrix_account();
  if (!by || shared.demo())
    return;
  net->search_directory(*by, one.server, one.query);
}
// A room of the directory joined, through the server it was listed by, and
// opened when it comes.
void app::apply(const request::join_directory_room& one) {
  const auto by = this->matrix_account();
  if (!by || shared.demo())
    return;
  std::vector<std::string> via;
  if (!one.server.empty())
    via.push_back(one.server);
  joining = mux::logic::link::room{one.room, std::nullopt, via};
  net->join(*by, one.room, via);
  root().close_explore();
}
// A room made, and opened once the model has it.
void app::apply(const request::create_room& one) {
  const auto by = this->matrix_account();
  if (!by || shared.demo())
    return;
  root().close_new_room();
  std::string alias = one.alias;
  if (alias.starts_with('#'))
    alias = alias.substr(1, alias.find(':') == std::string::npos ? std::string::npos : alias.find(':') - 1);
  net->create_room(*by, one.name, one.topic, one.open, one.open ? alias : std::string(), one.federate);
  root().show_message("New room", "Making " + one.name + "\u2026");
}
void app::apply(const request::start_group& one) {
  const auto& current = root().main().current;
  if (!current || shared.demo())
    return;
  root().close_new_chat();
  net->create_group(*current, one.name);
  root().show_message("New group", "Making " + one.name + "…");
}

// The room's management: made from what the model knows of it now.
void app::apply(const request::open_manage&) {
  (void)root().main().close_space_menu();  // the chat menu its Settings came from
  manage_target.reset();
  if (const auto chosen = root().main().chosen)
    this->manage_chat(*chosen);
}
// A space's settings: Manage, for it.
void app::apply(const request::manage_space& one) {
  (void)root().main().close_space_menu();
  const auto by = root().main().current;
  if (!by)
    return;
  manage_target = mux::conversation_id{*by, one.room};
  this->manage_chat(*manage_target);
}
// A space shown as one chat, its rooms as topics -- or as a space. Not one
// that holds spaces: it is a space.
void app::apply(const request::flip_forum& one) {
  const auto by = root().main().current;
  if (!by)
    return;
  const mux::conversation_id id{*by, one.room};
  const mux::conversation* space = model->find(id);
  if (!space || !space->space)
    return;
  const bool holds_spaces = std::ranges::any_of(space->children, [&](const std::string& child) {
    const mux::conversation* in = model->find(mux::conversation_id{*by, child});
    return in && in->space;
  });
  if (forums.contains(id))
    forums.erase(id);
  else if (!holds_spaces)
    forums.insert(id);
  (void)this->write();
  this->refresh();
  if (auto* managing = root().manage_up())
    managing->show_tab(managing->tab);
}
// A space's rooms in Home, or not: its own choice.
void app::apply(const request::flip_home_hide& one) {
  const auto by = root().main().current;
  if (!by)
    return;
  const mux::conversation_id id{*by, one.room};
  const mux::conversation* space = model->find(id);
  if (!space || !space->space)
    return;
  if (!hidden_from_home.erase(id))
    hidden_from_home.insert(id);
  (void)this->write();
  this->refresh();
  if (auto* managing = root().manage_up())
    managing->show_tab(managing->tab);
}
void app::apply(const request::close_forum&) { root().main().close_forum(); }
void app::apply(const request::manage_forum&) {
  if (const auto& open = root().main().forum_open)
    this->apply(request::manage_space{*open});
}
void app::manage_chat(const mux::conversation_id& id) {
  const mux::conversation* chat = model->find(id);
  if (!chat)
    return;
  const auto level_of = [&](const std::string& user) {
    const auto found = chat->powers.find(user);
    return found == chat->powers.end() ? chat->power_default : found->second;
  };
  mux::ui::room_settings_facts facts{.id = chat->id.id,
                                     .name = chat->name,
                                     .topic = chat->topic.value_or(""),
                                     .alias = chat->alias,
                                     .other_aliases = chat->other_aliases,
                                     .encrypted = chat->encrypted,
                                     .join_rule = chat->join_rule,
                                     .history = chat->history,
                                     .version = chat->version,
                                     .notify_mode = this->notify_mode_of(chat->id),
                                     .events_all = room_events.contains(chat->id)
                                                       ? std::optional<bool>(room_events.at(chat->id))
                                                       : std::nullopt,
                                     .typing = typing_sent_in.contains(chat->id) ? std::optional<bool>(typing_sent_in.at(chat->id))
                                                                                 : std::nullopt,
                                     .previews = previews_shown_in.contains(chat->id)
                                                     ? std::optional<bool>(previews_shown_in.at(chat->id))
                                                     : std::nullopt,
                                     .previews_direct = previews_direct_in.contains(chat->id)
                                                            ? std::optional<bool>(previews_direct_in.at(chat->id))
                                                            : std::nullopt,
                                     .receipts = receipts_shown_in.contains(chat->id)
                                                     ? std::optional<bool>(receipts_shown_in.at(chat->id))
                                                     : std::nullopt,
                                     .jump_search = jump_search_in.contains(chat->id)
                                                        ? std::optional<std::int64_t>(jump_search_in.at(chat->id))
                                                        : std::nullopt,
                                     .event_kinds = room_event_kinds.contains(chat->id)
                                                        ? std::optional<mux::config::room_event_kinds>(room_event_kinds.at(chat->id))
                                                        : std::nullopt,
                                     .mine = level_of(chat->id.account.address),
                                     .needs = chat->needs,
                                     .space = chat->space,
                                     .holds_spaces = std::ranges::any_of(chat->children, [&](const std::string& child) {
                                       const mux::conversation* in = model->find(mux::conversation_id{chat->id.account, child});
                                       return in && in->space;
                                     }),
                                     .forum = forums.contains(chat->id),
                                     .hidden_from_home = hidden_from_home.contains(chat->id)};
  // Element's privileged users: those the power levels name with a level of
  // their own, the highest first.
  for (const auto& [user, level] : chat->powers) {
    if (level == chat->needs.users_default)
      continue;
    const auto member = std::ranges::find(chat->members, user, &mux::member::id);
    facts.privileged.push_back(
        {user, member != chat->members.end() && !member->name.empty() ? member->name : user, level});
  }
  std::ranges::stable_sort(facts.privileged, std::greater{}, &mux::ui::room_settings_facts::person::level);
  root().open_manage(facts);
}
void app::apply(const request::close_manage&) {
  manage_target.reset();
  root().close_manage();
}
// The developer tools, for the chat being read.
void app::apply(const request::explore_state&) {
  const auto chosen = this->managed();
  if (!chosen || shared.demo())
    return;
  root().close_manage();
  net->list_state(*chosen);
}
void app::apply(const request::open_send_custom&) {
  root().close_manage();
  root().open_send_custom();
}
void app::apply(const request::close_devtools&) { root().close_devtools(); }
void app::apply(const request::send_custom& one) {
  const auto chosen = this->managed();
  if (!chosen || shared.demo())
    return;
  net->send_custom(*chosen, one.type, one.state_key, one.json);
}
// Done to the room being read, by its account.
void app::apply(const request::room_act& one) {
  const auto chosen = this->managed();
  if (!chosen || shared.demo())
    return;
  net->manage(*chosen, one.action);
}
// An emoji picked: into what is written, where the caret is; the input keeps
// the keys.
void app::apply(const request::insert_emoji& one) {
  auto& screen = root().main();
  // A custom emoji: its picture in the line, as the message will show it,
  // sent as its shortcode.
  const auto put = [&](auto& field) {
    if (one.picture.empty())
      field.insertText(one.text);
    else
      field.insertAtom("\u2003", one.picture, one.text, true);
    scene.focus(field);
  };
  splice::visit(splice::overloaded{[&](request::writing::chat) { put(screen.line.field); },
                                   [&](request::writing::thread) { put(screen.parts.threads.parts.line.parts.input.parts.field); }},
                emoji_into_);
}

void app::apply(const request::not_implemented& one) { root().show_notice(one.what); }

void app::apply(const request::close_notice&) { root().close_notice(); }

void app::apply(const request::resize_info& one) { root().main().resize_info(one.x); }

void app::apply(const request::choose_new_proxy& one) {
  if (auto* up = root().open_panel())
    splice::visit(
        [&](accounts& panel) {
          if (auto* pane = panel.adding())
            pane->set_proxy(one.index);
        },
        *up);
}

void app::apply(const request::toggle_mute&) {
  auto& screen = root().main();
  if (!screen.chosen)
    return;
  if (!muted.erase(*screen.chosen))
    muted.insert(*screen.chosen);
  (void)this->write();
  this->refresh();
}

void app::apply(const request::close_account_pages&) {
  if (auto* up = root().open_panel())
    splice::visit([](accounts& panel) { panel.close_pages(); }, *up);
}

void app::apply(const request::accounts_back&) {
  auto* up = root().open_panel();
  if (!up)
    return;
  splice::visit(
      [this](accounts& panel) {
        if (panel.pages_open())
          panel.close_pages();
        else
          this->apply(request::pop_panel{});
      },
      *up);
}

void app::apply(const request::account_page& one) {
  this->with_chosen_account([&](accounts& panel, mux::config::account_t& account) {
    panel.show_page(one.page, account, *model, proxies, theme);
    // Sessions: asked of the server as the page opens.
    if (one.page == 4)
      net->list_sessions(id_of(account));
  });
}
void app::apply(const request::sign_out_sessions& one) {
  this->with_chosen_account([&](accounts&, mux::config::account_t& account) {
    net->sign_out_sessions(id_of(account), one.devices, one.password);
  });
}
void app::apply(const request::rename_session& one) {
  this->with_chosen_account([&](accounts&, mux::config::account_t& account) {
    net->rename_session(id_of(account), one.device, one.name);
  });
}
void app::apply(const request::refresh_sessions&) {
  this->with_chosen_account([&](accounts&, mux::config::account_t& account) { net->list_sessions(id_of(account)); });
}

void app::apply(const request::flip_account_receipts&) {
  this->with_chosen_account([&](accounts& panel, mux::config::account_t& account) {
    auto& kept = mux::config::read_receipts_in(account);
    kept = !kept.value_or(true);
    if (auto* page = panel.privacy())
      page->show(*kept);
    (void)this->write();
  });
}

// An account's colour chosen, and its strip on its chats in other lists:
// kept, and the lists shown again.
void app::apply(const request::set_account_colour& one) {
  this->with_chosen_account([&](accounts& panel, mux::config::account_t& account) {
    mux::config::colour_in(account) = std::string(splice::visit([](const auto& each) { return mux::config::word_of(each); }, one.colour));
    if (auto* page = panel.chats_page())
      page->show_colour(mux::config::colour_of(account), mux::config::strip_of(account));
    (void)this->write();
  });
  this->refresh();
}
void app::apply(const request::flip_account_strip&) {
  this->with_chosen_account([&](accounts& panel, mux::config::account_t& account) {
    auto& kept = mux::config::strip_in(account);
    kept = !kept.value_or(true);
    if (auto* page = panel.chats_page())
      page->show_colour(mux::config::colour_of(account), *kept);
    (void)this->write();
  });
  this->refresh();
}

// A tombstoned room's way on: the room it was upgraded to, as a matrix.to
// link to it opens -- the chat where it is joined, its card where not.
void app::apply(const request::open_replacement&) {
  const auto& chosen = root().main().chosen;
  if (const mux::conversation* chat = chosen ? model->find(*chosen) : nullptr; chat && chat->replaced_by)
    this->apply(request::open_url{"https://matrix.to/#/" + *chat->replaced_by});
}

// Chats in other accounts' lists: placed, taken out, their strips.
mux::config::chat_placement* app::placement_of(const mux::conversation_id& chat, const mux::account_id& in) {
  const auto found = std::ranges::find_if(placements, [&](const mux::config::chat_placement& one) {
    return one.account == chat.account.address && one.conversation == chat.id && one.listed_in == in.address;
  });
  return found == placements.end() ? nullptr : &*found;
}
void app::apply(const request::place_chat& one) {
  (void)root().main().close_space_menu();
  if (one.to == one.chat.account)
    return;
  // Moved: out of every other list it was moved to, into this one.
  if (one.moved)
    std::erase_if(placements, [&](const mux::config::chat_placement& each) {
      return each.account == one.chat.account.address && each.conversation == one.chat.id && each.moved;
    });
  if (auto* kept = this->placement_of(one.chat, one.to))
    kept->moved = one.moved;
  else
    placements.push_back({.account = one.chat.account.address, .conversation = one.chat.id, .listed_in = one.to.address,
                          .moved = one.moved});
  (void)this->write();
  this->refresh();
}
void app::apply(const request::unplace_chat& one) {
  (void)root().main().close_space_menu();
  std::erase_if(placements, [&](const mux::config::chat_placement& each) {
    return each.account == one.chat.account.address && each.conversation == one.chat.id && each.listed_in == one.from.address;
  });
  (void)this->write();
  this->refresh();
}
void app::apply(const request::flip_chat_strip& one) {
  (void)root().main().close_space_menu();
  if (auto* kept = this->placement_of(one.chat, one.in)) {
    const auto own = this->find(one.chat.account.address);
    const bool now = kept->strip.value_or(own == saved.end() || mux::config::strip_of(*own));
    kept->strip = !now;
    (void)this->write();
  }
  this->refresh();
}
void app::apply(const request::set_chat_strip_colour& one) {
  (void)root().main().close_space_menu();
  if (auto* kept = this->placement_of(one.chat, one.in)) {
    kept->strip_colour = std::string(splice::visit([](const auto& each) { return mux::config::word_of(each); }, one.colour));
    kept->strip = true;
    (void)this->write();
  }
  this->refresh();
}

// Notifications: the page, its switches, what shows them; an account's and
// a chat's own.
void app::apply(const request::settings_notifications&) {
  if (auto* up = root().settings_up())
    up->show_notifications(notifications);
}
void app::apply(const request::flip_notify& one) {
  bool& flag = mux::config::flag_in(notifications, one.flag);
  flag = !flag;
  (void)this->write();
  if (auto* up = root().settings_up())
    if (auto* page = up->notifications())
      page->show(notifications);
}
void app::apply(const request::set_notify_backend& one) {
  notifications.backend = mux::config::word_of(one.backend);
  (void)this->write();
  if (auto* up = root().settings_up())
    if (auto* page = up->notifications())
      page->show(notifications);
}
void app::apply(const request::flip_account_notify&) {
  this->with_chosen_account([&](accounts&, mux::config::account_t& account) {
    auto& kept = mux::config::notify_in(account);
    kept = !kept.value_or(notifications.desktop);
    (void)this->write();
  });
}
void app::apply(const request::flip_account_notify_sound&) {
  this->with_chosen_account([&](accounts&, mux::config::account_t& account) {
    auto& kept = mux::config::notify_sound_in(account);
    kept = !kept.value_or(notifications.sound);
    (void)this->write();
  });
}
void app::apply(const request::set_chat_notify& one) {
  const auto chosen = this->managed();
  if (!chosen)
    return;
  notify_modes.erase(*chosen);
  muted.erase(*chosen);
  splice::visit(splice::overloaded{[&](mux::config::notify_mode::off) { muted.insert(*chosen); },
                             [&](mux::config::notify_mode::by_default) {},
                             [&](const auto& own) { notify_modes.insert_or_assign(*chosen, own); }},
             one.mode);
  (void)this->write();
  this->refresh();
}

// Which room events show, as chosen at a level: all of them, or one kind --
// none said, as the level under says.
void app::apply(const request::set_room_event_kind& one) {
  const auto set_kind = [&](std::optional<mux::config::room_event_kinds>& kinds) {
    if (!kinds)
      kinds.emplace();
    mux::logic::choice_in(*kinds, *one.kind) = one.show;
  };
  splice::visit(splice::overloaded{[&](mux::choice_level::everywhere) {
                               if (one.kind)
                                 set_kind(history.room_event_kinds);
                               else
                                 history.show_room_events = one.show.value_or(true);
                             },
                             [&](mux::choice_level::account) {
                               this->with_chosen_account([&](accounts&, mux::config::account_t& account) {
                                 if (one.kind)
                                   set_kind(mux::config::room_event_kinds_in(account));
                                 else
                                   mux::config::room_events_in(account) = one.show;
                               });
                             },
                             [&](mux::choice_level::chat) {
                               const auto chosen = this->managed();
                               if (!chosen)
                                 return;
                               if (one.kind)
                                 mux::logic::choice_in(room_event_kinds[*chosen], *one.kind) = one.show;
                               else if (one.show)
                                 room_events.insert_or_assign(*chosen, *one.show);
                               else
                                 room_events.erase(*chosen);
                             }},
             one.level);
  (void)this->write();
  this->refresh();
}

// A bar's order, as a drag left it: its items put there in that order --
// the one moved taken out of the bar it came from, and from hidden.
void app::apply(const request::place_spaces& one) {
  const auto mine = [&](const mux::config::space_placed& p) { return p.account == one.account; };
  std::erase_if(space_places, [&](const mux::config::space_placed& p) {
    return mine(p) && (p.bar == one.bar || (one.moved && p.item == *one.moved && (p.bar == mux::config::space_bar_t{mux::config::space_bar::hidden{}} ||
                                                                              (one.from && p.bar == *one.from))));
  });
  // Where the item came from the side bar by default -- put nowhere -- the
  // rest of the side bar is put too, so it stays as it was.
  std::ranges::copy(one.order | std::views::transform([&](const mux::config::space_item_t& item) {
                      return mux::config::space_placed{one.account, item, one.bar};
                    }),
                    std::back_inserter(space_places));
  (void)this->write();
  this->refresh();
}
// Home without what spaces hold, at a level.
void app::apply(const request::set_home_hides& one) {
  splice::visit(splice::overloaded{[&](mux::choice_level::everywhere) {
                                     home_hides_spaced = one.on.value_or(false);
                                     mux::ui::window_look().home_hides = home_hides_spaced;
                                   },
                                   [&](mux::choice_level::account) {
                                     this->with_chosen_account([&](accounts&, mux::config::account_t& account) {
                                       mux::config::home_hides_in(account) = one.on;
                                     });
                                   },
                                   [](mux::choice_level::chat) {}},
                one.level);
  (void)this->write();
  this->refresh();
  if (auto* up = root().settings_up(); up && up->appearance())
    up->show_appearance(theme, accent);
}
void app::apply(const request::set_home_direct& one) {
  splice::visit(splice::overloaded{[&](mux::choice_level::everywhere) {
                                     home_hides_direct = one.on.value_or(false);
                                     mux::ui::window_look().home_direct = home_hides_direct;
                                   },
                                   [&](mux::choice_level::account) {
                                     this->with_chosen_account([&](accounts&, mux::config::account_t& account) {
                                       mux::config::home_direct_in(account) = one.on;
                                     });
                                   },
                                   [](mux::choice_level::chat) {}},
                one.level);
  (void)this->write();
  this->refresh();
  if (auto* up = root().settings_up(); up && up->appearance())
    up->show_appearance(theme, accent);
}
// An item's bars, as chosen: the side, the top, both, or none -- hidden.
void app::apply(const request::set_space_bars& one) {
  (void)root().main().close_space_menu();
  std::erase_if(space_places, [&](const mux::config::space_placed& p) { return p.account == one.account && p.item == one.item; });
  if (one.side)
    space_places.push_back({one.account, one.item, mux::config::space_bar::side{}});
  if (one.top)
    space_places.push_back({one.account, one.item, mux::config::space_bar::top{}});
  if (!one.side && !one.top)
    space_places.push_back({one.account, one.item, mux::config::space_bar::hidden{}});
  (void)this->write();
  this->refresh();
  if (auto* up = root().settings_up(); up && up->appearance())
    up->show_appearance(theme, accent);
}

// How a level shows room events, as a whole: what it holds replaced.
void app::apply(const request::set_room_events& one) {
  splice::visit(splice::overloaded{[&](mux::choice_level::everywhere) {
                                     history.show_room_events = one.all.value_or(true);
                                     history.room_event_kinds = one.kinds;
                                   },
                                   [&](mux::choice_level::account) {
                                     this->with_chosen_account([&](accounts&, mux::config::account_t& account) {
                                       mux::config::room_events_in(account) = one.all;
                                       mux::config::room_event_kinds_in(account) = one.kinds;
                                     });
                                   },
                                   [&](mux::choice_level::chat) {
                                     const auto chosen = this->managed();
                                     if (!chosen)
                                       return;
                                     if (one.all)
                                       room_events.insert_or_assign(*chosen, *one.all);
                                     else
                                       room_events.erase(*chosen);
                                     if (one.kinds)
                                       room_event_kinds.insert_or_assign(*chosen, *one.kinds);
                                     else
                                       room_event_kinds.erase(*chosen);
                                   }},
                one.level);
  (void)this->write();
  this->refresh();
}

// How far a jump's search pages back, at a level.
void app::apply(const request::set_jump_search& one) {
  splice::visit(splice::overloaded{[&](mux::choice_level::everywhere) { history.jump_search = one.most.value_or(5000); },
                             [&](mux::choice_level::account) {
                               this->with_chosen_account([&](accounts&, mux::config::account_t& account) {
                                 mux::config::jump_search_in(account) = one.most;
                               });
                             },
                             [&](mux::choice_level::chat) {
                               const auto chosen = this->managed();
                               if (!chosen)
                                 return;
                               if (one.most)
                                 jump_search_in.insert_or_assign(*chosen, *one.most);
                               else
                                 jump_search_in.erase(*chosen);
                             }},
             one.level);
  (void)this->write();
  this->refresh();
}

// Link previews, at a level.
void app::apply(const request::set_link_previews& one) {
  splice::visit(splice::overloaded{[&](mux::choice_level::everywhere) { history.link_previews = one.show.value_or(true); },
                             [&](mux::choice_level::account) {
                               this->with_chosen_account([&](accounts&, mux::config::account_t& account) {
                                 mux::config::link_previews_in(account) = one.show;
                               });
                             },
                             [&](mux::choice_level::chat) {
                               const auto chosen = this->managed();
                               if (!chosen)
                                 return;
                               if (one.show)
                                 previews_shown_in.insert_or_assign(*chosen, *one.show);
                               else
                                 previews_shown_in.erase(*chosen);
                             }},
             one.level);
  (void)this->write();
  this->refresh();
}

// Where link previews come from, at a level.
void app::apply(const request::set_previews_direct& one) {
  splice::visit(splice::overloaded{[&](mux::choice_level::everywhere) { history.previews_direct = one.direct.value_or(false); },
                                   [&](mux::choice_level::account) {
                                     this->with_chosen_account([&](accounts&, mux::config::account_t& account) {
                                       mux::config::previews_direct_in(account) = one.direct;
                                     });
                                   },
                                   [&](mux::choice_level::chat) {
                                     const auto chosen = this->managed();
                                     if (!chosen)
                                       return;
                                     if (one.direct)
                                       previews_direct_in.insert_or_assign(*chosen, *one.direct);
                                     else
                                       previews_direct_in.erase(*chosen);
                                   }},
                one.level);
  (void)this->write();
  this->refresh();
}

// Whether others are told one is typing, at a level.
void app::apply(const request::set_typing_sent& one) {
  splice::visit(splice::overloaded{[&](mux::choice_level::everywhere) { history.send_typing = one.send.value_or(true); },
                                   [&](mux::choice_level::account) {
                                     this->with_chosen_account([&](accounts&, mux::config::account_t& account) {
                                       mux::config::send_typing_in(account) = one.send;
                                     });
                                   },
                                   [&](mux::choice_level::chat) {
                                     const auto chosen = this->managed();
                                     if (!chosen)
                                       return;
                                     if (one.send)
                                       typing_sent_in.insert_or_assign(*chosen, *one.send);
                                     else
                                       typing_sent_in.erase(*chosen);
                                   }},
                one.level);
  (void)this->write();
  this->refresh();
}

// Who has read up to where, as faces, at a level.
void app::apply(const request::set_receipts_shown& one) {
  splice::visit(splice::overloaded{[&](mux::choice_level::everywhere) { history.show_receipts = one.show.value_or(false); },
                             [&](mux::choice_level::account) {
                               this->with_chosen_account([&](accounts&, mux::config::account_t& account) {
                                 mux::config::show_receipts_in(account) = one.show;
                               });
                             },
                             [&](mux::choice_level::chat) {
                               const auto chosen = this->managed();
                               if (!chosen)
                                 return;
                               if (one.show)
                                 receipts_shown_in.insert_or_assign(*chosen, *one.show);
                               else
                                 receipts_shown_in.erase(*chosen);
                             }},
             one.level);
  (void)this->write();
  this->refresh();
}

// Room events, for the chosen account's chats: shown or not from now on,
// whatever every account's is.
void app::apply(const request::flip_account_room_events&) {
  this->with_chosen_account([&](accounts& panel, mux::config::account_t& account) {
    auto& kept = mux::config::room_events_in(account);
    kept = !kept.value_or(history.show_room_events);

    (void)this->write();
    this->refresh();
  });
}
// Room events, for the chat being read, whatever its account's are.
void app::apply(const request::flip_chat_room_events&) {
  const auto chosen = this->managed();
  if (!chosen)
    return;
  const bool now = this->room_events_shown(*chosen);
  room_events.insert_or_assign(*chosen, !now);
  (void)this->write();
  root().show_message("Room events", !now ? "Joins, renames and other room events are shown in this chat."
                                          : "Room events are hidden in this chat.");
  this->refresh();
}

void app::apply(const request::proxy_kind& one) {
  if (auto* up = root().settings_up())
    if (auto* editor = up->editor())
      editor->set_kind(one.kind);
}

void app::apply(const request::choose_account_proxy& one) {
  this->with_chosen_account([&](accounts& panel, mux::config::account_t& account) {
    auto& kept = mux::config::proxy_in(account);
    if (one.index < 0 || static_cast<std::size_t>(one.index) >= proxies.size())
      kept.reset();
    else
      kept = proxies[static_cast<std::size_t>(one.index)].name;
    (void)this->write();
    this->reconnect(account);
    panel.show_page(2, account, *model, proxies, theme);
  });
}

void app::reconnect(const mux::config::account_t& account) {
  if (!mux::config::enabled_of(account) || ask.demo)
    return;
  net->remove(mux::config::address_of(account));
  net->add(account, proxies);
}

void app::reconnect_through(const std::string& name) {
  for (const auto& one : saved)
    if (mux::config::proxy_of(one) == name)
      this->reconnect(one);
}

}  // namespace mux::app
