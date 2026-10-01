// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:accounts -- The accounts.
export module mux.ui:accounts;

import std;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes.flow;
import skiff.nodes.scroll;
import skiff.nodes.text;
import skiff.widgets.button;
import skiff.widgets.sliderbar;
import skiff.widgets.textbox;
import mux.core;
import mux.config;
import :base;
import :icons;
import :avatars;
import :controls;
import :themes;
import :forms;
import :add_account;
import :info;

export namespace mux::ui {

// ---- the accounts -------------------------------------------------------------------

// What the model says of a saved account, in a few words, and whether that
// is a failure.
[[nodiscard]] inline std::pair<std::string, bool> state_of(const config::account_t& one, const model& now) {
  const std::string& address = config::address_of(one);
  if (!config::enabled_of(one))
    return {"off", false};
  const auto found = now.accounts().find(account_id{protocol_of(address), address});
  if (found == now.accounts().end())
    return {"offline", false};
  bool failed = false;
  std::string said = splice::visit(splice::overloaded{[](const connection::offline&) { return std::string("offline"); },
                                           [](const connection::connecting&) { return std::string("connecting…"); },
                                           [](const connection::online&) { return std::string("online"); },
                                           [&failed](const connection::failed& why) {
                                             failed = true;
                                             return "failed: " + why.error;
                                           }},
                                found->second.state);
  return {std::move(said), failed};
}

// One account in the list: its address, protocol and state. A click shows
// its settings beside the list.
template <class Actions>
struct account_entry : nodes::Stack {
  Actions* actions = nullptr;
  std::string address;
  bool selected = false;
  struct parts_t {
    nodes::Text name;
    nodes::Text state;
  } parts;

  // Declared: its address over its protocol and state, on a plate lit
  // while it is the one chosen.
  account_entry(Actions* a, const config::account_t& saved, const model& now, bool is_selected)
      : actions(a), address(config::address_of(saved)), selected(is_selected),
        parts{.name = nodes::Text(address, 15.0f, text_colour, true), .state = nodes::Text("", 13.0f, dim_colour)} {
    this->setGap(4.0f);
    fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {7.0f, 16.0f, 7.0f, 16.0f}, .background = sidebar_colour, .selectedBackground = chosen_colour, .selected = selected});
    const auto [how, failed] = state_of(saved, now);
    parts.state.setText(std::format("{} · {}", config::protocol_name(saved), how));
    parts.state.setColour(failed ? error_colour : dim_colour);
    for (nodes::Text* each : {&parts.name, &parts.state}) {
      each->setElided(true);
      each->apply({.fillX = true});
    }
  }

  [[nodiscard]] bool acceptsInput() const { return true; }
  [[nodiscard]] bool onClick(float, float) {
    actions->select_account(address);
    return true;
  }
  [[nodiscard]] scene::Semantics semantics() const {
    scene::Semantics out;
    out.fRole = scene::semantic_role::list_item{};
    out.fLabel = address;
    out.fSelected = selected;
    out.fActions = {scene::semantic_action::focus{}, scene::semantic_action::activate{}};
    return out;
  }
};

// The chosen account: on or off, removed, and its own protocol's form.
template <class Actions>
struct account_editor : nodes::Stack {
  // Its address, then on or off and Remove, in a line.
  struct head_row : nodes::Stack {
    struct parts_t {
      nodes::Text heading;
      nodes::Text enabled_label{"On", 13.0f, dim_colour};
      widgets::Toggle<flip_account<Actions>> enabled;
      widgets::Button<remove_account<Actions>> remove;
    } parts;
    head_row(Actions* a, const config::account_t& saved)
        : parts{.heading = nodes::Text(config::address_of(saved), 20.0f, text_colour, true),
                .enabled = widgets::Toggle<flip_account<Actions>>(flip_account<Actions>{a, config::address_of(saved)}),
                .remove = widgets::Button<remove_account<Actions>>(
                    "Remove", remove_account<Actions>{a, config::address_of(saved)})} {
      this->setHorizontal();
      this->setGap(10.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY});
      parts.heading.setElided(true);
      parts.heading.apply({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
      parts.enabled_label.apply({.alignSelf = scene::align::kMiddle});
      parts.enabled.apply({.alignSelf = scene::align::kMiddle});
      parts.enabled.setOnNow(config::enabled_of(saved));
      parts.remove.apply({.width = 100.0f, .height = 32.0f});
    }
  };
  struct parts_t {
    head_row head;
    nodes::Text state{"", 13.0f, dim_colour};
    account_form<Actions> form;
  } parts;

