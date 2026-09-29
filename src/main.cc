// SPDX-License-Identifier: AGPL-3.0-only
// mux: the program. The network half runs on a thread of its own -- every
// account a fiber on one loop -- and the window on the main thread; what the
// accounts say crosses to the window in a mailbox, and what the window asks
// for is posted back to the loop.
//
//   mux                   the accounts saved in ~/.config/mux/accounts.json
//   mux <account>...      and these too, for this run, with the password in
//                         MUX_PASSWORD: user@domain for XMPP, @user:server for
//                         Matrix
//   mux --demo            fake accounts and conversations: no network, and
//                         nothing kept
//
// With no accounts at all it opens all the same, and says how to add one.
import std;
import knot;
import skia;
import mux.core;
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

namespace {

using namespace mux::app;

// What the program does to the window between events.
struct app {
  using adding = mux::ui::add_account_pane<actions>;
  using accounts = mux::ui::accounts_panel<actions>;
  using xmpp_form = mux::ui::xmpp_form<actions>;
  using matrix_form = mux::ui::matrix_form<actions>;

  mailbox_type* box = nullptr;
  mux::model* model = nullptr;
  network* net = nullptr;
  std::filesystem::path config_path;
  std::vector<mux::config::account_t> saved;
  // How much moves, as read, to be written back as it was.
  std::optional<std::string> motion;
  // The theme and the renderer, for the next start: kept in the file.
  mux::config::theme_t theme = mux::config::theme::tinted{};
  mux::config::accent_t accent = mux::config::accent::theme_own{};
  mux::config::renderer_t renderer = mux::config::renderer::opengl{};
  // How much is kept, in memory and on disk.
  mux::config::cache_limits limits;
  // What is done to a picture dropped before it is sent.
  mux::config::sending_settings sending;
  void apply_limits() {
    mux::ui::avatar_images().budget = static_cast<std::size_t>(limits.pictures_in_memory_mb) << 20;
    store.budget = static_cast<std::uintmax_t>(limits.messages_on_disk_mb) << 20;
  }
  // The proxy chosen for the account being added, as it is added.
  std::optional<std::string> new_proxy;
  // What the message field's text is: a new message, an answer to one, or
  // one edited; and the message whose menu is up.
  compose_t composing = compose::plain{};
  request::message_menu menu_target;
  // The chats muted, and the proxy profiles: kept in the file.
  std::set<mux::conversation_id> muted;
  std::vector<mux::config::proxy_settings> proxies;
  // Why the accounts file could not be read, when it could not: then it is
  // not written over either.
  std::optional<std::string> config_error;
  // The drawer, left open under a page coming in over it, to go when the
  // page is in.
  bool drawer_waits = false;
  // The account being added that a login is waiting to hear about.
  std::optional<std::string> pending_login;
  actions ask;
  skiff::scene::Scene<window_type> scene{std::in_place, &ask};

  // -- what the host asks
  skiff::scene::Scene<window_type>& window() { return scene; }

  void woken() {
    auto changes = box->take();
    if (changes.empty())
      return;
    for (const auto& one : changes) {
      if (const auto* given = std::get_if<mux::change::session_given>(&one))
        this->keep_session(*given);
      if (const auto* picture = std::get_if<mux::change::avatar_loaded>(&one))
        this->take_avatar(*picture, true);
      model->apply(one);
      this->keep_on_disk(one);
    }
    // Messages held to a number in all, least recently read out first.
    model->trim(static_cast<std::size_t>(limits.messages_in_memory), root().main().chosen);
    this->refresh();
    // What comes into the chat being read, at its end, is read.
    if (const auto& chosen = root().main().chosen; chosen && root().main().timeline.atEnd(40.0f))
      this->mark_read(*chosen);
    // A room joined from a link: opened once it is here.
    if (pending_link)
      if (const auto found = this->chat_of(*pending_link)) {
        const auto where = std::exchange(pending_link, std::nullopt);
        this->open_chat(*found, where->event);
      }
  }

  // -- avatars: what the chats and the people in them look like
  // Fetched once each, kept on disk under the cache, shown by what they are
  // of: a chat by its id, a person by theirs.
  // -- messages on disk: every change to one written as it is now
  message_store store;
  void keep_on_disk(const mux::change_t& one) {
    if (ask.demo)
      return;
    const auto as_now = [&](const mux::conversation_id& in, const std::string& id) {
      if (const mux::conversation* chat = model->find(in))
        if (const auto found = std::ranges::find(chat->timeline, id, &mux::message::id); found != chat->timeline.end())
          store.record(*found);
    };
    std::visit(mux::overloaded{[&](const mux::change::message_added& c) { as_now(c.message.in, c.message.id); },
                               [&](const mux::change::message_edited& c) { as_now(c.in, c.id); },
                               [&](const mux::change::message_redacted& c) { as_now(c.in, c.id); },
                               [&](const mux::change::reaction_changed& c) { as_now(c.in, c.id); },
                               [&](const mux::change::receipts_changed& c) {
                                 if (const mux::conversation* chat = model->find(c.in))
                                   store.keep_reads(c.in, *chat);
                               },
                               [&](const mux::change::message_acknowledged& c) {
                                 store.forget(c.in, c.local_id);
                                 as_now(c.in, c.id);
                               },
                               [](const auto&) {}},
               one);
  }

  std::set<mux::conversation_id> members_fetched;
  // What is being fetched from a server, not to be asked for twice.
  std::set<std::string> avatars_fetched;
  static std::filesystem::path avatar_file(std::string_view source) {
    std::string name;
    for (const char c : source)
      name += std::isalnum(static_cast<unsigned char>(c)) ? c : '_';
    return mux::config::cache_path("avatars") / name;
  }
  void take_avatar(const mux::change::avatar_loaded& picture, bool fresh) {
    // A file fetched to be saved: into Downloads, a number added where the
    // name is taken, and opened.
    if (picture.key.starts_with("file:")) {
      this->save_download(picture.bytes, picture.key.substr(5), true);
      return;
    }
    if (picture.key.starts_with("save:")) {
      // The name after save:, where the menu gave one; a picture's else.
      const std::string name = picture.key.substr(5);
      this->save_download(picture.bytes, name.starts_with("mxc://") ? std::string("image") : name, false);
      return;
    }
    if (auto image = skia::decodeImage(picture.bytes.data(), picture.bytes.size())) {
      mux::ui::avatar_images().put(picture.key, std::move(image));
      scene.state().markDamaged();
    }
    if (fresh) {
      // A thumbnail or a whole picture under its key; an avatar under its
      // source, whoever it is of.
      const auto where = avatar_file(picture.key.starts_with("thumb:") || picture.key.starts_with("full:")
                                         ? picture.key
                                         : picture.source);
      std::error_code failed;
      std::filesystem::create_directories(where.parent_path(), failed);
      std::ofstream(where, std::ios::binary) << picture.bytes;
      avatars_fetched.erase(picture.source);
      avatars_fetched.erase(picture.key);
      if (++avatars_written % 50 == 1)
        prune_avatar_files();
    }
  }
  // The pictures on disk held to a size: the least recently used go first,
  // a file's time being when it was last read or written.
  std::size_t avatars_written = 0;
  void prune_avatar_files() {
    const std::uintmax_t budget = static_cast<std::uintmax_t>(limits.pictures_on_disk_mb) << 20;
    std::error_code failed;
    const auto directory = mux::config::cache_path("avatars");
    std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> files;
    std::uintmax_t total = 0;
    for (const auto& entry : std::filesystem::directory_iterator(directory, failed)) {
      if (!entry.is_regular_file(failed))
        continue;
      total += entry.file_size(failed);
      files.emplace_back(entry.last_write_time(failed), entry.path());
    }
    if (total <= budget)
      return;
    std::ranges::sort(files);
    for (const auto& [when, path] : files) {
      if (total <= budget)
        break;
      const auto size = std::filesystem::file_size(path, failed);
      if (std::filesystem::remove(path, failed))
        total -= size;
    }
  }
  // A message's picture's thumbnail: from the disk where it was fetched
  // before, from the account where not.
  void want_picture(const mux::account_id& of, const std::string& source) {
    const std::string key = "thumb:" + source;
    if (source.empty() || mux::ui::avatar_images().has(key) || avatars_fetched.contains(key))
      return;
    const auto where = avatar_file(key);
    if (std::ifstream file{where, std::ios::binary}) {
      std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
      std::error_code failed;
      std::filesystem::last_write_time(where, std::filesystem::file_time_type::clock::now(), failed);
      this->take_avatar(mux::change::avatar_loaded{key, source, std::move(bytes)}, false);
      return;
    }
    avatars_fetched.insert(key);
    net->fetch_media(of, source, key, 860);
  }
  // What the model has pictures of and the window has not: from the disk
  // where they were fetched before, from the account where not.
  void ask_avatars() {
    if (ask.demo)
      return;
    const auto want = [&](const mux::account_id& of, const std::optional<std::string>& source, const std::string& key) {
      // Shown already, or on its way: nothing to do.
      if (!source || source->empty() || mux::ui::avatar_images().has(key) || avatars_fetched.contains(*source))
        return;
      const auto where = avatar_file(*source);
      if (std::ifstream file{where, std::ios::binary}) {
        std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        std::error_code failed;
        std::filesystem::last_write_time(where, std::filesystem::file_time_type::clock::now(), failed);  // used now
        this->take_avatar(mux::change::avatar_loaded{key, *source, std::move(bytes)}, false);
        return;
      }
      avatars_fetched.insert(*source);
      net->fetch_avatar(of, *source, key);
    };
    for (const auto& [id, account] : model->accounts())
      for (const auto& [key, one] : account.conversations) {
        want(id, one.avatar, one.id.id);
        // The people of the chat being read: its members' pictures.
        if (root().main().chosen == one.id) {
          for (const mux::member& each : one.members)
            want(id, each.avatar, each.id);
          // Its pictures' thumbnails, at twice the size they are drawn at.
          for (const mux::message& said : one.timeline)
            if (said.attachment && mux::is_picture(said.attachment->kind))
              want_picture(id, said.attachment->source);
        }
      }
  }

