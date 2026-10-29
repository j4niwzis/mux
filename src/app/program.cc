// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.program: what the program does to the window between events --
// the class, its state and what it does, declared; what it does is defined
// in the program_*.cc beside this, a part each.
export module mux.app.program;

import std;
import mux.vault;
import splice;
import knot;
import skia;
import mux.core;
import mux.config;
import mux.net;
import mux.media;
import mux.platform.faces;
import mux.platform.audio;
import skiff.platform.dialogs;
import skiff.platform.events;
import mux.platform.notifications;
import mux.platform.push;
import mux.platform.system;
import mux.protocols;
import mux.ui;
import mux.ui.proto;
import mux.app.proto;
import skiff.paint;
import skiff.scene;
import skiff.bind;
import mux.app.network;
import mux.app.workers;
import mux.app.demo;
import mux.app.store;
import mux.app.requests;
import mux.plugins.app;
import mux.app.words;
import mux.app.services;
import mux.app.kept;
import mux.app.search;
import mux.app.pictures;
import mux.app.drafts;
import mux.app.reading;
import mux.app.outbox;
import mux.app.settings;
import mux.app.menu;
import mux.app.notices;
import mux.app.history;
import mux.app.verification;
import mux.app.rooms;
import mux.app.room_card;
import mux.app.preferences;
import mux.app.manage;
import mux.app.looks;
import mux.app.jumps;
import mux.app.chat_view;
import mux.app.chat_list;
import mux.app.emoji;
import mux.app.accounts;
import mux.app.local_data;
import mux.logic.links;

export namespace mux::app {

// What a press is answered with where it is made -- in a release build, as
// the window routes it, every node with its type: the settings model the
// window's scopes edit, and the program's sink for what nothing in the
// window takes. The program says them once it has them; till then nothing
// is answered there, and a press is delivered along its path instead.
struct app;
struct press_target {
  kept_model* model = nullptr;
  app* sink = nullptr;
};
inline press_target& press_target_now() {
  static press_target kept;
  return kept;
}
}  // namespace mux::app

// What the window's routing starts with, found by its root's type: so in
// a namespace that lookup looks in. In a release build that is the
// window's own, mux::ui, named by the composed type; outside one the
// window is a skiff::compose::Held and names nothing of mux's, so there it
// is Held's namespace -- for the window alone, not any other held root.
namespace mux::app {
inline auto window_carry() {
  const press_target& now = press_target_now();
  return skiff::bind::carryFrom(now.model, now.sink);
}
}  // namespace mux::app
export namespace mux::ui {
template <class Root>
  requires(!skiff::scene::kErasedCombinators && std::same_as<Root, ::mux::app::window_type>)
auto startCarry(Root&) {
  return ::mux::app::window_carry();
}
}  // namespace mux::ui
export namespace skiff::compose {
template <class Root>
  requires(skiff::scene::kErasedCombinators && std::same_as<Root, ::mux::app::window_type>)
auto startCarry(Root&) {
  return ::mux::app::window_carry();
}
}  // namespace skiff::compose