  account_editor(Actions* a, const config::account_t& saved)
      : parts{.head = head_row(a, saved), .form = form_of(a, saved)} {
    fState.apply({.fill = true});
    this->setGap(6.0f);
    parts.state.setElided(true);
    parts.state.apply({.fillX = true, .margin = {0.0f, 0.0f, 14.0f, 0.0f}});
  }

  // What the model says of it now, kept current without touching the form.
  void show(const config::account_t& saved, const model& now) {
    const auto [how, failed] = state_of(saved, now);
    parts.state.setText(std::format("{} · {}", config::protocol_name(saved), how));
    parts.state.setColour(failed ? error_colour : dim_colour);
    parts.head.parts.enabled.setOn(config::enabled_of(saved));
  }

  void say(std::string text, bool error) {
    splice::visit([&](auto& one) { one.say(std::move(text), error); }, parts.form);
  }
};

// A line with a switch on its right: its text, and the switch.
template <class Act>
struct switch_row : nodes::Stack {
  struct parts_t {
    nodes::Text label;
    widgets::Toggle<Act> toggle;
  } parts;

  // Declared: the text taking the room, the switch at the end.
  switch_row(std::string text, Act what)
      : parts{.label = nodes::Text(std::move(text), 15.0f, text_colour), .toggle = widgets::Toggle<Act>(std::move(what))} {
    this->setHorizontal();
    this->setGap(16.0f);
    fState.apply({.fillX = true, .height = row_item<nothing>::kHeight, .padding = {0.0f, 20.0f, 0.0f, 20.0f}});
    parts.label.setElided(true);
    parts.label.apply({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
    parts.toggle.apply({.alignSelf = scene::align::kMiddle});
  }
};

// A page of an account's settings chosen from its list.
template <class Actions>
struct choose_account_page {
  Actions* actions = nullptr;
  int page = 0;
  void operator()() const { actions->account_page(page); }
};

// An account's pages, in place of the list of accounts once one is chosen: a
// line for each page of its settings, the one shown lit.
template <class Actions>
struct account_pages : nodes::Stack {
  using row = row_item<choose_account_page<Actions>>;
  struct parts_t {
    row connection;
    row privacy;
    row chats;
    row sessions;
    row proxy;
  } parts;

  explicit account_pages(Actions* a)
      : parts{.connection = row("Connection", {a, 0}, icon::sliders{}),
              .privacy = row("Privacy", {a, 1}, icon::eye{}),
              .chats = row("Chats", {a, 3}, icon::people{}),
              .sessions = row("Sessions", {a, 4}, icon::info{}),
              .proxy = row("Proxy", {a, 2}, icon::gear{})} {
    fState.apply({.padding = {6.0f, 0.0f, 0.0f, 0.0f}});
    this->light(0);
  }
  void light(int page) {
    parts.connection.set_lit(page == 0);
    parts.privacy.set_lit(page == 1);
    parts.chats.set_lit(page == 3);
    parts.sessions.set_lit(page == 4);
    parts.proxy.set_lit(page == 2);
  }
};

// A section's title on a settings page, as Gajim sets them: small, bold, dim.
inline nodes::Text section_title(std::string text) { return nodes::Text(std::move(text), 13.0f, dim_colour, true); }

// An account's Privacy page: whether it sends read receipts; whether it
// tells others one is typing, as every account's until chosen here, and a
// chat or space of it may choose again.
template <class Actions>
struct account_privacy : nodes::Stack {
  using receipts_row = switch_row<ask<Actions, &Actions::flip_account_receipts>>;
  using notify_row = switch_row<ask<Actions, &Actions::flip_account_notify>>;
  using notify_sound_row = switch_row<ask<Actions, &Actions::flip_account_notify_sound>>;
  struct parts_t {
    nodes::Text title = section_title("PRIVACY");
    receipts_row receipts;
    typing_choice<Actions> typing;
    notify_row notify;
    notify_sound_row notify_sound;
    nodes::Text note{"Off, the people you talk to through this account are not told when you have read their "
                     "messages, or that you are typing. Theirs are still shown, and receipts are still kept here.",
                     13.0f, dim_colour};
  } parts;

  account_privacy(Actions* a, bool receipts_on, std::optional<bool> typing_on, std::optional<bool> events_all = std::nullopt,
                  const std::optional<config::room_event_kinds>& kinds = std::nullopt, bool notify_on = true,
                  bool notify_sound_on = true, std::optional<bool> faces_on = std::nullopt,
                  std::optional<std::int64_t> jump_most = std::nullopt, std::optional<bool> previews_on = std::nullopt)
      : parts{.receipts = receipts_row("Send read receipts", {a}),
              .typing = typing_choice<Actions>(a, choice_level::account{}, typing_on),
              .notify = notify_row("Desktop notifications from it", {a}),
              .notify_sound = notify_sound_row("Their sound", {a})} {
    (void)events_all, (void)kinds, (void)faces_on, (void)jump_most, (void)previews_on;
    this->setGap(8.0f);
    parts.note.apply({.fillX = true});
    fState.apply({.fill = true});
    parts.note.setWrapped(true);
    parts.receipts.parts.toggle.setOnNow(receipts_on);
    parts.notify.parts.toggle.setOnNow(notify_on);
    parts.notify_sound.parts.toggle.setOnNow(notify_sound_on);
  }
  void show(bool receipts_on) { parts.receipts.parts.toggle.setOn(receipts_on); }
  void say(std::string, bool) {}
};

// An account's Chats page: how its chats are -- the room events they show,
// who has read up to where, link previews, how far a jump looks back, their
// looks, and its spaces in the bars. Each as every account's, until chosen
// here; a chat of it may choose again.
template <class Actions>
struct account_chats : nodes::Stack {
  // Its colour chosen.
  struct set_colour {
    Actions* actions;
    void operator()(const config::accent_t& one) const { actions->set_account_colour(one); }
  };
  // Home without what its spaces hold -- but direct messages -- or as every
  // account's.
  struct pick_home {
    Actions* actions;
    void operator()(std::size_t index) const {
      actions->set_home_hides(choice_level::account{}, index == 0 ? std::nullopt : std::optional<bool>(index >= 2));
      actions->set_home_direct(choice_level::account{}, index == 0 ? std::nullopt : std::optional<bool>(index == 3));
    }
  };
  struct parts_t {
    // Its colour, as only this page shows it: the strip of
    // its chats listed in other accounts' lists, unless they chose another.
    nodes::Text colour_title = section_title("COLOUR");
    accent_circles<set_colour> colours;
    switch_row<ask<Actions, &Actions::flip_account_strip>> strip;
    nodes::Text title = section_title("CHATS");
    event_kind_list<Actions> events;
    receipts_choice<Actions> faces;
    previews_choice<Actions> previews;
    previews_direct_choice<Actions> previews_direct;
    jump_search_choice<Actions> jump_search;
    nodes::Text looks_title = section_title("LOOKS");
    look_choices<Actions> looks;
    nodes::Text spaces_title = section_title("SPACES");
    choice_menu<pick_home> home;
    spaces_choices<Actions> places;
  } parts;
  account_chats(Actions* a, std::optional<bool> events_all, const std::optional<config::room_event_kinds>& kinds,
                std::optional<bool> faces_on, std::optional<std::int64_t> jump_most, std::optional<bool> previews_on,
                std::optional<bool> home_hides, std::optional<bool> home_direct, const config::accent_t& colour,
                bool strip_on, const config::theme_t& theme, std::optional<bool> direct_on = std::nullopt)
      : parts{.colours = accent_circles<set_colour>({a}, theme, false),
              .strip = switch_row<ask<Actions, &Actions::flip_account_strip>>("A strip on its chats in other lists", {a}),
              .events = event_kind_list<Actions>(a, choice_level::account{}, events_all, kinds),
              .faces = receipts_choice<Actions>(a, choice_level::account{}, faces_on),
              .previews = previews_choice<Actions>(a, choice_level::account{}, previews_on),
              .previews_direct = previews_direct_choice<Actions>(a, choice_level::account{}, direct_on),
              .jump_search = jump_search_choice<Actions>(a, choice_level::account{}, jump_most),
              .looks = look_choices<Actions>(a, choice_level::account{}),
              .home = choice_menu<pick_home>("Home",
                                             {"As above", "Every chat", "Without chats spaces hold",
                                              "Without those and direct messages"},
                                             !home_hides ? 0 : !*home_hides ? 1 : home_direct.value_or(false) ? 3 : 2, pick_home{a}),
              .places = spaces_choices<Actions>(a)} {
    this->setGap(8.0f);
    fState.apply({.fillX = true, .autoSize = scene::axes::kY});
    for (nodes::Text* each : {&parts.title, &parts.looks_title, &parts.spaces_title})
      each->apply({.margin = {10.0f, 0.0f, 0.0f, 0.0f}});
    this->show_colour(colour, strip_on);
  }
  void show_colour(const config::accent_t& colour, bool strip_on) {
    parts.colours.show_chosen(colour);
    parts.strip.parts.toggle.setOn(strip_on);
  }
  void say(std::string, bool) {}
};

// An account's Sessions page, as Element's: this session first, then the
// others -- each its name, its ID, where and when it was last seen -- each to
// rename or sign out, and all the others at once. Where the server asks for
// the password to sign one out, a field for it.
template <class Actions>
struct account_sessions : nodes::Stack {
  Actions* actions = nullptr;
  struct sign_out_one {
    account_sessions* page;
    std::string device;
    void operator()() const { page->sign_out({device}); }
  };
  struct sign_out_rest {
    account_sessions* page;
    void operator()() const { page->sign_out(page->others); }
  };
  struct start_rename {
    account_sessions* page;
    std::size_t row;
    void operator()() const { page->renaming(row); }
  };
  struct save_rename {
    account_sessions* page;
    std::size_t row;
    void operator()() const { page->rename(row); }
  };
  struct reload {
    Actions* actions;
    void operator()() const { actions->refresh_sessions(); }
  };
  // One session: its name over its ID, when and where it was last seen;
  // Rename, and Sign out where it is not this one.
  struct session_row : nodes::Stack {
    std::string device;
    std::string name;
    struct lines_t : two_lines {
      lines_t(std::string shown, std::string facts) : two_lines(std::move(shown), std::move(facts), 15.0f, 2.0f, 12.0f) {}
    };
    struct parts_t {
      lines_t lines;
      widgets::TextBox<> field;
      widgets::Button<save_rename> save;
      widgets::Button<start_rename> rename;
      std::optional<widgets::Button<sign_out_one>> sign_out;
    } parts;
    session_row(account_sessions* page, std::size_t index, const change::session_info& one, bool current)
        : device(one.id), name(one.name),
          parts{.lines = lines_t(one.name.empty() ? std::string("Unnamed session") : one.name, facts_of(one, current)),
                .field = widgets::TextBox<>("Session name"),
                .save = widgets::Button<save_rename>("Save", {page, index}),
                .rename = widgets::Button<start_rename>("Rename", {page, index})} {
      this->setHorizontal();
      this->setGap(8.0f);
      fState.apply({.fillX = true, .height = 60.0f, .padding = {0.0f, 12.0f, 0.0f, 12.0f}, .cornerRadius = 8.0f,
                    .background = tile_colour});
      parts.field.setText(one.name);
      parts.field.apply({.height = 32.0f, .grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
      parts.field.setVisible(false);
      parts.save.apply({.width = 70.0f, .height = 30.0f, .alignSelf = scene::align::kMiddle});
      parts.save.setVisible(false);
      parts.rename.apply({.width = 80.0f, .height = 30.0f, .alignSelf = scene::align::kMiddle});
      if (!current) {
        parts.sign_out.emplace("Sign out", sign_out_one{page, one.id});
        parts.sign_out->apply({.width = 86.0f, .height = 30.0f, .alignSelf = scene::align::kMiddle});
      }
    }
    // Its ID, when it was last seen, from where: as Element says them.
    [[nodiscard]] static std::string facts_of(const change::session_info& one, bool current) {
      std::string out = one.id;
      if (current)
        out += " · this session";
      if (one.last_seen)
        out += std::format(" · last seen {:%d.%m.%Y %H:%M}", std::chrono::floor<std::chrono::minutes>(*one.last_seen));
      if (one.ip)
        out += " · " + *one.ip;
      return out;
    }
    void show_field(bool on) {
      parts.lines.setVisible(!on);
      parts.field.setVisible(on);
      parts.save.setVisible(on);
      parts.rename.setVisible(!on);
      this->invalidateLayout();
    }
  };
  struct password_row : nodes::Stack {
    struct parts_t {
      nodes::Text label{"Your password, to sign sessions out:", 13.0f, dim_colour};
      widgets::TextBox<> field{"Password"};
    } parts;
    password_row() {
      this->setGap(6.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY});
      parts.field.setMasked(true);
      parts.field.apply({.fillX = true, .height = 34.0f});
    }
  };
  struct parts_t {
    nodes::Text title = section_title("SESSIONS");
    nodes::Text note{"Loading the sessions…", 13.0f, dim_colour};
    nodes::Text current_title = section_title("CURRENT SESSION");
    std::vector<session_row> current;
    nodes::Text others_title = section_title("OTHER SESSIONS");
    std::vector<session_row> rows;
    password_row password;
    widgets::Button<sign_out_rest> rest;
    widgets::Button<reload> refresh;
  } parts;
  std::vector<std::string> others;

  explicit account_sessions(Actions* a)
      : actions(a), parts{.rest = widgets::Button<sign_out_rest>("Sign out of all other sessions", {this}),
                          .refresh = widgets::Button<reload>("Refresh", {a})} {
    this->setGap(8.0f);
    fState.apply({.fillX = true, .autoSize = scene::axes::kY});
    parts.note.setWrapped(true);
    parts.note.apply({.fillX = true});
    for (nodes::Text* each : {&parts.current_title, &parts.others_title})
      each->apply({.margin = {10.0f, 0.0f, 0.0f, 0.0f}});
    parts.password.setVisible(false);
    parts.rest.apply({.width = 260.0f, .height = 34.0f, .margin = {8.0f, 0.0f, 0.0f, 0.0f}});
    parts.rest.setVisible(false);
    parts.refresh.apply({.width = 100.0f, .height = 30.0f});
    for (scene::Node* each : std::initializer_list<scene::Node*>{&parts.current_title, &parts.others_title})
      each->setVisible(false);
  }
  // The sessions, as the server listed them: this one first, the others by
  // when they were last seen, the latest first.
  void show(const std::string& current, std::vector<change::session_info> all) {
    std::ranges::sort(all, std::ranges::greater{}, [](const change::session_info& one) {
      return one.last_seen.value_or(std::chrono::sys_time<std::chrono::milliseconds>{});
    });
    parts.current.clear();
    parts.rows.clear();
    others.clear();
    for (const change::session_info& one : all | std::views::filter([&](const change::session_info& s) { return s.id == current; }))
      parts.current.emplace_back(this, 0, one, true);
    std::size_t index = 0;
    for (const change::session_info& one : all | std::views::filter([&](const change::session_info& s) { return s.id != current; })) {
      parts.rows.emplace_back(this, ++index, one, false);
      others.push_back(one.id);
    }
    parts.note.setText(all.empty() ? std::string("No sessions.") : std::string());
    parts.note.setVisible(all.empty());
    parts.current_title.setVisible(!parts.current.empty());
    parts.others_title.setVisible(!parts.rows.empty());
    parts.rest.setVisible(parts.rows.size() > 1);
    this->invalidateLayout();
  }
  // The server said no: why; and the password field, where that is it.
  void refused(const std::string& why, bool needs_password) {
    parts.note.setText(why);
    parts.note.setColour(error_colour);
    parts.note.setVisible(true);
    if (needs_password)
      parts.password.setVisible(true);
    this->invalidateLayout();
  }
  void sign_out(std::vector<std::string> devices) {
    if (devices.empty())
      return;
    parts.note.setText("Signing out…");
    parts.note.setColour(dim_colour);
    parts.note.setVisible(true);
    actions->sign_out_sessions(std::move(devices), parts.password.parts.field.text());
    this->invalidateLayout();
  }
  [[nodiscard]] session_row* row_at(std::size_t index) {
    if (index == 0)
      return parts.current.empty() ? nullptr : &parts.current.front();
    return index <= parts.rows.size() ? &parts.rows[index - 1] : nullptr;
  }
  void renaming(std::size_t index) {
    if (session_row* row = this->row_at(index))
      row->show_field(true);
  }
  void rename(std::size_t index) {
    if (session_row* row = this->row_at(index)) {
      row->show_field(false);
      actions->rename_session(row->device, row->parts.field.text());
    }
  }
  void say(std::string, bool) {}
};

// A proxy profile chosen for the chosen account: -1 for none.
template <class Actions>
struct choose_account_proxy {
  Actions* actions = nullptr;
  int index = -1;
  void operator()() const { actions->choose_account_proxy(index); }
};

// An account's Proxy page, as Gajim's: which of the program's proxy profiles
// it connects through, or none; and the way to the profiles themselves.
template <class Actions>
struct account_proxy : nodes::Stack {
  using manage_row = row_item<ask<Actions, &Actions::manage_proxies>>;
  struct parts_t {
    nodes::Text title = section_title("PROXY");
    std::vector<row_item<choose_account_proxy<Actions>>> choices;
    manage_row manage;
  } parts;

  account_proxy(Actions* a, const std::vector<config::proxy_settings>& all, const std::optional<std::string>& current)
      : parts{.manage = manage_row("Manage proxies…", {a}, icon::gear{})} {
    auto& choices = parts.choices;
    parts.title.apply({.margin = {0.0f, 0.0f, 4.0f, 0.0f}});
    parts.manage.apply({.margin = {8.0f, 0.0f, 0.0f, 0.0f}});
    fState.apply({.fill = true});
    // An empty place where the dots are, so the names line up.
    choices.emplace_back("No proxy", choose_account_proxy<Actions>{a, -1}, icon::dot{skia::colorSetARGB(0, 0, 0, 0)},
                         !current.has_value());
    for (std::size_t i = 0; i < all.size(); ++i)
      choices.emplace_back(std::format("{} ({} {}:{})", all[i].name, config::label_of(config::proxy_kind_of(all[i].kind)),
                                       all[i].host, all[i].port),
                           choose_account_proxy<Actions>{a, static_cast<int>(i)}, icon::dot{proxy_colour(all[i].name)},
                           current && *current == all[i].name);
  }
  void show(bool) {}
  void say(std::string, bool) {}
};

// The saved accounts down the side, and the chosen one's settings beside
// them.
template <class Actions>
struct accounts_panel : closes_on_escape<Actions> {
  static constexpr int kTab = 2;
  static constexpr float kListWidth = 280.0f;

  std::optional<std::string> selected;
  // The proxy profiles, for adding an account through one.
  std::vector<config::proxy_settings> proxies;

  // Its ← goes back from an account's pages to the list, and from the list
  // to the chats.
  using header_t = page_header<ask<Actions, &Actions::accounts_back>, ask<Actions, &Actions::accounts_back>>;
  // Under the header: the list down the side, and beside it what is chosen.
  struct body_row : nodes::Stack {
    struct side_column : nodes::Stack {
      using add_row = row_item<ask<Actions, &Actions::open_new_account>>;
      struct parts_t {
        add_row add;
        account_pages<Actions> pages;
        nodes::Text message{"", 13.0f, error_colour};
        nodes::ScrollContainer<nodes::Flow<std::vector<account_entry<Actions>>>> list{
            nodes::Flow<std::vector<account_entry<Actions>>>({.spacingY = 0.0f, .wrap = false}, {})};
      } parts;
      add_row& add = parts.add;
      account_pages<Actions>& pages = parts.pages;
      nodes::Text& message = parts.message;
      decltype(parts_t::list)& list = parts.list;
      explicit side_column(Actions* a)
          : parts{.add = add_row("Add account", {a}, icon::plus{}), .pages = account_pages<Actions>(a)} {
        fState.apply({.fillY = true, .width = kListWidth, .background = sidebar_colour});
        pages.setVisible(false);
        pages.apply({.fillX = true, .autoSize = scene::axes::kY});
        message.setWrapped(true);
        message.apply({.fillX = true, .margin = scene::Margin::all(8.0f)});
        list.apply({.fillX = true, .grow = scene::axes::kY});
        std::get<0>(list.fChildren).apply({.fillX = true, .autoSize = scene::axes::kY});
      }
    };
    using detail_t = splice::variant<nodes::Text, account_editor<Actions>, add_account_pane<Actions>, account_privacy<Actions>,
                                     account_proxy<Actions>, account_chats<Actions>, account_sessions<Actions>>;
    struct detail_column : nodes::Stack {
      // No account chosen, or the chosen one, or adding one.
      struct parts_t {
        // In a scroll view: a page taller than the window scrolls.
        nodes::ScrollContainer<detail_t> scroll{detail_t{std::in_place_index<0>, "Choose an account.", 15.0f, dim_colour}};
      } parts;
      detail_column() {
        fState.apply({.fillY = true, .grow = scene::axes::kX, .padding = {24.0f, 28.0f, 24.0f, 28.0f}});
        parts.scroll.apply({.fill = true});
      }
    };
    struct parts_t {
      side_column side;
      detail_column main;
    } parts;
    explicit body_row(Actions* a) : parts{.side = side_column(a)} {
      this->setHorizontal();
      fState.apply({.fillX = true, .grow = scene::axes::kY});
    }
  };
  struct parts_t {
    header_t header;
    body_row body;
  } parts;
  header_t& header = parts.header;
  typename body_row::side_column::add_row& add = parts.body.parts.side.add;
  account_pages<Actions>& pages = parts.body.parts.side.pages;
  nodes::Text& message = parts.body.parts.side.message;
  decltype(body_row::side_column::parts_t::list)& list = parts.body.parts.side.list;
  typename body_row::detail_t& detail = std::get<0>(parts.body.parts.main.parts.scroll.fChildren);
  // The page shown: across the pane, as tall as what it holds -- scrolled
  // from its top.
  void fit_detail() {
    splice::visit(
        [](auto& one) {
          one.fState.apply({.relativeSize = scene::axes::kX});
          one.fState.apply({.autoSize = scene::axes::kY});
        },
        detail);
    parts.body.parts.main.parts.scroll.scrollToStart();
  }

  // What is beside the list coming in when another is chosen, fading in.
  skiff::paint::Tween swap{1.0f, 200.0f, skiff::paint::movement::subtle{}};
  void begin_swap() {
    swap.jump(0.0f);
    swap.setTarget(1.0f);
    this->fade();
  }
  // The page coming in: faded in, and slid in from the side it comes from.
  float swap_side = 1.0f;
  void fade() {
    const float value = swap.value();
    const float shift = (1.0f - value) * 28.0f * swap_side;
    splice::visit(
        [&](auto& one) {
          one.fState.setAlpha(value);
          one.apply({.shiftX = shift});
        },
        detail);
  }
  // The side column's switch, the accounts' list for an account's pages and
  // back: the one coming in slid in and faded in, as the page beside it.
  skiff::paint::Tween side_swap{1.0f, 220.0f, skiff::paint::movement::subtle{}};
  float side_from = 1.0f;
  void slide_side() {
    const float value = side_swap.value();
    const float shift = (1.0f - value) * 36.0f * side_from;
    const std::array<scene::Node*, 2> shown = pages.visible() ? std::array<scene::Node*, 2>{&pages, nullptr}
                                                               : std::array<scene::Node*, 2>{&list, &add};
    for (scene::Node* each : shown | std::views::filter([](scene::Node* one) { return one != nullptr; })) {
      each->fState.setAlpha(value);
      each->apply({.shiftX = shift});
    }
  }
  [[nodiscard]] bool settling() const { return swap.moving() || side_swap.moving(); }
  void update(double now_ms) {
    if (swap.step(now_ms))
      this->fade();
    if (side_swap.step(now_ms))
      this->slide_side();
  }

  explicit accounts_panel(Actions* a)
      : closes_on_escape<Actions>(a), parts{.header = header_t("Accounts", {a}, {a}, true, false), .body = body_row(a)} {
    this->fState.apply({.fill = true});
  }

  // The saved accounts, with what the model says of each; the chosen one's
  // form is kept as it is, typing and all.
  void show(const std::vector<config::account_t>& saved, const model& now) {
    auto& entries = std::get<0>(std::get<0>(list.fChildren).fChildren);
    entries.clear();
    const config::account_t* chosen = nullptr;
    for (const config::account_t& one : saved) {
      const bool is_it = selected && config::address_of(one) == *selected;
      if (is_it)
        chosen = &one;
      entries.emplace_back(this->actions, one, now, is_it);
    }
    if (chosen) {
      if (auto* up = this->editor())
        up->show(*chosen, now);
    } else if (!this->adding()) {
      // Nothing to show beside the list: with no account at all, adding one.
      selected.reset();
      if (saved.empty())
        this->show_adding();
      else
        detail.template emplace<0>("Choose an account.", 15.0f, dim_colour);
    }
    this->invalidateLayout();
  }

  // An account's settings, brought up afresh.
  // One of the chosen account's pages beside the list: 0 Connection, 1
  // Privacy, 2 Proxy.
  void show_page(int page, const config::account_t& one, const model& now,
                 const std::vector<config::proxy_settings>& proxies = {}, const config::theme_t& theme = config::theme_t{}) {
    pages.light(page);
    if (page == 1) {
      detail.template emplace<3>(this->actions, config::read_receipts_of(one), config::send_typing_of(one),
                                   config::room_events_of(one), config::room_event_kinds_of(one),
                                   config::notify_of(one).value_or(true), config::notify_sound_of(one).value_or(true),
                                   config::show_receipts_of(one), config::jump_search_of(one),
                                   config::link_previews_of(one));
    } else if (page == 3) {
      detail.template emplace<5>(this->actions, config::room_events_of(one), config::room_event_kinds_of(one),
                                   config::show_receipts_of(one), config::jump_search_of(one), config::link_previews_of(one),
                                   config::home_hides_of(one), config::home_direct_of(one), config::colour_of(one),
                                   config::strip_of(one), theme, config::previews_direct_of(one));
    } else if (page == 4) {
      detail.template emplace<6>(this->actions);
    } else if (page == 2) {
      detail.template emplace<4>(this->actions, proxies, config::proxy_of(one));
    } else {
      detail.template emplace<1>(this->actions, one);
      splice::get<1>(detail).show(one, now);
    }
    this->fit_detail();
    this->begin_swap();
    this->invalidateLayout();
  }
  [[nodiscard]] account_privacy<Actions>* privacy() {
    return splice::visit(splice::overloaded{[](account_privacy<Actions>& one) { return &one; },
                                 [](auto&) -> account_privacy<Actions>* { return nullptr; }},
                      detail);
  }
  [[nodiscard]] account_chats<Actions>* chats_page() {
    return splice::visit(splice::overloaded{[](account_chats<Actions>& one) { return &one; },
                                            [](auto&) -> account_chats<Actions>* { return nullptr; }},
                         detail);
  }
  [[nodiscard]] account_sessions<Actions>* sessions() {
    return splice::visit(splice::overloaded{[](account_sessions<Actions>& one) { return &one; },
                                            [](auto&) -> account_sessions<Actions>* { return nullptr; }},
                         detail);
  }
  [[nodiscard]] account_proxy<Actions>* proxy() {
    return splice::visit(splice::overloaded{[](account_proxy<Actions>& one) { return &one; },
                                 [](auto&) -> account_proxy<Actions>* { return nullptr; }},
                      detail);
  }
  [[nodiscard]] bool pages_open() const { return pages.visible(); }

  void select(const config::account_t& one, const model& now) {
    add.set_lit(false);
    selected = config::address_of(one);
    this->show_pages(true);
    this->show_page(0, one, now);
  }

  // Adding an account, beside the list.
  // The account list, or the chosen account's pages, down the side.
  void show_pages(bool shown) {
    // Into an account: its pages come in from the right, and the page beside
    // them with them; back: the list comes in from the left.
    if (shown != pages.visible()) {
      side_from = shown ? 1.0f : -1.0f;
      swap_side = side_from;
      side_swap.jump(0.0f);
      side_swap.setTarget(1.0f);
    }
    pages.setVisible(shown);
    add.setVisible(!shown);
    list.setVisible(!shown);
    this->slide_side();
    header.parts.title.setText(shown && selected ? *selected : std::string("Accounts"));
    this->invalidateLayout();
  }
  // Back to the list of accounts, nothing chosen.
  void close_pages() {
    selected.reset();
    this->show_pages(false);
    detail.template emplace<0>("Choose an account.", 15.0f, dim_colour);
    this->fit_detail();
    this->begin_swap();
  }

  void show_adding() {
    this->show_pages(false);
    selected.reset();
    add.set_lit(true);
    detail.template emplace<2>(this->actions, proxies);
    this->fit_detail();
    this->begin_swap();
    this->invalidateLayout();
  }

  [[nodiscard]] account_editor<Actions>* editor() {
    return splice::visit(splice::overloaded{[](account_editor<Actions>& one) { return &one; },
                                 [](auto&) -> account_editor<Actions>* { return nullptr; }},
                      detail);
  }
  [[nodiscard]] add_account_pane<Actions>* adding() {
    return splice::visit(splice::overloaded{[](add_account_pane<Actions>& one) { return &one; },
                                 [](auto&) -> add_account_pane<Actions>* { return nullptr; }},
                      detail);
  }
  [[nodiscard]] xmpp_form<Actions>* xmpp() {
    return splice::visit(splice::overloaded{[](account_editor<Actions>& one) { return xmpp_form_in(one.parts.form); },
                                 [](add_account_pane<Actions>& one) { return one.xmpp(); },
                                 [](auto&) -> xmpp_form<Actions>* { return nullptr; }},
                      detail);
  }

  void say(std::string text) {
    message.setText(std::move(text));
    this->invalidateLayout();
  }

};

}  // namespace mux::ui