  // A Matrix session given: kept with its account, for the next start.
  void keep_session(const mux::change::session_given& given) {
    const auto found = this->find(given.account.address);
    if (found == saved.end())
      return;
    std::visit(mux::overloaded{[&](mux::config::matrix_account& one) {
                                 one.access_token = given.access_token;
                                 one.device_id = given.device_id;
                               },
                               [](mux::config::xmpp_account&) {}},
               *found);
    (void)this->write();
  }

  void before_frame() {
    root().drop_closed();
    auto pending = std::exchange(ask.requests, {});
    for (const request_t& one : pending)
      std::visit([this](const auto& each) { this->apply(each); }, one);
    if (drawer_waits && !root().pages_moving()) {
      root().close_drawer_now();
      drawer_waits = false;
    }
  }

  void closing() {
    if (const auto& chosen = root().main().chosen)
      this->keep_draft(*chosen, root().main().line.text());
    net->shutdown();
  }
  // Drafts: in the screen, and on disk in one small file, written anew
  // when one changes.
  void keep_draft(const mux::conversation_id& in, const std::string& text) {
    auto& drafts = root().main().drafts;
    const bool blank = std::ranges::all_of(text, [](unsigned char c) { return std::isspace(c) != 0; });
    const auto found = drafts.find(in);
    if (blank ? found == drafts.end() : (found != drafts.end() && found->second == text))
      return;
    if (blank)
      drafts.erase(in);
    else
      drafts.insert_or_assign(in, text);
    if (ask.demo)
      return;
    knot::value::object all;
    for (const auto& [id, draft] : drafts)
      all.emplace(id.account.address + "\n" + id.id, knot::value(draft));
    const auto where = mux::config::state_path("drafts.json");
    std::error_code failed;
    std::filesystem::create_directories(where.parent_path(), failed);
    std::ofstream(where, std::ios::binary | std::ios::trunc) << knot::to_json_string(knot::value(std::move(all)));
  }
  void load_drafts() {
    std::ifstream file(mux::config::state_path("drafts.json"), std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    auto parsed = knot::try_read<knot::value>(std::string_view(text));
    if (!parsed || !parsed->is<knot::value::object>())
      return;
    for (const auto& [key, draft] : parsed->as<knot::value::object>()) {
      const auto cut = key.find('\n');
      if (cut == std::string::npos || !draft.is<std::string>())
        continue;
      const std::string address = key.substr(0, cut);
      root().main().drafts.insert_or_assign(
          mux::conversation_id{mux::account_id{mux::ui::protocol_of(address), address}, key.substr(cut + 1)},
          draft.as<std::string>());
    }
  }

  // -- the window
  window_type& root() { return scene.root(); }

  void show_conversations() {
    root().close_drawer();
    pending_login.reset();
    root().close();
    this->refresh();
  }
  accounts& show_accounts() {
    // From the drawer, the page comes in over it, and the drawer goes once
    // the page is in: not two things moving at once.
    if (root().drawer_open())
      drawer_waits = true;
    root().close_settings();
    pending_login.reset();
    auto& panel = root().open<accounts>();
    if (config_error)
      panel.say(*config_error);
    this->refresh();
    return panel;
  }
  // The accounts, with this one's settings up beside them.
  accounts& show_account(const std::string& address) {
    auto& panel = this->show_accounts();
    if (const auto found = this->find(address); found != saved.end()) {
      panel.select(*found, *model);
      panel.show(saved, *model);
    }
    return panel;
  }
  // Adding an account: beside the list, on the accounts page.
  void show_adding() {
    auto& panel = this->show_accounts();
    panel.proxies = proxies;
    panel.show_adding();
    this->refresh();
  }

  std::vector<mux::config::account_t>::iterator find(std::string_view address) {
    return std::ranges::find(saved, address, [](const auto& one) -> std::string_view {
      return mux::config::address_of(one);
    });
  }

  // The panel that is up, if one is, and the XMPP form in it, if there is one.
  [[nodiscard]] mux::ui::xmpp_form<actions>* xmpp_form_up() {
    auto* up = root().open_panel();
    if (!up)
      return nullptr;
    return std::visit([](auto& panel) { return panel.xmpp(); }, *up);
  }

  // Everything brought up to date with the model: each panel by its own
  // overload.
  void refresh() {
    this->ask_avatars();
    root().main().muted = muted;
    root().show(saved, *model);
    root().main().show(*model);
    if (auto* up = root().open_panel())
      std::visit([this](auto& panel) { this->bring_up_to_date(panel); }, *up);
  }
  void bring_up_to_date(accounts& panel) {
    panel.proxies = proxies;
    panel.show(saved, *model);
    if (auto* pane = panel.adding())
      std::visit([this](auto& form) { this->watch_login(form); }, pane->form);
  }

  // A new account waiting to log in: online is done, failed is said.
  template <class Form>
  void watch_login(Form& form) {
    if (!pending_login)
      return;
    const auto found = model->accounts().find(mux::account_id{mux::ui::protocol_of(*pending_login), *pending_login});
    if (found == model->accounts().end())
      return;
    std::visit(mux::overloaded{[&](const mux::connection::online&) { this->show_conversations(); },
                               [&](const mux::connection::failed& why) {
                                 form.say(why.error.empty() ? "The server said no." : why.error, true);
                                 pending_login.reset();
                               },
                               [&](const auto&) { form.say("Connecting…", false); }},
               found->second.state);
  }

  void apply(const request::choose& one) {
    // What was being written where the reader was: kept as its draft; and
    // the chat opened's own put back in the field.
    auto& screen = root().main();
    // Finding is in one chat: another chosen, it is closed.
    if (searching && searching->in != one.which) {
      searching.reset();
      screen.show_search(false);
    }
    if (screen.chosen && *screen.chosen != one.which) {
      this->keep_draft(*screen.chosen, screen.line.text());
      screen.line.set_text(screen.draft_of(one.which));
    } else if (!screen.chosen) {
      screen.line.set_text(screen.draft_of(one.which));
    }
    model->touch(one.which);
    // What was kept of its reads, where the model has nothing newer.
    if (const mux::conversation* chat = model->find(one.which); chat && !ask.demo) {
      auto kept = message_store::read_reads(one.which);
      std::map<std::string, std::string> missing;
      for (auto& [user, event] : kept.read_by)
        if (!chat->read_by.contains(user))
          missing.emplace(user, std::move(event));
      if (!missing.empty())
        model->apply(mux::change_t{mux::change::receipts_changed{one.which, std::move(missing)}});
      if (!chat->read_up_to && kept.me)
        model->read_up_to(one.which, *kept.me);
    }
    // A group opened: all its members, once, where a sync gives only some.
    if (const mux::conversation* chat = model->find(one.which);
        chat && !ask.demo && chat->member_count > static_cast<std::int64_t>(chat->members.size()) &&
        members_fetched.insert(one.which).second)
      net->fetch_members(one.which);
    root().main().chosen = one.which;
    root().main().show(*model);
    this->mark_read(one.which);
  }

  // A chat opened: read up to its last message from someone else, and the
  // people in it told so where read receipts are on.
  // The user's own read position: kept here always -- in the model and on
  // disk -- and told to the server only where the account's privacy lets it.
  void mark_read(const mux::conversation_id& which) {
    if (ask.demo)
      return;
    const mux::conversation* one = model->find(which);
    if (!one)
      return;
    for (auto it = one->timeline.rbegin(); it != one->timeline.rend(); ++it)
      if (!it->outgoing && !it->id.empty()) {
        if (one->read_up_to == it->id)
          return;
        const std::string id = it->id;
        model->read_up_to(which, id);
        store.keep_reads(which, *model->find(which));
        if (const auto account = this->find(which.account.address);
            account != saved.end() && mux::config::read_receipts_of(*account))
          net->mark_read(which, id);
        return;
      }
  }

  // The chosen chat left: a Matrix room here; XMPP rooms are not there yet.
  void apply(const request::leave_chat&) {
    const auto& chosen = root().main().chosen;
    if (!chosen)
      return;
    const mux::conversation* one = model->find(*chosen);
    const bool room = one && mux::ui::is_group(*one);
    std::visit(mux::overloaded{[&](mux::protocol::xmpp) {
                                 if (!room) {
                                   root().show_notice("Leaving a direct XMPP chat");
                                   return;
                                 }
                                 if (!ask.demo)
                                   net->leave(*chosen);
                                 root().main().info_open = false;
                               },
                               [&](mux::protocol::matrix) {
                                 if (!ask.demo)
                                   net->leave(*chosen);
                                 root().main().info_open = false;
                               }},
               chosen->account.speaks);
  }
  void apply(const request::back&) { this->show_conversations(); }
  void apply(const request::open_accounts&) { (void)this->show_accounts(); }
  void apply(const request::open_new_account&) { this->show_adding(); }
  void apply(const request::add_xmpp&) { this->switch_form(&adding::show_xmpp); }
  void apply(const request::add_matrix&) { this->switch_form(&adding::show_matrix); }
  void apply(const request::select_account& one) {
    auto* up = root().open_panel();
    if (!up)
      return;
    std::visit(
        [&](accounts& panel) {
          if (const auto found = this->find(one.address); found != saved.end()) {
            pending_login.reset();
            panel.select(*found, *model);
            panel.show(saved, *model);
          }
        },
        *up);
  }
  void apply(const request::toggle_advanced&) {
    if (auto* form = this->xmpp_form_up())
      form->show_advanced(!form->advanced);
  }
  void apply(const request::toggle_plain&) {
    if (auto* form = this->xmpp_form_up())
      form->flip_plain();
  }
  void apply(const request::submit_login&) {
    auto* up = root().open_panel();
    if (!up)
      return;
    std::visit(
        [this](accounts& panel) {
          if (auto* editor = panel.editor())
            std::visit([this](auto& form) { this->edit(form); }, editor->form);
          else if (auto* pane = panel.adding()) {
            new_proxy = pane->proxy;
            std::visit([this](auto& form) { this->add(form); }, pane->form);
          }
        },
        *up);
  }
  void apply(const request::flip_enabled& one) { this->flip_enabled(one.address); }
  void apply(const request::remove_account& one) { this->remove(one.address); }
  void apply(const request::open_drawer&) { root().open_drawer(); }
  void apply(const request::show_account& one) { (void)this->show_account(one.address); }
  void apply(const request::set_motion& one) { this->set_motion(one.level); }
  void apply(const request::quit&) { mux::host::request_quit(); }
  void apply(const request::open_settings&) {
    root().close_drawer();
    root().open_settings(motion.value_or("full"));
  }
  void apply(const request::close_settings&) { root().close_settings(); }
  void apply(const request::toggle_info&) { root().main().toggle_info(); }
  void apply(const request::jump_to_end&) { root().main().jump_to_end(); }

  // A message's menu, and what is chosen from it.
  void apply(const request::message_menu& one) {
    menu_target = one;
    root().open_menu(one);
  }
  void apply(const request::close_menu&) { root().close_menu(); }
  // -- files to send: chosen with the paperclip, or dropped on the window
  struct prepared_file {
    std::string bytes;
    std::string name;
    std::string mimetype;
    bool image = false;
    int width = 0, height = 0;
    std::string key;  // its picture, known to the window already
  };
  std::vector<prepared_file> to_send;
  std::uint64_t files_made = 0;
  void apply(const request::attach_files&) {
    if (root().main().chosen)
      mux::host::choose_files();
  }
  void apply(const request::close_send_box&) {
    to_send.clear();
    root().close_send_box();
  }
  // Files given: read, a picture known by its bytes; a picture dropped on
  // the window written anew from its pixels -- nothing of its file, its
  // metadata among it, goes with it -- and named image.<its type>. Then the
  // send box, with what was waiting in it before.
  void files_given(std::vector<std::string> paths, bool dropped) {
    if (!root().main().chosen)
      return;
    for (const std::string& path : paths) {
      std::ifstream file(path, std::ios::binary);
      if (!file)
        continue;
      prepared_file one;
      one.bytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
      one.name = std::filesystem::path(path).filename().string();
      one.mimetype = "application/octet-stream";
      if (const auto type = mux::media::picture_of(one.bytes)) {
        if (auto image = skia::decodeImage(one.bytes.data(), one.bytes.size())) {
          one.image = true;
          one.width = image->width();
          one.height = image->height();
          one.mimetype = std::string(mux::media::mimetype_of(*type));
          // Dropped: its metadata cut out of the file, the picture's bytes
          // as they were, and named image.<type> -- as Settings says.
          if (dropped && sending.strip_metadata)
            one.bytes = mux::media::without_metadata(one.bytes);
          if (dropped && sending.rename)
            one.name = std::format("image.{}", mux::media::extension_of(*type));
          one.key = std::format("thumb:local:mux-file-{}-{}", std::chrono::system_clock::now().time_since_epoch().count(),
                                ++files_made);
          mux::ui::avatar_images().put(one.key, std::move(image));
        }
      }
      to_send.push_back(std::move(one));
    }
    if (to_send.empty())
      return;
    std::vector<mux::ui::pending_file> shown;
    for (const prepared_file& one : to_send)
      shown.push_back({one.name, one.key, static_cast<std::int64_t>(one.bytes.size()), one.image});
    root().open_send_box(shown);
  }
  // Sent: each file, the caption with the first; the box closed.
  void apply(const request::send_files&) {
    const auto& chosen = root().main().chosen;
    auto* box = root().send_box_up();
    if (!chosen || !box || to_send.empty())
      return;
    std::string caption = box->caption.text();
    for (prepared_file& one : to_send) {
      // Its local id is its picture's, so the window shows it while it goes.
      std::string local = one.key.empty() ? std::format("mux-file-{}-{}", std::chrono::system_clock::now().time_since_epoch().count(),
                                                        ++files_made)
                                          : one.key.substr(std::string_view("thumb:local:").size());
      net->send_file(*chosen, std::move(local), std::move(one.bytes), one.name, one.mimetype, one.image, one.width,
                     one.height, std::exchange(caption, std::string()));
    }
    to_send.clear();
    root().close_send_box();
  }

  // A picture seen whole: over the window at once, the thumbnail until all
  // of it has come.
  void apply(const request::open_picture& one) {
    root().open_picture(one.source, one.sender, one.name, one.when);
    const auto& chosen = root().main().chosen;
    if (chosen && !mux::ui::avatar_images().has("full:" + one.source) && avatars_fetched.insert("full:" + one.source).second)
      net->fetch_media(chosen->account, one.source, "full:" + one.source, 0);
  }
  void apply(const request::close_picture&) { root().close_picture(); }
  // A picture saved into Downloads: from the disk where it was fetched
  // whole, fetched whole where not; named as the picture's type says.
  void apply(const request::save_picture& one) {
    const auto kept = avatar_file("full:" + one.source);
    if (std::ifstream file{kept, std::ios::binary}) {
      std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
      this->save_download(bytes, "image", false);
      return;
    }
    if (const auto& chosen = root().main().chosen)
      net->fetch_media(chosen->account, one.source, "save:" + one.source, 0);
  }
  // Bytes saved into Downloads under a name -- a picture's with its type's
  // extension, a number added where the name is taken -- and opened, or not.
  void save_download(const std::string& bytes, std::string name, bool open) {
    if (const auto type = mux::media::picture_of(bytes); type && !name.contains('.'))
      name += std::format(".{}", mux::media::extension_of(*type));
    const std::filesystem::path base = std::filesystem::path(name).filename();
    std::error_code failed;
    std::filesystem::create_directories(downloads(), failed);
    auto where = downloads() / (base.empty() ? std::filesystem::path("file") : base);
    for (int n = 1; std::filesystem::exists(where, failed); ++n)
      where = downloads() / std::format("{} ({}){}", base.stem().string(), n, base.extension().string());
    std::ofstream(where, std::ios::binary) << bytes;
    if (open)
      mux::host::open_url("file://" + where.string());
    else
      root().show_message("Saved", std::format("Saved to {}", where.string()));
  }
  // A file saved -- into ~/Downloads, under its name -- and opened.
  void apply(const request::open_file& one) {
    const auto& chosen = root().main().chosen;
    if (!chosen)
      return;
    net->fetch_media(chosen->account, one.source, "file:" + one.name, 0);
  }
  static std::filesystem::path downloads() {
    if (const char* home = std::getenv("HOME"); home && *home)
      return std::filesystem::path(home) / "Downloads";
    return std::filesystem::current_path();
  }

  // A message swiped to the left: answered, as its menu's Reply does.
  void apply(const request::reply_to& one) {
    menu_target.id = one.id;
    menu_target.text = one.text;
    this->apply(request::menu_reply{});
  }
  void apply(const request::menu_reply&) {
    root().close_menu();
    composing = compose::reply{menu_target.id};
    // As tdesktop's: "Reply to <name>" over a line of the message -- its
    // text, or what it carries where it has none.
    std::string title = "Reply";
    std::string line = menu_target.text;
    if (const auto& chosen = root().main().chosen)
      if (const mux::conversation* chat = model->find(*chosen)) {
        const auto said = std::ranges::find(chat->timeline, menu_target.id, &mux::message::id);
        if (said != chat->timeline.end()) {
          title = "Reply to " + mux::ui::sender_name(*chat, said->sender);
          if (line.empty() && said->attachment)
            line = mux::is_picture(said->attachment->kind) ? std::string("Photo") : said->attachment->name;
        }
      }
    std::ranges::replace(line, '\n', ' ');
    root().main().line.show_context(mux::ui::compose_context{mux::ui::icon::reply{}, std::move(title), std::move(line)});
  }
  void apply(const request::menu_edit&) {
    root().close_menu();
    composing = compose::edit{menu_target.id};
    std::string line = menu_target.text;
    std::ranges::replace(line, '\n', ' ');
    root().main().line.show_context(mux::ui::compose_context{mux::ui::icon::pencil{}, "Edit message", std::move(line)});
    root().main().line.set_text(menu_target.text);
  }
  void apply(const request::menu_copy&) {
    root().close_menu();
    skiff::scene::setClipboardText(menu_target.copied.empty() ? menu_target.text : menu_target.copied);
  }
  // A reaction: the user's own put where it is not, taken back where it
  // is -- shown at once, and told to the server.
  void apply(const request::react& one) {
    const auto& chosen = root().main().chosen;
    const mux::conversation* chat = chosen ? model->find(*chosen) : nullptr;
    if (!chat)
      return;
    const auto said = std::ranges::find(chat->timeline, one.id, &mux::message::id);
    if (said == chat->timeline.end())
      return;
    const std::string& me = chosen->account.address;
    const auto who = said->reactions.find(one.key);
    const bool on = who == said->reactions.end() || !who->second.contains(me);
    model->apply(mux::change_t{mux::change::reaction_changed{*chosen, one.id, one.key, me, on}});
    if (!ask.demo)
      net->react(*chosen, one.id, one.key, on);
    this->refresh();
  }
  void apply(const request::menu_react& one) {
    root().close_menu();
    this->apply(request::react{menu_target.id, one.key});
  }
  void apply(const request::menu_copy_link&) {
    root().close_menu();
    skiff::scene::setClipboardText(menu_target.link);
  }
  // A picture or a file saved into Downloads, from the menu.
  void apply(const request::menu_save&) {
    root().close_menu();
    if (!menu_target.media)
      return;
    const auto kept = avatar_file("full:" + *menu_target.media);
    if (std::ifstream file{kept, std::ios::binary}) {
      std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
      this->save_download(bytes, menu_target.media_name.empty() ? std::string("image") : menu_target.media_name, false);
      return;
    }
    if (const auto& chosen = root().main().chosen)
      net->fetch_media(chosen->account, *menu_target.media,
                       "save:" + (menu_target.media_name.empty() ? std::string("image") : menu_target.media_name), 0);
  }
  void apply(const request::menu_delete&) {
    root().close_menu();
    const auto& chosen = root().main().chosen;
    if (!chosen)
      return;
    if (ask.demo)
      box->push(mux::change_t{mux::change::message_redacted{*chosen, menu_target.id}});
    else
      net->remove_message(*chosen, menu_target.id);
  }
  // A link pressed: to a user, a room or a message, in here -- matrix.to,
  // matrix: and xmpp: links -- and anywhere else, in the browser.
  void apply(const request::open_url& one) {
    if (auto where = link_target_of(one.url)) {
      this->go_to(*where);
      return;
    }
    mux::host::open_url(one.url);
  }

  // What a link points at, in here.
  struct link_target {
    std::string id;                    // @user, !room, #alias, or a JID
    std::optional<std::string> event;  // $event, in the room
    std::vector<std::string> via;      // the servers to join through
    bool xmpp = false;
  };
  static std::string percent_decoded(std::string_view text) {
    std::string out;
    for (std::size_t at = 0; at < text.size(); ++at) {
      if (text[at] == '%' && at + 2 < text.size() + 0 && at + 2 <= text.size() - 1) {
        unsigned value = 0;
        if (std::from_chars(text.data() + at + 1, text.data() + at + 3, value, 16).ec == std::errc{}) {
          out += static_cast<char>(value);
          at += 2;
          continue;
        }
      }
      out += text[at];
    }
    return out;
  }
  static std::optional<link_target> link_target_of(std::string_view url) {
    link_target out;
    std::string_view query;
    const auto split_query = [&](std::string_view& path) {
      if (const auto mark = path.find('?'); mark != std::string_view::npos) {
        query = path.substr(mark + 1);
        path = path.substr(0, mark);
      }
    };
    const auto read_via = [&] {
      for (std::size_t at = 0; at < query.size();) {
        auto end = query.find('&', at);
        if (end == std::string_view::npos)
          end = query.size();
        const std::string_view pair = query.substr(at, end - at);
        if (pair.starts_with("via="))
          out.via.push_back(percent_decoded(pair.substr(4)));
        at = end + 1;
      }
    };
    if (url.starts_with("https://matrix.to/#/")) {
      std::string_view path = url.substr(20);
      split_query(path);
      read_via();
      const auto slash = path.find('/');
      out.id = percent_decoded(path.substr(0, slash));
      if (slash != std::string_view::npos)
        out.event = percent_decoded(path.substr(slash + 1));
    } else if (url.starts_with("matrix:")) {
      std::string_view path = url.substr(7);
      split_query(path);
      read_via();
      const auto part = [&]() {
        const auto slash = path.find('/');
        const std::string_view one = path.substr(0, slash);
        path = slash == std::string_view::npos ? std::string_view() : path.substr(slash + 1);
        return one;
      };
      // Its parts' names (MSC2312), read into what they are.
      const uri_part_t kind = uri_part_of(part());
      const std::string name = percent_decoded(part());
      const std::optional<char> sigil = std::visit([](auto of) { return of.sigil; }, kind);
      if (!sigil)
        return std::nullopt;
      out.id = *sigil + name;
      if (std::visit([](auto of) { return of.names_event; }, uri_part_of(part())))
        out.event = "$" + percent_decoded(part());
    } else if (url.starts_with("xmpp:")) {
      std::string_view path = url.substr(5);
      split_query(path);
      out.id = percent_decoded(path);
      out.xmpp = true;
    } else {
      return std::nullopt;
    }
    if (out.id.empty() || (!out.xmpp && !std::string_view("@!#").contains(out.id.front())))
      return std::nullopt;
    return out;
  }

  // A link followed: the chat it names, opened -- and the message in it,
  // jumped to -- or the person, their page; a room not joined yet, joined,
  // and opened when it comes.
  std::optional<link_target> pending_link;
  void open_chat(const mux::conversation_id& which, const std::optional<std::string>& event) {
    auto& screen = root().main();
    screen.current = which.account;
    this->apply(request::choose{which});
    if (event)
      screen.jump_to(*event);
  }
  std::optional<mux::conversation_id> chat_of(const link_target& where) const {
    for (const auto& [account, one] : model->accounts())
      for (const auto& [key, chat] : one.conversations) {
        const bool matrix = mux::is_matrix(account.speaks);
        if (where.xmpp != !matrix)
          continue;
        if (chat.id.id == where.id || (chat.alias && *chat.alias == where.id))
          return chat.id;
      }
    return std::nullopt;
  }
  void go_to(const link_target& where) {
    auto& screen = root().main();
    if (!where.xmpp && where.id.front() == '@') {
      // In the chat being read: their page. Elsewhere: a chat with them.
      if (const mux::conversation* here = screen.chosen ? model->find(*screen.chosen) : nullptr;
          here && std::ranges::contains(here->members, where.id, &mux::member::id)) {
        this->apply(request::open_member_info{where.id});
        return;
      }
      for (const auto& [account, one] : model->accounts())
        for (const auto& [key, chat] : one.conversations)
          if (mux::one_to_one(chat.kind) &&
              std::ranges::contains(chat.members, where.id, &mux::member::id)) {
            this->open_chat(chat.id, std::nullopt);
            return;
          }
      root().show_message("No chat yet", std::format("There is no chat with {} yet.", where.id));
      return;
    }
    if (const auto found = this->chat_of(where)) {
      this->open_chat(*found, where.event);
      return;
    }
    if (where.xmpp) {
      root().show_message("Not joined", std::format("{} is not in your list.", where.id));
      return;
    }
    // A Matrix room not joined: joined through the account in view, or the
    // first Matrix one, and opened when it comes.
    std::optional<mux::account_id> by;
    if (screen.current && mux::is_matrix(screen.current->speaks))
      by = screen.current;
    for (const auto& [account, one] : model->accounts())
      if (!by && mux::is_matrix(account.speaks))
        by = account;
    if (!by) {
      root().show_message("No Matrix account", "A Matrix account is needed to open that room.");
      return;
    }
    pending_link = where;
    net->join(*by, where.id, where.via);
  }
  void apply(const request::cancel_compose&) {
    composing = compose::plain{};
    root().main().line.show_context(std::nullopt);
  }
  // Older messages of a chat: from the disk while it has some from before
  // the oldest in memory, from the server past that.
  void apply(const request::load_older& one) {
    if (ask.demo)
      return;
    const mux::conversation* chat = model->find(one.in);
    const auto before = chat && !chat->timeline.empty() ? chat->timeline.front().at
                                                        : message_store::time_point::max();
    if (auto kept = store.older(one.in, before, 100); !kept.empty()) {
      for (auto it = kept.rbegin(); it != kept.rend(); ++it)
        model->apply(mux::change_t{mux::change::message_added{.message = std::move(*it), .history = true}});
      // The window may ask again: there may be more on the disk.
      root().main().history_asked.reset();
      this->refresh();
      return;
    }
    net->load_older(one.in, one.from);
  }
  void apply(const request::submit_message& one) { this->send_message(one.text); }
  void apply(const request::resize_sidebar& one) { root().main().resize_sidebar(one.x); }
  // A member written to: their direct chat, where there is one already.
  void apply(const request::message_person& one) {
    if (model->find(one.who) != nullptr)
      this->apply(request::choose{one.who});
    else
      root().show_notice("Starting a new chat");
  }
  void apply(const request::jump_to_message& one) { root().main().jump_to(one.id); }

  // Finding in a chat: what is asked, in which chat, the messages it is in
  // (newest first), and which of them is shown.
  struct search_state {
    mux::conversation_id in;
    std::string query;
    std::vector<std::string> found;
    std::optional<std::size_t> at;
  };
  std::optional<search_state> searching;
  void apply(const request::open_search&) {
    auto& screen = root().main();
    if (!screen.chosen)
      return;
    if (!searching || searching->in != *screen.chosen)
      searching = search_state{*screen.chosen, {}, {}, std::nullopt};
    screen.show_search(true);
    scene.focus(screen.search.field);
  }
  void apply(const request::close_search&) {
    searching.reset();
    root().main().show_search(false);
  }
  void apply(const request::search_typed& one) {
    if (!searching)
      return;
    searching->query = one.text;
    searching->found = this->find_in(searching->in, one.text);
    searching->at.reset();
    this->search_to(true);
  }
  void apply(const request::search_step& one) { this->search_to(one.older); }
  // To the next found, older or newer, shown and flashed; the count set.
  void search_to(bool older) {
    if (!searching)
      return;
    auto& screen = root().main();
    const std::size_t count = searching->found.size();
    if (count > 0) {
      if (!searching->at)
        searching->at = 0;
      else if (older && *searching->at + 1 < count)
        ++*searching->at;
      else if (!older && *searching->at > 0)
        --*searching->at;
      screen.jump_to(searching->found[*searching->at]);
    }
    screen.search.show_found(searching->at, count, !searching->query.empty());
  }
  // The messages of a chat with the words asked in their text or in the name
  // of what they carry, any case: all it has -- those on disk and those in
  // memory -- newest first.
  std::vector<std::string> find_in(const mux::conversation_id& in, std::string_view query) {
    const std::string asked = mux::ui::folded(query);
    if (asked.empty())
      return {};
    std::map<std::string, mux::message> all = store.everything(in);
    if (const mux::conversation* chat = model->find(in))
      for (const mux::message& one : chat->timeline)
        all.insert_or_assign(one.id, one);
    std::vector<const mux::message*> hits;
    for (const auto& [id, one] : all) {
      if (one.redacted)
        continue;
      const bool in_text = mux::ui::folded(one.body.plain).contains(asked);
      const bool in_name = one.attachment && mux::ui::folded(one.attachment->name).contains(asked);
      if (in_text || in_name)
        hits.push_back(&one);
    }
    std::ranges::sort(hits, std::ranges::greater{}, &mux::message::at);
    std::vector<std::string> out;
    for (const mux::message* one : hits)
      out.push_back(one->id);
    return out;
  }
  // A sender pressed in the messages: their page, in the chat's info.
  void apply(const request::open_member_info& one) {
    auto& screen = root().main();
    if (!screen.info_open)
      screen.toggle_info();
    screen.info.open_member(one.id);
  }
  void apply(const request::not_implemented& one) { root().show_notice(one.what); }
  void apply(const request::close_notice&) { root().close_notice(); }
  void apply(const request::resize_info& one) { root().main().resize_info(one.x); }
  void apply(const request::choose_new_proxy& one) {
    if (auto* up = root().open_panel())
      std::visit(
          [&](accounts& panel) {
            if (auto* pane = panel.adding())
              pane->set_proxy(one.index);
          },
          *up);
  }
  // The chosen chat muted, or not: kept in the file.
  void apply(const request::toggle_mute&) {
    auto& screen = root().main();
    if (!screen.chosen)
      return;
    if (!muted.erase(*screen.chosen))
      muted.insert(*screen.chosen);
    (void)this->write();
    this->refresh();
  }
  void apply(const request::close_account_pages&) {
    if (auto* up = root().open_panel())
      std::visit([](accounts& panel) { panel.close_pages(); }, *up);
  }
  // ← on the accounts page: from an account's pages to the list, from the
  // list to the chats.
  void apply(const request::accounts_back&) {
    auto* up = root().open_panel();
    if (!up)
      return;
    std::visit(
        [this](accounts& panel) {
          if (panel.pages_open())
            panel.close_pages();
          else
            this->apply(request::pop_panel{});
        },
        *up);
  }
  // The chosen account, and its accounts page, when they are up.
  template <class F>
  void with_chosen_account(F&& f) {
    auto* up = root().open_panel();
    if (!up)
      return;
    std::visit(
        [&](accounts& panel) {
          if (!panel.selected)
            return;
          if (const auto found = this->find(*panel.selected); found != saved.end())
            f(panel, *found);
        },
        *up);
  }
  void apply(const request::account_page& one) {
    this->with_chosen_account([&](accounts& panel, mux::config::account_t& account) {
      panel.show_page(one.page, account, *model, proxies);
    });
  }
  void apply(const request::flip_account_receipts&) {
    this->with_chosen_account([&](accounts& panel, mux::config::account_t& account) {
      auto& kept = mux::config::read_receipts_in(account);
      kept = !kept.value_or(true);
      if (auto* page = panel.privacy())
        page->show(*kept, mux::config::send_typing_of(account));
      (void)this->write();
    });
  }
  void apply(const request::flip_account_typing&) {
    this->with_chosen_account([&](accounts& panel, mux::config::account_t& account) {
      auto& kept = mux::config::send_typing_in(account);
      kept = !kept.value_or(true);
      if (auto* page = panel.privacy())
        page->show(mux::config::read_receipts_of(account), *kept);
      (void)this->write();
    });
  }
  // The user typing in the chosen chat, or not: said, where the account's
  // privacy lets it -- 'typing' at most every twenty seconds while it goes
  // on (it lasts thirty), 'stopped' when the field is emptied or sent.
  std::optional<mux::conversation_id> typing_in;
  std::chrono::steady_clock::time_point typing_said{};
  void apply(const request::typing& one) {
    if (ask.demo)
      return;
    const auto& chosen = root().main().chosen;
    const auto now = std::chrono::steady_clock::now();
    const auto allowed = [&](const mux::conversation_id& in) {
      const auto account = this->find(in.account.address);
      return account != saved.end() && mux::config::send_typing_of(*account);
    };
    if (typing_in && (!one.on || typing_in != chosen)) {
      if (allowed(*typing_in))
        net->typing(*typing_in, false);
      typing_in.reset();
    }
    if (one.on && chosen && allowed(*chosen) &&
        (typing_in != chosen || now - typing_said > std::chrono::seconds(20))) {
      net->typing(*chosen, true);
      typing_in = chosen;
      typing_said = now;
    }
  }
  void apply(const request::proxy_kind& one) {
    if (auto* up = root().settings_up())
      if (auto* editor = up->editor())
        editor->set_kind(one.kind);
  }
  // The chosen account through a profile, or none: kept, and connected again.
  void apply(const request::choose_account_proxy& one) {
    this->with_chosen_account([&](accounts& panel, mux::config::account_t& account) {
      auto& kept = mux::config::proxy_in(account);
      if (one.index < 0 || static_cast<std::size_t>(one.index) >= proxies.size())
        kept.reset();
      else
        kept = proxies[static_cast<std::size_t>(one.index)].name;
      (void)this->write();
      this->reconnect(account);
      panel.show_page(2, account, *model, proxies);
    });
  }
  void reconnect(const mux::config::account_t& account) {
    if (!mux::config::enabled_of(account) || ask.demo)
      return;
    net->remove(mux::config::address_of(account));
    net->add(account, proxies);
  }
  // The accounts going through a profile, connected again.
  void reconnect_through(const std::string& name) {
    for (const auto& one : saved)
      if (mux::config::proxy_of(one) == name)
        this->reconnect(one);
  }
  void apply(const request::settings_appearance&) {
    if (auto* up = root().settings_up())
      up->show_appearance(theme, accent);
  }
  void apply(const request::settings_files&) {
    if (auto* up = root().settings_up())
      up->show_files(sending);
  }
  void apply(const request::flip_strip_metadata&) {
    sending.strip_metadata = !sending.strip_metadata;
    (void)this->write();
  }
  void apply(const request::flip_rename_pictures&) {
    sending.rename = !sending.rename;
    (void)this->write();
  }
  void apply(const request::settings_storage&) {
    if (auto* up = root().settings_up())
      up->show_storage(limits);
  }
  // A limit halved or doubled, within its bounds: kept, and in force at once.
  void apply(const request::change_limit& one) {
    std::int64_t& value = mux::config::value_of(limits, one.which);
    const auto [low, high] = mux::config::bounds_of(one.which);
    value = std::clamp(one.more ? value * 2 : value / 2, low, high);
    this->apply_limits();
    model->trim(static_cast<std::size_t>(limits.messages_in_memory), root().main().chosen);
    (void)this->write();
    if (auto* up = root().settings_up())
      if (auto* page = up->storage())
        page->show(limits);
  }
  // What is kept on disk, gone: the stored messages and the pictures, and
  // the pictures in memory, to be fetched again as they are wanted.
  void apply(const request::clear_stored&) {
    std::error_code failed;
    std::filesystem::remove_all(mux::config::state_path("messages"), failed);
    std::filesystem::remove_all(mux::config::cache_path("avatars"), failed);
    mux::ui::avatar_images().clear();
    avatars_fetched.clear();
    root().show_message("Storage", "The stored messages and pictures are cleared.");
    this->refresh();
  }
  void apply(const request::settings_rendering&) {
    if (auto* up = root().settings_up())
      up->show_rendering(renderer);
  }
  // The theme or the renderer chosen: kept, for the next start.
  // A theme chosen: its colours in place, and the window made again in them,
  // as it was -- the chats, the one chosen, the widths -- with Settings open
  // where it was.
  void apply(const request::set_theme& one) {
    theme = one.theme;
    (void)this->write();
    this->rebuild_in_theme();
  }
  // An accent chosen: the same as a theme, over it.
  void apply(const request::set_accent& one) {
    accent = one.accent;
    (void)this->write();
    this->rebuild_in_theme();
  }
  // The theme and accent now chosen put in place, and the window made again
  // in them, as it was, with Settings open on Appearance.
  void rebuild_in_theme() {
    auto& before = root().main();
    const auto chosen = before.chosen;
    const auto current = before.current;
    const float side_width = before.side_width;
    const float info_width = before.info_width;
    const bool info_open = before.info_open;
    mux::ui::use_theme(theme, accent);
    pending_login.reset();
    drawer_waits = false;
    root().rebuild();
    auto& after = root().main();
    after.chosen = chosen;
    after.current = current;
    after.side_width = side_width;
    after.info_width = info_width;
    after.info_open = info_open;
    this->refresh();
    root().open_settings(motion.value_or("full"));
    if (auto* up = root().settings_up())
      up->show_appearance(theme, accent);
  }
  void apply(const request::set_renderer& one) {
    renderer = one.renderer;
    this->show_appearance_choices();
    (void)this->write();
  }
  void show_appearance_choices() {
    if (auto* up = root().settings_up()) {
      if (auto* page = up->appearance())
        page->show(theme, accent);
      if (auto* page = up->rendering())
        page->show(renderer);
    }
  }
  void apply(const request::manage_proxies&) {
    root().open_settings(motion.value_or("full"));
    if (auto* up = root().settings_up())
      up->show_proxies(proxies, false);
  }
  void apply(const request::settings_proxies&) {
    if (auto* up = root().settings_up())
      up->show_proxies(proxies);
  }
  void apply(const request::add_proxy&) {
    if (auto* up = root().settings_up())
      up->show_proxy(std::nullopt, -1);
  }
  void apply(const request::edit_proxy& one) {
    if (auto* up = root().settings_up(); up && one.index >= 0 && static_cast<std::size_t>(one.index) < proxies.size())
      up->show_proxy(proxies[static_cast<std::size_t>(one.index)], one.index);
  }
  // A profile saved: a new one added, or one changed -- and renamed in the
  // accounts that use it.
  void apply(const request::save_proxy_profile&) {
    auto* up = root().settings_up();
    auto* editor = up ? up->editor() : nullptr;
    if (!editor)
      return;
    auto typed = editor->proxy();
    if (!typed) {
      editor->say(typed.error(), true);
      return;
    }
    for (std::size_t i = 0; i < proxies.size(); ++i)
      if (proxies[i].name == typed->name && static_cast<int>(i) != editor->index) {
        editor->say("There is a proxy of that name already.", true);
        return;
      }
    const std::string name = typed->name;
    if (editor->index < 0) {
      proxies.push_back(std::move(*typed));
    } else {
      auto& kept = proxies[static_cast<std::size_t>(editor->index)];
      for (auto& one : saved)
        if (auto& uses = mux::config::proxy_in(one); uses == kept.name)
          uses = name;
      kept = std::move(*typed);
    }
    if (auto failed = this->write()) {
      editor->say(*failed, true);
      return;
    }
    this->reconnect_through(name);
    up->show_proxies(proxies);
  }
  // A profile deleted: the accounts that used it connect directly.
  void apply(const request::delete_proxy_profile&) {
    auto* up = root().settings_up();
    auto* editor = up ? up->editor() : nullptr;
    if (!editor || editor->index < 0 || static_cast<std::size_t>(editor->index) >= proxies.size())
      return;
    const std::string name = proxies[static_cast<std::size_t>(editor->index)].name;
    proxies.erase(proxies.begin() + editor->index);
    for (auto& one : saved)
      if (auto& uses = mux::config::proxy_in(one); uses == name) {
        uses.reset();
        this->reconnect(one);
      }
    (void)this->write();
    up->show_proxies(proxies);
  }


  void apply(const request::send_typed&) { this->send_message(root().main().line.text()); }
  // What is in the message field, to the chosen chat -- a new message, an
  // answer to one, or one edited -- and the field emptied.
  void send_message(std::string text) {
    auto& screen = root().main();
    const auto blank = [](unsigned char c) { return std::isspace(c) != 0; };
    if (!screen.chosen || std::ranges::all_of(text, blank))
      return;
    const mux::conversation_id to = *screen.chosen;
    std::visit(mux::overloaded{[&](const compose::plain&) { ask.send(to, std::move(text)); },
                               [&](const compose::reply& one) {
                                 if (ask.demo)
                                   ask.send(to, std::move(text));
                                 else
                                   net->send(to, std::move(text), one.id);
                               },
                               [&](const compose::edit& one) {
                                 if (ask.demo)
                                   box->push(mux::change_t{
                                       mux::change::message_edited{to, one.id, mux::body{std::move(text), std::nullopt}}});
                                 else
                                   net->edit(to, one.id, std::move(text));
                               }},
               composing);
    composing = compose::plain{};
    screen.line.show_context(std::nullopt);
    screen.line.clear();
    this->keep_draft(to, std::string());
  }
  // Another account's chats listed: the drawer goes back, and no chat is
  // chosen.
  void apply(const request::switch_account& one) {
    auto& screen = root().main();
    screen.current = mux::account_id{mux::ui::protocol_of(one.address), one.address};
    screen.chosen.reset();
    root().close_drawer();
    this->refresh();
  }
  void apply(const request::pop_panel&) {
    pending_login.reset();
    root().back_panel();
    this->refresh();
  }
  void apply(const request::settings_home&) {
    if (auto* up = root().settings_up())
      up->show_home();
  }
  void apply(const request::settings_animations&) {
    if (auto* up = root().settings_up())
      up->show_animations();
  }

  // How much moves, from now on and in the file.
  void set_motion(std::string level) {
    skiff::paint::motionLevel() = motion_of(level);
    root().show_motion(level);
    motion = std::move(level);
    (void)this->write();
  }

  void switch_form(void (adding::*to)()) {
    auto* up = root().open_panel();
    if (!up)
      return;
    pending_login.reset();
    std::visit(
        [to](accounts& panel) {
          if (auto* pane = panel.adding())
            (pane->*to)();
        },
        *up);
  }

  // A new account: saved, and started; the panel waits to hear how it went.
  template <class Form>
  void add(Form& form) {
    auto typed = form.account();
    if (!typed) {
      form.say(typed.error(), true);
      return;
    }
    mux::config::account_t account{std::move(*typed)};
    mux::config::proxy_in(account) = std::exchange(new_proxy, std::nullopt);
    const std::string address = mux::config::address_of(account);
    if (this->find(address) != saved.end()) {
      form.say("That account is already here.", true);
      return;
    }
    saved.push_back(account);
    if (auto failed = this->write()) {
      form.say(*failed, true);
      return;
    }
    net->add(account, proxies);
    pending_login = address;
    form.say("Connecting…", false);
  }

  // An account's settings changed: the old one stops, and the new one, on
  // or off as the old one was, takes its place in the list.
  template <class Form>
  void edit(Form& form) {
    auto typed = form.account();
    if (!typed) {
      form.say(typed.error(), true);
      return;
    }
    mux::config::account_t account{std::move(*typed)};
    const std::string address = mux::config::address_of(account);
    const std::string was = form.editing.value_or(address);
    const auto old = this->find(was);
    if (old == saved.end())
      return;
    if (address != was && this->find(address) != saved.end()) {
      form.say("That account is already here.", true);
      return;
    }
    // What the form does not show is kept: on or off, the proxy, receipts.
    mux::config::enabled_of(account) = mux::config::enabled_of(*old);
    mux::config::proxy_in(account) = mux::config::proxy_of(*old);
    mux::config::read_receipts_in(account) = mux::config::read_receipts_in(*old);
    mux::config::send_typing_in(account) = mux::config::send_typing_in(*old);
    // And a Matrix session: the same user on the same homeserver goes on
    // with the device it has, rather than logging in as a new one at every
    // Save.
    if (auto* now = std::get_if<mux::config::matrix_account>(&account))
      if (const auto* before = std::get_if<mux::config::matrix_account>(&*old);
          before && before->user_id == now->user_id && before->homeserver == now->homeserver &&
          before->password == now->password) {
        now->access_token = before->access_token;
        now->device_id = before->device_id;
      }
    // Nothing changed: saved as it is, and the connection left alone.
    const bool same = account == *old;
    *old = account;
    const auto failed = this->write();
    if (!same) {
      net->remove(was);
      if (mux::config::enabled_of(account))
        net->add(account, proxies);
    }
    // The form is rebuilt from what was saved: `form` is gone after this.
    auto& panel = this->show_account(address);
    if (auto* editor = panel.editor())
      editor->say(failed ? *failed : std::string("Saved."), failed.has_value());
  }

  void flip_enabled(const std::string& address) {
    const auto found = this->find(address);
    if (found == saved.end())
      return;
    bool& enabled = mux::config::enabled_of(*found);
    enabled = !enabled;
    if (enabled)
      net->add(*found, proxies);
    else
      net->remove(address);
    this->save_from_accounts();
    this->refresh();
  }

  void remove(const std::string& address) {
    if (std::erase_if(saved, [&](const auto& one) { return mux::config::address_of(one) == address; }) == 0)
      return;
    net->remove(address);
    this->save_from_accounts();
    this->refresh();
  }

  // Written, unless the file there could not be read: that one is the
  // user's to look at, not to lose.
  [[nodiscard]] std::optional<std::string> write() {
    if (ask.demo)
      return std::nullopt;  // the demo keeps nothing
    if (config_error)
      return "Not saved: " + *config_error;
    auto file = mux::config::file_of(saved);
    file.motion = motion;
    if (!proxies.empty())
      file.proxies = proxies;
    file.theme = mux::config::word_of(theme);
    file.accent = mux::config::word_of(accent);
    file.renderer = mux::config::word_of(renderer);
    file.cache = limits;
    file.sending = sending;
    if (!muted.empty()) {
      std::vector<mux::config::muted_chat> kept;
      for (const auto& one : muted)
        kept.push_back({one.account.address, one.id});
      file.muted = std::move(kept);
    }
    if (auto done = mux::config::save(config_path, file); !done)
      return "Not saved: " + done.error();
    return std::nullopt;
  }
  void save_from_accounts() {
    if (auto failed = this->write())
      if (auto* up = root().open_panel())
        std::visit([&](accounts& panel) { panel.say(*failed); }, *up);
  }
};

}  // namespace

int main(int argc, char** argv) {
  mailbox_type box{wake_window{}};
  mux::model model;
  network net;
  net.box = &box;

  // `mux --demo`: fake accounts and conversations, no network, nothing kept.
  const bool demo = argc > 1 && std::string_view(argv[1]) == "--demo";
  const std::filesystem::path config_path = mux::config::default_path();
  mux::config::file saved;
  std::optional<std::string> config_error;
  std::optional<std::string> config_note;
  if (demo) {
    saved = mux::config::file_of(fake::accounts());
    fake::fill(model);
  } else if (auto loaded = mux::config::load(config_path))
    saved = std::move(*loaded);
  else {
    // Not a file this mux can read -- one of an older mux, most likely: kept
    // aside, where it can be looked at, and a new one begun, so that what is
    // set from now on is saved. Refusing to save instead lost every change
    // without a word.
    std::filesystem::path aside = config_path;
    aside += ".unreadable";
    std::error_code moved;
    std::filesystem::rename(config_path, aside, moved);
    if (moved) {
      config_error = loaded.error();
    } else {
      config_note = std::format("{}\n\nIt was kept as {}, and a new one begun.", loaded.error(), aside.string());
    }
    std::println(std::cerr, "[mux] {}", loaded.error());
  }

  // Accounts named on the command line, for this run only.
  std::vector<mux::config::account_t> extra;
  if (argc > 1 && !demo) {
    const char* password = std::getenv("MUX_PASSWORD");
    if (!password) {
      std::println(std::cerr, "accounts on the command line take their password from MUX_PASSWORD, which is not set");
      return 2;
    }
    for (int at = 1; at < argc; ++at)
      extra.push_back(mux::config::account_from(argv[at], password));
  }

  const auto proxies = saved.proxies.value_or(std::vector<mux::config::proxy_settings>{});
  for (const auto& one : mux::config::accounts_of(saved))
    if (mux::config::enabled_of(one) && !demo)
      net.start(one, proxies);
  for (const auto& one : extra)
    net.start(one, proxies);
  net.thread = std::thread([&net] {
    try {
      net.loop.run_forever();
    } catch (const std::exception& failed) {
      std::println(std::cerr, "[mux] the network stopped: {}", failed.what());
    }
  });

  // The theme first: what is made takes its colours from it.
  mux::ui::use_theme(mux::config::theme_of(saved.theme), mux::config::accent_of(saved.accent));
  app program;
  program.box = &box;
  program.model = &model;
  program.net = &net;
  // A pill's avatar in a message's text: the one of what it names.
  skiff::scene::pillPainter() = {+[](void*, skia::SkCanvas* canvas, const skia::SkRect& disc, std::string_view target,
                                     float alpha) {
                                   const auto at = target.find("#/");
                                   const std::string_view id = at == std::string_view::npos ? target : target.substr(at + 2);
                                   mux::ui::draw_avatar(canvas, disc, id, id, alpha);
                                 },
                                 nullptr};
  // A link pressed in a message's text: routed as a link is.
  skiff::scene::linkOpener() = {+[](void* self, std::string_view url) {
                                  static_cast<app*>(self)->ask.open_url(std::string(url));
                                },
                                &program};
  program.ask.net = &net;
  program.ask.demo = demo;
  program.ask.box = &box;
  program.config_path = config_path;
  program.saved = mux::config::accounts_of(saved);
  program.motion = saved.motion;
  program.theme = mux::config::theme_of(saved.theme);
  program.accent = mux::config::accent_of(saved.accent);
  program.renderer = mux::config::renderer_of(saved.renderer);
  program.limits = saved.cache.value_or(mux::config::cache_limits{});
  if (!demo)
    program.load_drafts();
  program.sending = saved.sending.value_or(mux::config::sending_settings{});
  program.apply_limits();
  program.proxies = proxies;
  for (const auto& one : saved.muted.value_or(std::vector<mux::config::muted_chat>{}))
    program.muted.insert({{mux::ui::protocol_of(one.account), one.account}, one.conversation});
  skiff::paint::motionLevel() = motion_of(saved.motion);
  program.root().show_motion(saved.motion.value_or("full"));
  program.config_error = std::move(config_error);
  program.refresh();

  if (config_note)
    program.root().show_message("The accounts file could not be read", *config_note);
  const int code = mux::host::run(
      program, {.software = program.renderer == mux::config::renderer_t{mux::config::renderer::software{}}});
  net.thread.join();
  return code;
}