export namespace mux::app {

// What the program does to the window between events.
// What the program does to the window between events -- on what the
// accounts file keeps, its base.
struct app : kept_settings {
  // Made in the theme's colours, which are in place before the window is.
  // Made in the theme's colours and the window's look as the program
  // starts: the window, made here, is shown in them.
  app(const mux::ui::palette& theme_colours, const mux::ui::window_look_t& window);
  // Keep construction, exception cleanup and UI teardown in the owning
  // module unit; importers must not instantiate the window's destructors.
  ~app();
  // -- the parts: each owns its state, and reaches the rest through what
  // they share
  services shared;
  search_part search{shared};
  pictures_part pictures{shared};
  drafts_part drafts{shared};
  reading_part reading{shared};
  outbox_part outbox{shared, drafts, this->sending()};
  menu_part menu{&shared, &outbox, &pictures};
  settings_part settings{shared, *this, pictures};
  notices_part notices{shared};
  history_part paging{shared};
  verification_part verification{shared};
  rooms_part rooms{shared};
  room_card_part room_card{shared};
  preferences_part preferences{shared, *this};
  manage_part manage{shared, *this};
  looks_part looks{shared, *this, colours};
  jumps_part jumps{shared};
  chat_view_part chat_view{shared};
  chat_list_part chat_list{shared, *this, colours};
  emoji_part emoji{&shared};
  accounts_part accounts_screen{shared, *this};
  local_data_part local_data{shared, *this};
  // Each plugin's part that says it has one (contract::program_parts), after
  // the program's own, in the plugins' order.
  // Their types named without calling for them: a member function's deduced
  // type is not known while the class is still being read.
  template <class List>
  struct plugin_parts_list;
  template <class... Features>
  struct plugin_parts_list<plugin_list<Features...>> {
    using type = std::tuple<decltype(extend(contract::program_parts{}, type_tag<Features>{}, std::declval<services&>()))...>;
    static type made(services& shared) { return type{extend(contract::program_parts{}, type_tag<Features>{}, shared)...}; }
  };
  using plugin_parts_t = plugin_parts_list<composition::providers<contract::program_parts, enabled_plugins>>;
  typename plugin_parts_t::type plugin_parts = plugin_parts_t::made(shared);
  // A change the accounts made, to each plugin's part that takes it -- a
  // call's signalling, the calls part's -- and their time between events.
  template <class Part, class Change>
    requires requires(Part& part, const Change& one) { part.take(one); }
  static void take_change(Part& part, const Change& one) {
    part.take(one);
  }
  template <class Part, class Change>
  static void take_change(Part&, const Change&) {}
  template <class Part>
    requires requires(Part& part) { part.tick(); }
  static void tick_part(Part& part) {
    part.tick();
  }
  template <class Part>
  static void tick_part(Part&) {}
  void plugins_take(const mux::change_t& one) {
    spl::visit([&](const auto& each) { std::apply([&](auto&... part) { (take_change(part, each), ...); }, plugin_parts); }, one);
  }
  // Files picked for a plugin (one asked for them), not the outbox.
  template <class Part>
  static bool part_took_files(Part& part, const std::vector<std::string>& paths, bool dropped) {
    if constexpr (requires { { part.took_files(paths, dropped) } -> std::same_as<bool>; })
      return part.took_files(paths, dropped);
    else
      return false;
  }
  bool plugins_took_files(const std::vector<std::string>& paths, bool dropped) {
    return std::apply([&](auto&... part) { return (part_took_files(part, paths, dropped) || ...); }, plugin_parts);
  }
  void plugins_tick() {
    std::apply([](auto&... part) { (tick_part(part), ...); }, plugin_parts);
  }
  // Files chosen in the dialog, or dropped on the window: to the outbox.
  void files_given(std::vector<std::string> paths, bool dropped) {
    if (plugins_took_files(paths, dropped))
      return;
    if (manage.took_files(paths, dropped)) return;
    if (looks.took_files(paths, dropped))
      return;
    if (profile_files_given(paths, dropped)) return;
    outbox.files_given(std::move(paths), dropped);
  }
  // Where Save As… was asked to put what it saves: to the pictures part.
  void save_path_chosen(std::string path) { pictures.save_to(std::move(path)); }
  // The file dialog not shown -- on Linux, SDL asks xdg-desktop-portal for
  // it, else zenity: with neither, the paperclip did nothing at all.
  void dialog_failed(std::string why) {
    shared.notice("The file dialog could not be opened",
                        "The system gave no file dialog (" + why +
                            "). On Linux it comes from xdg-desktop-portal with a backend (-gtk, -gnome, -kde or -wlr), or "
                            "from zenity: install one of them. Meanwhile files can be dropped on the window, and pictures pasted.");
  }
  // What the parts share, pointed at the program's own: once the program
  // is given its model, network and mailbox.
  void wire();
  // A request, to the part that takes it -- the first with an apply for
  // it, as overload resolution finds -- and to the program's own where
  // none does.
  // Offered to a part: done by it -- and what it asks for in turn, where
  // its apply() returns that, done too.
  template <class Part, class Request>
    requires requires(Part& part, const Request& one) {
      { part.apply(one) } -> std::same_as<void>;
    }
  bool offer(Part& part, const Request& one) {
    part.apply(one);
    return true;
  }
  template <class Part, class Request>
    requires requires(Part& part, const Request& one) { part_apply(part, one); }
  bool offer(Part& part, const Request& one) {
    part_apply(part, one);
    return true;
  }
  // What a part asks for in turn, in its own type, given to the program
  // to do at once: where the part decides it in a visit -- a chat's
  // protocol -- its type is static there.
  struct taker {
    app* program = nullptr;
    template <class R>
    void operator()(const R& asked) const {
      program->take(asked);
    }
  };
  template <class Part, class Request>
    requires requires(Part& part, const Request& one, const taker& asked) { part.apply(one, asked); }
  bool offer(Part& part, const Request& one) {
    part.apply(one, taker{this});
    return true;
  }
  template <class Part, class Request>
  bool offer(Part&, const Request&) {
    return false;
  }

 public:
  // What the window answers, and what the program's parts ask in turn:
  // done at once, as it is taken -- the program is the window's sink. A
  // press is answered after the handler that made it has returned, and
  // what is done to the window is a model's edit, read by the bindings
  // after the event: nothing pressed is gone under its own answer.
  bool event_requests_taken = false;
  template <class E>
  void take(const E& one) {
    event_requests_taken = true;
    this->route(one);
  }
  template <class... E>
  void take(const spl::variant<E...>& one) {
    spl::visit([this](const auto& each) { this->take(each); }, one);
  }
  template <class E>
  void take(const std::optional<E>& one) {
    if (one)
      this->take(*one);
  }
  // A protocol's "nothing to ask": nothing.
  void take(const mux::proto::part::no_request&) {}
  template <class Part, class Request>
  static constexpr bool takes = requires(Part& part, const Request& one) { part_apply(part, one); } ||
                                requires(Part& part, const Request& one) { part.apply(one); } ||
                                requires(Part& part, const Request& one, const taker& asked) { part.apply(one, asked); };
  // Whether a protocol lists a request among its own (requests_of): what
  // the runtime walks outside a release build serve it by.
  template <class Request, class... Listed>
  static consteval bool listed_in(mux::proto::request_list<Listed...>) {
    return (std::same_as<Request, Listed> || ...);
  }
  template <class Request, class... Tags>
  static consteval bool listed_by_a_protocol(mux::protocol_list<Tags...>) {
    return (listed_in<Request>(decltype(mux::proto::protocol_requests_of(std::declval<const mux::state_of<Tags>&>())){}) || ...);
  }
  // A protocol's own request: done as its program glue says (mux.app.proto).
  // Listed by its protocol too, or outside a release build nothing serves it:
  // a press on a room's settings page (manage_room) was dropped so, every
  // Expand and Edit doing nothing.
  template <class Request>
    requires requires(app& self, const Request& one) { program_asked(self, one); }
  void route(const Request& one) {
    static_assert(listed_by_a_protocol<Request>(mux::protocols{}),
                  "a request a protocol's program glue takes (program_asked), missing from its requests_of: "
                  "outside a release build nothing would serve it");
    program_asked(*this, one);
  }
  // The program's parts, in the order a request is offered to them; what
  // none of them takes, the program's own.
  auto parts() {
    return std::tuple_cat(
        std::tie(search, pictures, reading, outbox, settings, menu, notices, paging, verification,
                 rooms, room_card, preferences, manage, looks, jumps, chat_view, chat_list, emoji, accounts_screen, local_data),
        std::apply([](auto&... theirs) { return std::tie(theirs...); }, plugin_parts));
  }
  template <class Request>
  void route(const Request& one) {
    std::apply(
        [&](auto&... part) {
          static_assert((takes<std::remove_reference_t<decltype(part)>, Request> || ...) || takes<app, Request>,
                        "a request no part of the program takes");
          (void)((offer(part, one) || ...) || offer(*this, one));
        },
        parts());
  }

#ifdef SKIFF_ERASED_WALKS
  // Outside a release build, what the window's presses ask for reaches the
  // program through skiff's runtime walks (skiff.bind:runtime), which take a
  // request by its type from this table: every type the program takes --
  // its parts' apply()s, with a taker or without, their part_apply()s, its
  // own apply()s, each protocol's requests -- found while compiling
  // (skiff::bind::takes), each to route() as a release build takes it.
  template <class R>
  static void routed(void* self, const void* one) {
    static_cast<app*>(self)->route(*static_cast<const R*>(one));
  }
  template <class... R>
  void served_each(std::vector<skiff::bind::rt::Served>& out, skiff::model::Types<R...>) {
    (out.push_back({skiff::model::rt::keyOf<R>(), this, &routed<R>}), ...);
  }
  template <class... R>
  void served_each(std::vector<skiff::bind::rt::Served>& out, mux::proto::request_list<R...>) {
    served_each(out, skiff::model::Types<R...>{});
  }
  template <class Part>
  struct part_apply_tag {};
  template <class Part>
  static constexpr bool probe_part_apply() {
    return requires(Part& part) { part_apply(part, skiff::bind::takes::loophole::Probe<part_apply_tag<Part>>{}); };
  }
  template <class Part>
  using part_applied = typename skiff::bind::takes::loophole::Read<part_apply_tag<Part>, probe_part_apply<Part>()>::type;
  template <class Part>
  void served_by_part(std::vector<skiff::bind::rt::Served>& out, Part&) {
    served_each(out, skiff::bind::takes::Applied<Part>{});
    served_each(out, skiff::bind::takes::AppliedWith<Part, taker>{});
    served_each(out, part_applied<Part>{});
  }
  template <class... Tags>
  void served_protocols(std::vector<skiff::bind::rt::Served>& out, mux::protocol_list<Tags...>) {
    (served_each(out, decltype(mux::proto::protocol_requests_of(std::declval<const mux::state_of<Tags>&>())){}), ...);
  }

 public:
  // What skiff's runtime walks hand a request to (rt::servedBy).
  std::vector<skiff::bind::rt::Served> servedTable() {
    std::vector<skiff::bind::rt::Served> out;
    std::apply([&](auto&... part) { (served_by_part(out, part), ...); }, parts());
    served_each(out, skiff::bind::takes::Applied<app>{});
    served_protocols(out, mux::protocols{});
    return out;
  }
#endif

  using accounts = mux::ui::accounts_panel;

  mailbox_type* box = nullptr;
  // What wakes the window: as main made it, for the mailbox.
  wake_window wake;
  // The system's dialogs, put over the window as it is made.
  skiff::platform::dialogs::dialogs system_dialogs;
  mux::model* model = nullptr;
  network* net = nullptr;
  // A room the user made, to be shown as soon as the model has it.
  std::optional<mux::conversation_id> made_room_;
  // What plays voice messages: handed to the window and to the parts.
  mux::platform::audio::speaker speaker;
  // The theme's colours: handed to the window, which is made in them --
  // made again in new ones when the theme changes (rebuild_in_theme).
  mux::ui::palette colours;
  std::optional<mux::config::style_settings> active_style;
  actions ask;
  // What the window is made with: the program's own, which its nodes point
  // at for as long as it is.
  mux::ui::ui_needs window_needs{.sound = &speaker, .colours = &colours, .emoji = &shared.emoji,
                                 .looks = &shared.looks, .paint = &shared.paint, .shared = &shared.ui};
  window_scene scene_storage{window_needs};
  skiff::scene::Scene<window_type>& scene = scene_storage.get();

  // -- what the host asks
  skiff::scene::Scene<window_type>& window();
  // What the host draws the window with.
  mux::ui::mux_paint& painting() { return shared.paint; }

  void woken();
  // What the network brought, not applied yet: taken a slice of time a
  // frame while the window is on screen (take_arriving). Applied at once,
  // a wake's thousands of events -- every room's gap caught up on -- held
  // the UI's thread two seconds at a time, and a phone called the app not
  // responding.
  std::deque<mux::change_t> arriving_;
  void take_arriving(std::chrono::milliseconds most);
  // A surge of it, as the log tells it: since the queue was last empty, how
  // many came, how deep it got, how many slices took it and how long.
  struct arriving_surge {
    std::size_t came = 0, deepest = 0, slices = 0;
    double ms = 0.0;
  } surge_;
  // A link's message gone to, and a thread's answer to be scrolled to once
  // its panel shows it.
  void go_to_linked(const mux::conversation_id& in, const std::string& event);
  bool open_in_thread(const mux::conversation& chat, const std::string& id);
  std::optional<std::pair<mux::conversation_id, std::string>> linked_;
  std::optional<std::string> thread_target_;

  // -- messages on disk: every change to one written as it is now
  message_store store;

  std::set<mux::conversation_id> members_fetched;

  // A Matrix session given: kept with its account, for the next start.

  // A link pressed in a message's text: routed as a link is.
  void open_link(std::string url) { this->take(mux::ui::request::open_url{std::move(url)}); }
  void before_frame();
  [[nodiscard]] double wake_in() const;
  void after_event();
  // The window bound to the model, and the pages with the model's widgets
  // it was last walked whole with.
  skiff::bind::Binding<kept_model> window_binding;
  // What the window shows that the program opens and closes -- the dialogs'
  // facts -- and the binding the dialogs read it through.
  mux::ui::shown_model showing{mux::ui::shown_root{}};
  skiff::bind::Binding<mux::ui::shown_model> showing_binding;
  // What is shown read by the dialogs and layers bound to it; a menu just
  // made, given the keys.
  void refresh_shown();
  [[nodiscard]] bool accounts_up();
  [[nodiscard]] bool page_over_chats();
  // The keys asked for the chat's field since they were last let go.
  bool keys_asked_free = false;
  // And to the chats, the window's other model.
  skiff::bind::Binding<mux::chats_model> chats_binding;
  void show_chats_now();
  void take_page_input();
  void settle_model();
  void show_looks();

  // What the window asks of the program by its hooks (skiff.platform.window).
  [[nodiscard]] int interface_scale() const { return this->appearance().interface_scale; }
  // Shaped as a phone's for the window's long press: as the layout chosen
  // in Settings says, the size where it is automatic.
  [[nodiscard]] bool phone_shaped(float width, float height) const {
    return mux::ui::window_shape{width, height, this->appearance().layout}.phone();
  }
  void touch_changed(bool on) {
    shared.by_touch = on;
    mux::ui::typed_by_touch() = on;
  }
  [[nodiscard]] std::optional<skia::SkPoint> take_press_again() { return std::exchange(shared.press_again, std::nullopt); }
  // The faces the window draws text with first (skiff.platform.fonts).
  using faces = mux::platform::faces;
  void closing();
  void backgrounded();
  // Drafts: in the screen, and on disk in one small file, written anew
  // when one changes.

  // -- the window
  window_type& root();

  void show_conversations();



  // Everything brought up to date with the model: each panel by its own
  // overload.
  // `from`: who asked -- said on stderr where MUX_TRACE_FRAMES is set, to
  // find what rebuilds the window when nothing should.
  void refresh(std::source_location from = std::source_location::current());
  // Each thing the window shows of the settings, brought up to date.
  void note_spaces();
  // Every chat of every account.
  [[nodiscard]] auto all_chats() const {
    return model->accounts() | std::views::values | std::views::transform([](const auto& account) -> const auto& { return account.conversations; }) | std::views::join | std::views::values;
  }
  // What is kept, applied: the settings read at the start -- or, where local
  // data is encrypted, once it is unlocked -- and the accounts started.
  void begin(const mux::config::file& saved, std::vector<mux::config::account_t> extra, bool demo,
             std::optional<std::string> error);
  // Local data encrypted and locked: the unlock screen, and what the start
  // would have done kept until it is opened.
  void lock(std::vector<mux::config::account_t> extra, bool demo);

  void apply(const request::choose& one);

  // A chat opened: read up to its last message from someone else, and the
  // people in it told so where read receipts are on.
  // The user's own read position: kept here always -- in the model and on
  // disk -- and told to the server only where the account's privacy lets it.

  // The chosen chat left: a Matrix room here; XMPP rooms are not there yet.
  void apply(const request::leave_chat&);
  void apply(const request::open_leave_space&);
  void apply(const request::leave_space&);
  void apply(const request::close_leave_space&);
  // Out of the chat open, back to the chats: what was written kept as its draft.
  void apply(const request::close_chat&);
  void apply(const request::back&);
  void apply(const request::open_drawer&);
  void apply(const request::close_drawer&);
  void apply(const request::banner_pressed&);
  void apply(const request::head_pressed&);
  void apply(const request::card_action&);
  void apply(const request::quit&);
  void apply(const request::toggle_info&);
  void apply(const request::jump_to_end&);
  // The way down, as the chat's list said it: kept, for its corner to read.
  // Whether the way down is wanted, as the list says; its count is the
  // chat's unread, kept before each frame (before_frame).
  void apply(const request::way_down& one) {
    mux::ui::change_shown<mux::ui::way_down_shown>(showing, [&](mux::ui::way_down_shown& now) { now.wanted = one.wanted; });
  }
  void apply(const request::return_to_chat&);
  void go_live(const mux::conversation_id& in);

  // A message's menu, and what is chosen from it.
  // -- files to send: chosen with the paperclip, or dropped on the window
  // Files given: read, a picture known by its bytes; a picture dropped on
  // the window written anew from its pixels -- nothing of its file, its
  // metadata among it, goes with it -- and named image.<its type>. Then the
  // send box, with what was waiting in it before.
  // Sent: each file, the caption with the first; the box closed.

  // A message swiped to the left: answered, as its menu's Reply does.
  // A reaction: the user's own put where it is not, taken back where it
  // is -- shown at once, and told to the server.
  // A picture or a file saved into Downloads, from the menu.
  // A link pressed: to a user, a room or a message, in here -- matrix.to,
  // matrix: and xmpp: links -- and anywhere else, in the browser.
  void apply(const request::open_url& one);
  void apply(const request::copy_log&);

  // A link followed, as where it leads says: a chat opened -- and a message
  // in it jumped to -- a person's page, a word said, or a room joined, and
  // opened when it comes.
  void follow(const mux::logic::link_t& where);
  void open_chat(const mux::conversation_id& which, const std::optional<std::string>& event);
  void go_to_message(const mux::conversation_id& in, std::string id, std::optional<std::string> fragment,
                     std::optional<std::string> from = std::nullopt);
  // Older messages of a chat: from the disk while it has some from before
  // the oldest in memory, from the server past that.
  // A window around a message jumped to, and a window paged forward.
  void apply(const request::resize_sidebar& one);
  void apply(const request::escape&);
  void apply(const request::copy_selected&);
  void apply(const request::pressed_at& one);
  // A member written to: their direct chat, where there is one already.
  void apply(const request::message_person& one);
  void apply(const request::jump_to_message& one);

  // A sender pressed in the messages: their page, in the chat's info.
  void apply(const request::refresh_profile& one);
  void apply(const request::save_profile& one);
  void apply(const request::save_presence& one);
  void apply(const request::pick_profile_banner& one);
  void apply(const request::remove_profile_banner& one);
  bool profile_files_given(const std::vector<std::string>& paths, bool dropped);
  std::optional<account_id> picking_profile_banner_;
  void apply(const request::open_member_info& one);
  void apply(const request::not_implemented& one);
  void apply(const request::close_notice&);
  void apply(const request::close_person_info&);
  // The person whose card is open, in which chat: shown again as what is
  // known of their keys comes.
  std::optional<std::pair<mux::conversation_id, std::string>> person_open_;
  // The notifications mux shows itself, for the host to put up; one pressed;
  // the window's focus -- the notices part's.
  using toast_due = notices_part::toast_due;
  using toast_card = mux::ui::toast_card_t;
  static auto make_toast(const mux::ui::palette& colours, std::string key, std::string title, std::string text) {
    return mux::ui::toast_card(colours, std::move(key), std::move(title), std::move(text));
  }
  [[nodiscard]] std::vector<toast_due> take_toasts() { return notices.take_toasts(); }
  void open_notified(const mux::conversation_id& chat) { this->open_chat(chat, std::nullopt); }
  void focus_changed(bool on) { notices.focus_changed(on); }
  // Whether the window is on screen: not hidden, minimised, covered or
  // suspended -- a phone's screen off, another app over it. Off it, only
  // the connections go on: the model kept up and notifications given, and
  // nothing else -- the window not brought up to date, no pictures asked
  // for, nothing marked read, no frames. Brought up to date as it comes back.
  bool on_screen = true;
  bool refresh_waiting_ = false;
  // How long a frame gives to what arrived: the rest waits for the next.
  static constexpr std::chrono::milliseconds kArrivingSlice{12};
  void shown_changed(bool now) {
    if (now == on_screen)
      return;
    on_screen = now;
    if (now && std::exchange(refresh_waiting_, false))
      this->refresh();
  }
  void apply(const request::copy_text& one);
  void apply(const request::text_key& one);
  void apply(const request::text_formatting& one);
  void apply(const request::close_text_menu&);
  void apply(const request::give_passphrase&);
  // The account whose room keys a passphrase was asked for.
  std::optional<mux::account_id> keys_of;
  void apply(const request::resize_info& one);
  // ← on the accounts page: from an account's pages to the list, from the
  // list to the chats.
  void apply(const request::open_replacement&);
  // A limit halved or doubled, within its bounds: kept, and in force at once.
  // What is kept on disk, gone: the stored messages and the pictures, and
  // the pictures in memory, to be fetched again as they are wanted.
  // The theme or the renderer chosen: kept, for the next start.
  // A theme chosen: its colours in place, and the window made again in them,
  // as it was -- the chats, the one chosen, the widths -- with Settings open
  // where it was.
  // An accent chosen: the same as a theme, over it.
  // The theme and accent now chosen put in place, and the window made again
  // in them, as it was, with Settings open on Appearance.
  void rebuild_in_theme();


  // What is in the message field, to the chosen chat -- a new message, an
  // answer to one, or one edited -- and the field emptied.
  // Another account's chats listed: the drawer goes back, and no chat is
  // chosen.
  void apply(const request::switch_account& one);
  void apply(const request::pop_panel&);

  // Last: join decoding and disk-reading jobs before destroying the store,
  // scene or parts their captures reach through shared. C++ destroys members
  // in reverse declaration order, even while construction is unwinding.
  workers work;
};

}  // namespace mux::app
