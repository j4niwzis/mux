// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.chat_list: what the chats screen's side lists, worked out by the
// program -- the account listed and its folder, the folder tabs and the
// space bars, the list of chats open, what the server search found, and the chats
// listed: their order, their rows' views and heights. From what is kept and
// the chats model, put in what is shown (mux.ui:list_shown) only where it
// moved. A part of the program: it reaches the rest through the services.
export module mux.app.chat_list;

import std;
import splice;
import skia;
import mux.core;
import mux.config;
import mux.protocols;
import mux.logic.text;
import mux.kept_root;
import mux.ui;
import mux.plugins.app;
import mux.ui.proto;
import skiff.compose;
import skiff.model;
import mux.app.services;
import mux.app.kept;
import mux.app.network;
import mux.app.requests;

export namespace mux::app {

// What the plugins say of the chat list's folders (contract::chat_folders):
// a space's, for one.
template <class List>
struct folder_rules;
template <class... Features>
struct folder_rules<plugin_list<Features...>> {
  static bool folder_chat(const mux::ui::folder_context& ctx, const mux::conversation& one) {
    return (is_folder(contract::chat_folders{}, type_tag<Features>{}, ctx, one) || ...);
  }
  static std::optional<std::set<std::string>> rooms_of(const mux::ui::folder_context& ctx, const mux::ui::folder_t& folder) {
    std::optional<std::set<std::string>> found;
    ((found = found ? found : folder_rooms(contract::chat_folders{}, type_tag<Features>{}, ctx, folder)), ...);
    return found;
  }
  static std::set<std::string> home_out_of(const mux::ui::folder_context& ctx) {
    std::set<std::string> out;
    (out.insert_range(out_of_home(contract::chat_folders{}, type_tag<Features>{}, ctx)), ...);
    return out;
  }
  static void tabs_into(const mux::ui::folder_context& ctx, std::vector<std::pair<std::string, mux::ui::folder_t>>& out) {
    (folder_tabs(contract::chat_folders{}, type_tag<Features>{}, ctx, out), ...);
  }
  static bool keeps(const mux::ui::folder_context& ctx, const mux::ui::folder_t& folder) {
    return (keeps_folder(contract::chat_folders{}, type_tag<Features>{}, ctx, folder) || ...);
  }
  static void bars_into(const mux::ui::folder_context& ctx, const mux::ui::list_shown& list, mux::ui::side_shown& side, mux::ui::ui_shared& shared) {
    (space_bars(contract::chat_folders{}, type_tag<Features>{}, ctx, list, side, shared), ...);
  }
};
using folder_plugins = folder_rules<composition::providers<contract::chat_folders, enabled_plugins>>;
// What the plugins say of chats that open as a list of chats
// (contract::chat_scopes): a forum, for one -- whether one does, and the row
// it is listed by.
template <class List>
struct scope_rules;
template <class... Features>
struct scope_rules<plugin_list<Features...>> {
  static bool opens(const services& shared, const mux::conversation& one) {
    return (opens_as_scope(contract::chat_scopes{}, type_tag<Features>{}, shared, one) || ...);
  }
  template <class Events>
  static std::optional<mux::conversation> row(const services& shared, const mux::conversation& one, const mux::account& in, const Events& events_of) {
    std::optional<mux::conversation> made;
    ((made = made ? made : scope_row(contract::chat_scopes{}, type_tag<Features>{}, shared, one, in, events_of)), ...);
    return made;
  }
};
using scope_plugins = scope_rules<composition::providers<contract::chat_scopes, enabled_plugins>>;

class chat_list_part {
 public:
  chat_list_part(services& shared, kept_settings& kept, const mux::ui::palette& colours) : s_(&shared), k_(&kept), colours_(&colours) {}
  chat_list_part(const chat_list_part&) = delete;
  chat_list_part& operator=(const chat_list_part&) = delete;

  // Each frame: the screen's frame, as the window is and what is open; and
  // the parts worked out again where what they come from moved.
  void show() {
    this->show_frame();
    const mux::model& now = *s_->model;
    const auto& chat = *s_->showing->look<mux::ui::chat_shown>();
    mux::ui::list_shown list = *s_->showing->look<mux::ui::list_shown>();
    // The account listed: the one asked for, where the model has it; else
    // the one listed, else the first.
    const auto has = [&](const std::optional<mux::account_id>& one) { return one && now.accounts().contains(*one); };
    list.account = has(chat.current) ? chat.current
                   : has(list.account) ? list.account
                   : now.accounts().empty() ? std::nullopt
                                            : std::optional<mux::account_id>(now.accounts().keyAt(0));
    list.scope = chat.scope;
    const inputs seen{now.chats().revision(), k_->state.revision(), list, chat.chosen, chat.scope, s_->drafts_revision,
                      s_->showing->look<mux::ui::found_shown>()->count.has_value(), s_->showing->look<mux::ui::elsewhere_shown>()->found.has_value()};
    if (seen_ == seen)
      return;
    seen_ = seen;
    const kept_view kept = this->read_kept(list.account);
    this->keep_folder(now, kept, list);
    this->put(list);
    auto listing = std::make_shared<mux::ui::chat_listing>(this->listing_of(now, kept, list, chat));
    order_ = listing->chats | std::views::transform(&mux::ui::listed_chat::id) | std::ranges::to<std::vector>();
    auto side = this->side_of(now, kept, list, chat);
    const bool elsewhere = s_->showing->look<mux::ui::elsewhere_shown>()->found.has_value();
    side.no_chats = !now.accounts().empty() && listing->chats.empty() && !elsewhere;
    side.chats_on = !s_->showing->look<mux::ui::found_shown>()->count.has_value() && (!listing->chats.empty() || !elsewhere);
    this->put(std::move(side));
    this->put(mux::ui::listing_shown{++revision_, std::move(listing), list.account, now.accounts().keys() | std::ranges::to<std::vector>(),
                                     k_->state.root().looks.fValue.theme});
    this->search_elsewhere(now, list);
  }

  // A folder chosen; what is typed in the search; the bars made big or not.
  void apply(const request::pick_folder& one) {
    this->change([&](mux::ui::list_shown& list) {
      list.folder = one.folder;
      list.pointed.reset();
    });
  }
  void apply(const request::list_search& one) {
    this->change([&](mux::ui::list_shown& list) { list.search = one.text; });
  }
  void apply(const request::big_bars& one) {
    this->change([&](mux::ui::list_shown& list) { list.big_bars = one.on; });
  }
  // An icon of the bars being dragged: the side bar up to drop on, whatever
  // it holds; and dropped -- in a bar, its items in their new order.
  void apply(const skiff::compose::ItemsDragging& one) {
    dragging_ = one.on;
    seen_.reset();
  }
  template <class Asked>
  void apply(const skiff::compose::ItemsPlaced<mux::config::space_item_t>& one, const Asked& asked) {
    dragging_ = false;
    seen_.reset();
    const auto bar_of = [](skiff::compose::ReorderBar bar) -> mux::config::space_bar_t {
      return bar == skiff::compose::ReorderBar::horizontal ? mux::config::space_bar_t{mux::config::space_bar::top{}}
                                                           : mux::config::space_bar_t{mux::config::space_bar::side{}};
    };
    const auto& listed = s_->showing->look<mux::ui::list_shown>()->account;
    asked(request::place_spaces{listed ? listed->address : std::string(), bar_of(one.to), one.order, bar_of(one.from), one.item});
  }
  // The chats screen's keys, as tdesktop's: Ctrl+1 to Ctrl+9 a folder;
  // Ctrl+F finds in the chat, Ctrl+K puts a link on what is selected,
  // Ctrl+Up and Ctrl+Down answer the messages up and down, Up in an empty
  // field edits the last sent; Ctrl+C and Delete copy and delete what is
  // selected; Esc lets go of what is up, one thing at a time -- a forum
  // gone into (a list of chats), a selection, the search, an answer, then the threads or the
  // info, else the chat; Ctrl+Tab and Alt+Up and Alt+Down go through the
  // chats listed (a forum gone to, lit, not opened); Ctrl+End to the newest.
  template <class Asked>
  void apply(const request::screen_key& one, const Asked& asked) {
    namespace keys = skiff::scene::keys;
    const bool any = one.control || one.shift || one.alt;
    const auto& chat = *s_->showing->look<mux::ui::chat_shown>();
    const auto& list = *s_->showing->look<mux::ui::list_shown>();
    const auto& side = *s_->showing->look<mux::ui::side_shown>();
    static constexpr std::array kFolderKeys{keys::k1, keys::k2, keys::k3, keys::k4, keys::k5, keys::k6, keys::k7, keys::k8, keys::k9};
    if (const auto digit = std::ranges::find(kFolderKeys, one.key); one.control && digit != kFolderKeys.end()) {
      if (const auto place = static_cast<std::size_t>(digit - kFolderKeys.begin()); place < side.folders.size())
        this->apply(request::pick_folder{side.folders[place].folder});
      return;
    }
    if (one.key == keys::kEscape && !any && !chat.chosen && chat.scope) {
      asked(request::close_scope{});
      return;
    }
    const bool stepping = (one.key == keys::kTab && one.control) || ((one.key == keys::kUp || one.key == keys::kDown) && one.alt);
    if (!chat.chosen && !(list.pointed && stepping))
      return;
    const bool selected = s_->showing->look<mux::ui::selection_shown>()->count > 0;
    const bool searching = s_->searching;
    const bool answering = s_->showing->look<mux::ui::chat_compose>()->said.has_value();
    if (one.key == keys::kF && one.control)
      asked(request::open_search{});
    else if (one.key == keys::kK && one.control)
      asked(request::ask_link{});
    else if (one.key == keys::kUp && one.control)
      asked(request::reply_step{true, one.repeat});
    else if (one.key == keys::kDown && one.control)
      asked(request::reply_step{false, one.repeat});
    else if (one.key == keys::kUp && !any && mux::ui::composer_empty(s_->chat_writing()))
      asked(request::edit_last{});
    else if (one.key == keys::kC && one.control && selected)
      asked(request::selection_copy{});
    else if (one.key == keys::kDelete && !any && selected)
      asked(request::selection_delete{});
    else if (one.key == keys::kC && one.control) {
      // What any text shows selected: its own copy of it, as it changed.
      if (const std::string text = skiff::scene::selectedText(); !text.empty())
        skiff::scene::setClipboardText(text);
    } else if (one.key == keys::kEscape && !any && selected)
      asked(request::selection_cancel{});
    else if (one.key == keys::kEscape && !any && searching)
      asked(request::close_search{});
    else if (one.key == keys::kEscape && !any && chat.thread_answer)
      asked(request::thread_answer_cancel{});
    else if (one.key == keys::kEscape && !any && answering)
      asked(request::cancel_compose{});
    else if (stepping)
      this->step(one.key == keys::kUp || one.shift, chat, list, asked);
    else if ((one.key == keys::kPageUp || one.key == keys::kPageDown) && !any)
      asked(request::page_messages{one.key == keys::kPageUp});
    else if (one.key == keys::kEnd && one.control)
      asked(request::jump_to_end{});
    else if (one.key == keys::kEscape && !any) {
      // Nothing else to let go of: the threads or the info shut, else the
      // chat closed, as tdesktop's Esc closes it.
      if (chat.threads_open)
        asked(request::toggle_threads{});
      else if (chat.info_open)
        asked(request::toggle_info{});
      else
        asked(request::close_chat{});
    }
  }
  // Out of the list of chats open, to its row, lit: Alt+Right goes back in.
  template <class Asked>
  void apply(const request::leave_scope&, const Asked& asked) {
    const auto& chat = *s_->showing->look<mux::ui::chat_shown>();
    const auto& listed = s_->showing->look<mux::ui::list_shown>()->account;
    if (!chat.scope || !listed)
      return;
    this->point(mux::conversation_id{*listed, *chat.scope});
    asked(request::close_scope{});
  }
  // Into the list of chats gone to (Alt+Right).
  template <class Asked>
  void apply(const request::open_pointed&, const Asked& asked) {
    if (const auto at = s_->showing->look<mux::ui::list_shown>()->pointed) {
      this->point(std::nullopt);
      asked(request::choose{*at});
    }
  }
  // A swipe on a phone's window: right, a step back -- from the threads or
  // the info to the chat, from the chat to the chats -- or, from the chats,
  // the drawer out.
  template <class Asked>
  void apply(const request::screen_swipe& one, const Asked& asked) {
    const auto& chat = *s_->showing->look<mux::ui::chat_shown>();
    if (!s_->single() || !one.right)
      return;
    if (!chat.chosen)
      asked(request::open_drawer{});
    else if (chat.threads_open)
      asked(request::toggle_threads{});
    else if (chat.info_open)
      asked(request::toggle_info{});
    else
      asked(request::close_chat{});
  }
  // A list of chats gone to by Alt+Up or Alt+Down: lit, not opened.
  void point(std::optional<mux::conversation_id> at) {
    this->change([&](mux::ui::list_shown& list) { list.pointed = std::move(at); });
  }

  // The server's answers for what is searched: kept while they are for what
  // is typed now.
  void found_rooms(const std::string& query, std::vector<mux::directory_room> rooms) {
    if (query.empty() || query != asked_)
      return;
    rooms_ = std::move(rooms);
    rooms_came_ = true;
    this->show_elsewhere();
  }
  void found_people(const std::string& query, std::vector<mux::found_person> people) {
    if (query.empty() || query != asked_)
      return;
    people_ = std::move(people);
    people_came_ = true;
    for (const mux::found_person& one : people_ | std::views::filter([](const mux::found_person& one) { return one.avatar && !one.avatar->empty(); })) { mux::ui::listed_avatars().emplace_back(one.id, *one.avatar); }
    this->show_elsewhere();
  }
  // The side's edge dragged to `x`; the info's (or the threads') to `x`:
  // their widths, within what the window leaves the chat.
  void resize_side(float x) {
    this->change_frame([&](mux::ui::screen_shown& now) { now.side_width = std::max(mux::ui::kMinSidebar, x); });
  }
  void resize_info(float x) {
    const float across = s_->showing->look<mux::ui::window_shape>()->width;
    this->change_frame([&](mux::ui::screen_shown& now) {
      now.info_width = std::clamp(across - x, 260.0f, std::max(260.0f, across - now.side_width - 300.0f));
    });
  }
  // Whether a chat opens as a list of chats, as a plugin says (a forum).
  [[nodiscard]] bool is_scope(const mux::conversation_id& id) const {
    const mux::conversation* one = s_->model->find(id);
    return one != nullptr && scope_plugins::opens(*s_, *one);
  }
  // Every chat listed, in order: what Ctrl+Tab and Alt+Up go through.
  [[nodiscard]] const std::vector<mux::conversation_id>& order() const { return order_; }

 private:
  // What it is worked out from: the chats, what is kept, what is listed and
  // chosen, the drafts.
  struct inputs {
    skiff::model::Revision chats = 0, kept = 0;
    mux::ui::list_shown list;
    std::optional<mux::conversation_id> chosen;
    std::optional<std::string> scope;
    std::uint64_t drafts = 0;
    bool found = false, elsewhere = false;
    friend bool operator==(const inputs&, const inputs&) = default;
  };
  // What is kept that the list reads: the chats in other accounts' lists and
  // their strips, the chats muted, left out of Home; the
  // bars' settings; and each chat's room events.
  struct kept_view {
    std::map<mux::account_id, std::vector<mux::conversation_id>> listed_in;
    std::set<mux::conversation_id> moved_out;
    std::map<mux::conversation_id, skia::SkColor> strips;
    std::set<mux::conversation_id> muted, hidden_from_home;
    bool spaces_on = true, top_bar_on = true;
    std::vector<mux::config::space_placed> space_places;
    bool home_hides_spaced = false, home_hides_direct = false;
    mux::chat_settings reads;
  };
  kept_view read_kept(const std::optional<mux::account_id>& listed) const {
    const kept_root& root = k_->state.root();
    const auto& looks = root.looks.fValue;
    const mux::config::account_t* own = listed ? mux::account_settings(root, listed->address) : nullptr;
    const auto own_or = [&](const std::optional<bool>& theirs, bool everyone) { return theirs.value_or(everyone); };
    const auto& placements = root.placements.fValue;
    const auto chat_of = [](const mux::config::chat_placement& one) {
      return mux::conversation_id{{mux::ui::protocol_of(one.account), one.account}, one.conversation};
    };
    const auto strip_shown = [&](const mux::config::chat_placement& one) {
      const mux::config::account_t* theirs = mux::account_settings(root, one.account);
      return one.strip.value_or(theirs == nullptr || mux::config::strip_of(*theirs));
    };
    const auto strip_of = [&](const mux::config::chat_placement& one) {
      const mux::config::account_t* theirs = mux::account_settings(root, one.account);
      const mux::config::accent_t colour = one.strip_colour ? mux::config::accent_of(one.strip_colour)
                                           : theirs != nullptr ? mux::config::colour_of(*theirs)
                                                               : mux::config::default_colour_of(one.account);
      return std::pair{chat_of(one), mux::ui::colour_of(colour, looks.theme)};
    };
    return {.listed_in = std::ranges::fold_left(placements, std::map<mux::account_id, std::vector<mux::conversation_id>>{},
                                                [&](auto by_list, const mux::config::chat_placement& one) {
                                                  by_list[mux::account_id{mux::ui::protocol_of(one.listed_in), one.listed_in}].push_back(chat_of(one));
                                                  return by_list;
                                                }),
            .moved_out = placements | std::views::filter(&mux::config::chat_placement::moved) | std::views::transform(chat_of) | std::ranges::to<std::set>(),
            // The last placement of a chat says its strip, as it was written last.
            .strips = placements | std::views::reverse | std::views::filter(strip_shown) | std::views::transform(strip_of) | std::ranges::to<std::map>(),
            .muted = mux::chats_where<&mux::config::chat_choices::muted>(root),
            .hidden_from_home = mux::chats_where<&mux::config::chat_choices::hidden_from_home>(root),
            .spaces_on = looks.spaces,
            .top_bar_on = looks.top_bar,
            .space_places = looks.space_places,
            .home_hides_spaced = own ? own_or(mux::config::home_hides_of(*own), looks.home_hides_spaced) : looks.home_hides_spaced,
            .home_hides_direct = own ? own_or(mux::config::home_direct_of(*own), looks.home_hides_direct) : looks.home_hides_direct,
            .reads = k_->reads()};
  }
  // Which room events a chat shows: in an encrypted room, who joins and who
  // is invited always -- each is given the room's key.
  [[nodiscard]] mux::room_event_filter events_of(const kept_view& kept, const mux::conversation& one) const {
    mux::room_event_filter filter = kept.reads.room_event_filter_of(one.id);
    if (one.encrypted)
      for (const mux::room_event_t& kind : std::array{mux::room_event_t{mux::room_event::joins{}}, mux::room_event_t{mux::room_event::invites{}}}) { filter.shown[kind.index()] = true; }
    return filter;
  }
  // What a folder is worked out in, for the plugins that say what theirs
  // are (contract::chat_folders): the account listed, its chats opened as
  // lists of chats (forums), what is kept of the bars and of Home.
  [[nodiscard]] mux::ui::folder_context context_of(const mux::model& now, const kept_view& kept,
                                                   const std::optional<mux::account_id>& listed) const {
    mux::ui::folder_context ctx{.in = listed ? now.accounts().find(*listed) : nullptr,
                                .hidden_from_home = kept.hidden_from_home,
                                .spaces_on = kept.spaces_on,
                                .top_bar_on = kept.top_bar_on,
                                .home_hides_spaced = kept.home_hides_spaced,
                                .space_places = kept.space_places};
    if (ctx.in)
      ctx.scopes = ctx.in->conversations.values() | std::views::filter([&](const mux::conversation& one) { return scope_plugins::opens(*s_, one); }) |
                   std::views::transform([](const mux::conversation& one) { return one.id.id; }) | std::ranges::to<std::set>();
    return ctx;
  }
  // The folders the account has -- the plugins' (a space's, where there
  // are no bars), then its groups -- and the one chosen kept while it is
  // still there: a tab's, or a plugin's (a bar's).
  [[nodiscard]] std::vector<std::pair<std::string, mux::ui::folder_t>> folders_of(const mux::model& now, const kept_view& kept,
                                                                                const std::optional<mux::account_id>& listed) const {
    std::vector<std::pair<std::string, mux::ui::folder_t>> out{{"All", mux::ui::folder::all{}}};
    if (!listed)
      return out;
    const auto& chats = now.accounts().at(*listed).conversations;
    folder_plugins::tabs_into(this->context_of(now, kept, listed), out);
    const auto groups = chats.values() | std::views::transform(&mux::conversation::groups) | std::views::join | std::ranges::to<std::set<std::string>>();
    for (const std::string& name : groups) { out.emplace_back(name, mux::ui::folder::group{name}); }
    return out;
  }
  void keep_folder(const mux::model& now, const kept_view& kept, mux::ui::list_shown& list) const {
    const bool in_bars = folder_plugins::keeps(this->context_of(now, kept, list.account), list.folder);
    const auto folders = this->folders_of(now, kept, list.account);
    if (!in_bars && !std::ranges::contains(folders, list.folder, &std::pair<std::string, mux::ui::folder_t>::second))
      list.folder = mux::ui::folder::all{};
  }

  // The side: the tabs, the bars, the list of chats open, "No chats yet".
  [[nodiscard]] mux::ui::side_shown side_of(const mux::model& now, const kept_view& kept, const mux::ui::list_shown& list,
                                           const mux::ui::chat_shown& chat) const {
    mux::ui::side_shown out;
    const auto folders = this->folders_of(now, kept, list.account);
    if (folders.size() > 1)
      out.folders = folders | std::views::transform([&](const auto& one) {
                      return mux::ui::folder_tab_shown{one.first, one.second, one.second == list.folder};
                    }) | std::ranges::to<std::vector>();
    this->bars_of(now, kept, list, out);
    out.side_on = out.side_on || dragging_;
    out.dragging = dragging_;
    out.account = list.account ? list.account->address : std::string();
    const mux::account* in = list.account ? now.accounts().find(*list.account) : nullptr;
    const mux::conversation* scope = chat.scope && in ? in->conversations.find(*chat.scope) : nullptr;
    if (scope && scope_plugins::opens(*s_, *scope))
      out.scope = mux::ui::display_name(*scope);
    return out;
  }
  // The bars: the plugins' (contract::chat_folders) -- the spaces'.
  void bars_of(const mux::model& now, const kept_view& kept, const mux::ui::list_shown& list, mux::ui::side_shown& out) const {
    folder_plugins::bars_into(this->context_of(now, kept, list.account), list, out, s_->ui);
  }

  // The chats listed: the account's in the folder chosen -- or the list of
  // chats open -- with the other accounts' placed here, those searched for,
  // invites first, then by their newest.
  [[nodiscard]] mux::ui::chat_listing listing_of(const mux::model& now, const kept_view& kept, const mux::ui::list_shown& list,
                                                 const mux::ui::chat_shown& chat) const {
    mux::ui::chat_listing out;
    constexpr auto lower = mux::logic::folded;
    const std::string wanted = lower(list.search);
    const mux::account* in = list.account ? now.accounts().find(*list.account) : nullptr;
    if (in == nullptr)
      return out;
    // What the plugins say of the folder chosen: the rooms it shows (a
    // space's, down every level), and what Home leaves out.
    const mux::ui::folder_context ctx = this->context_of(now, kept, list.account);
    const std::optional<std::set<std::string>> in_folder_rooms = folder_plugins::rooms_of(ctx, list.folder);
    const auto rooms_of = [](const mux::conversation& one) -> const std::vector<std::string>& { return one.children; };
    // Lists of chats (forums): each listed as one chat; their rooms in them,
    // not beside them.
    const std::set<std::string> in_scopes = in->conversations.values() | std::views::filter([&](const mux::conversation& one) { return scope_plugins::opens(*s_, one); }) | std::views::transform(rooms_of) | std::views::join | std::ranges::to<std::set>();
    const mux::conversation* scope = chat.scope ? in->conversations.find(*chat.scope) : nullptr;
    // What the plugins leave out of Home (what the account's spaces hold,
    // where it is chosen so) -- but direct messages.
    const bool home = list.folder == mux::ui::folder_t{mux::ui::folder::all{}};
    const std::set<std::string> in_spaces = home ? folder_plugins::home_out_of(ctx) : std::set<std::string>{};
    const auto direct = [](const mux::conversation& one) {
      return spl::visit(spl::overloaded{[](mux::conversation_kind::direct) { return true; }, [](const auto&) { return false; }}, one.kind);
    };
    const auto in_folder = [&](const mux::conversation& one) {
      if (!one.space && in_spaces.contains(one.id.id) && !direct(one))
        return false;
      if (kept.home_hides_spaced && kept.home_hides_direct && home && direct(one))
        return false;
      if (scope)
        return !one.space && std::ranges::contains(scope->children, one.id.id);
      // A folder of a plugin's, not a chat (a space) -- but a list of chats is one chat.
      if (folder_plugins::folder_chat(ctx, one))
        return false;
      if (!one.space && in_scopes.contains(one.id.id))
        return false;
      return spl::visit(spl::overloaded{[](const mux::ui::folder::all&) { return true; },
                                        [&](const mux::ui::folder::space&) { return in_folder_rooms && in_folder_rooms->contains(one.id.id); },
                                        [&](const mux::ui::folder::group& g) { return std::ranges::contains(one.groups, g.name); },
                                        [&](const mux::ui::folder::direct&) { return direct(one); }},
                        list.folder);
    };
    // Upgraded away, its new room here: only the new one listed -- where the
    // two agree (matrix-js-sdk's CVE-2025-59160, the other way round).
    const auto found_by = [&](const mux::conversation& one) {
      const auto& state = mux::ui::protocol_state_of(s_->ui, one.id.account);
      if (const auto successor = mux::proto::successor_of(state, one))
        if (const auto* next = in->conversations.find(*successor); next != nullptr && mux::proto::predecessor_of(state, *next) == one.id.id)
          return false;
      return in_folder(one) && (wanted.empty() || lower(mux::ui::display_name(one)).contains(wanted) || lower(one.id.id).contains(wanted));
    };
    // Its own chats -- not those moved to another account's list -- and the
    // other accounts' listed in it.
    std::vector<const mux::conversation*> chats = in->conversations.values() | std::views::filter([&](const mux::conversation& one) { return !kept.moved_out.contains(one.id) && found_by(one); }) | std::views::transform([](const mux::conversation& one) { return &one; }) | std::ranges::to<std::vector>();
    if (const auto extra = kept.listed_in.find(*list.account); extra != kept.listed_in.end())
      chats.append_range(extra->second | std::views::transform([&](const mux::conversation_id& id) { return now.find(id); }) | std::views::filter([&](const mux::conversation* one) { return one != nullptr && found_by(*one); }));
    const auto strip_for = [&](const mux::conversation* one) -> std::optional<skia::SkColor> {
      if (one->id.account == *list.account)
        return std::nullopt;
      const auto found = kept.strips.find(one->id);
      return found == kept.strips.end() ? std::nullopt : std::optional<skia::SkColor>(found->second);
    };
    // A list of chats' row (a forum's): as its plugin makes it.
    for (const mux::conversation*& one : chats)
      if (auto made = scope_plugins::row(*s_, *one, *in, [&](const mux::conversation& topic) { return this->events_of(kept, topic); }))
        one = &out.made_rows.insert_or_assign(one->id, std::move(*made)).first->second;
    // Invites first, as Element lists them; then by their newest.
    std::ranges::sort(chats, std::ranges::greater{}, [&](const mux::conversation* one) {
      const mux::message* last = mux::newest(*one, this->events_of(kept, *one));
      return std::pair{one->invite.has_value(), last ? last->at : std::chrono::sys_time<std::chrono::milliseconds>{}};
    });
    // The chat open -- or, where Alt+Up or Alt+Down went to a list of chats, that.
    const auto is_chosen = [&](const mux::conversation* one) { return list.pointed ? *list.pointed == one->id : chat.chosen && *chat.chosen == one->id; };
    out.chats = chats | std::views::transform([&](const mux::conversation* one) {
                  const auto events = this->events_of(kept, *one);
                  return mux::ui::listed_chat{.id = one->id,
                                              .view = mux::ui::conversation_view_of(s_->ui, *one, is_chosen(one), kept.muted.contains(one->id),
                                                                                    s_->draft_of(one->id), events, strip_for(one)),
                                              .events = events,
                                              .strip = strip_for(one),
                                              .height = mux::ui::conversation_height(*one)};
                }) | std::ranges::to<std::vector>();
    return out;
  }

  // A search of two letters or more also asks the account's server, where
  // it finds rooms or people: once for each thing typed.
  void search_elsewhere(const mux::model& now, const mux::ui::list_shown& list) {
    const std::string& typed = list.search;
    const auto& state = list.account ? std::optional(mux::ui::protocol_state_of(s_->ui, *list.account)) : std::nullopt;
    const bool find_rooms = state && mux::proto::offers(*state, mux::proto::feature::room_directory{});
    const bool find_people = state && mux::proto::offers(*state, mux::proto::feature::people_directory{});
    const bool asks = (find_rooms || find_people) && !mux::logic::folded(typed).empty() && typed.size() >= 2;
    (void)now;
    if (list.account != asked_by_) {
      asked_by_ = list.account;
      asked_.clear();
    }
    if (asks && typed != asked_) {
      asked_ = typed;
      rooms_.clear();
      people_.clear();
      rooms_came_ = !find_rooms;
      people_came_ = !find_people;
      if (!s_->demo()) {
        if (find_rooms)
          s_->net->search_directory(*list.account, std::string(), typed);
        if (find_people)
          s_->net->search_people(*list.account, typed);
      }
    } else if (!asks && !asked_.empty()) {
      asked_.clear();
      rooms_.clear();
      people_.clear();
    }
    this->show_elsewhere();
  }
  // What the server found, less the rooms the account is in; a room's
  // address typed whole, offered to join first.
  void show_elsewhere() {
    if (asked_.empty()) {
      this->put(mux::ui::elsewhere_shown{});
      return;
    }
    const mux::model& now = *s_->model;
    auto rooms = rooms_;
    if (asked_by_)
      std::erase_if(rooms, [&](const mux::directory_room& room) { return now.find(mux::conversation_id{*asked_by_, room.id}) != nullptr; });
    if (const auto address = mux::ui::room_address_for(asked_by_, asked_))
      if (std::ranges::none_of(rooms, [&](const mux::directory_room& room) { return room.id == *address || room.alias == *address; }) &&
          now.find(mux::conversation_id{*asked_by_, *address}) == nullptr)
        rooms.insert(rooms.begin(), mux::directory_room{.id = *address, .name = "Join room by address", .alias = *address});
    this->put(mux::ui::elsewhere_shown{mux::ui::server_search_facts{std::move(rooms), people_, asked_by_, !rooms_came_ || !people_came_}});
  }

  // To the next chat listed, or the one before -- from the list of chats
  // gone to, where one is: such a list gone to, lit, not opened; a chat, opened.
  template <class Asked>
  void step(bool back, const mux::ui::chat_shown& chat, const mux::ui::list_shown& list, const Asked& asked) {
    const auto at = std::ranges::find(order_, list.pointed ? *list.pointed : *chat.chosen);
    if (at == order_.end())
      return;
    const auto index = static_cast<std::size_t>(at - order_.begin());
    const std::size_t to = back ? (index == 0 ? order_.size() - 1 : index - 1) : (index + 1) % order_.size();
    const mux::conversation* next = s_->model->find(order_[to]);
    if (next && scope_plugins::opens(*s_, *next)) {
      this->point(order_[to]);
      return;
    }
    this->point(std::nullopt);
    asked(request::choose{order_[to]});
  }
  // A phone's way (the user's, #13548): a window narrow and taller than it
  // is wide shows one thing at a time -- the chats, a chat, or its info or
  // threads -- not side by side.
  void show_frame() {
    const auto& shape = *s_->showing->look<mux::ui::window_shape>();
    const auto& chat = *s_->showing->look<mux::ui::chat_shown>();
    this->change_frame([&](mux::ui::screen_shown& now) {
      now.single = shape.phone();
      now.pane = !chat.chosen ? mux::ui::pane_t{mux::ui::pane::list{}}
                 : chat.info_open || chat.threads_open ? mux::ui::pane_t{mux::ui::pane::beside{}}
                                                       : mux::ui::pane_t{mux::ui::pane::chat{}};
      const bool beside = (chat.info_open || chat.threads_open) && chat.chosen.has_value();
      now.info_up = beside && !chat.threads_open;
      now.panel_up = beside && chat.threads_open;
      now.beside_edge = beside && !now.single;
      now.side_up = !now.single || !chat.chosen;
      now.side_edge = !now.single;
      now.chat_up = !now.single || (chat.chosen && !beside);
      now.selecting = s_->showing->look<mux::ui::selection_shown>()->count > 0;
      now.suggesting = s_->composer_list_up;
      now.pointing = s_->showing->look<mux::ui::list_shown>()->pointed.has_value();
      now.in_scope = chat.scope.has_value();
    });
  }
  template <class Change>
  void change_frame(Change change) {
    auto now = *s_->showing->look<mux::ui::screen_shown>();
    change(now);
    this->put(std::move(now));
  }
  template <class Change>
  void change(Change change) {
    auto list = *s_->showing->look<mux::ui::list_shown>();
    change(list);
    this->put(std::move(list));
  }
  template <class Part>
  void put(Part now) {
    if (*s_->showing->look<Part>() != now)
      mux::ui::show(*s_->showing, std::move(now));
  }

  services* s_;
  kept_settings* k_;
  const mux::ui::palette* colours_;
  std::optional<inputs> seen_;
  bool dragging_ = false;
  std::uint64_t revision_ = 0;
  std::vector<mux::conversation_id> order_;
  // The server search: what was asked, by which account, and what came.
  std::string asked_;
  std::optional<mux::account_id> asked_by_;
  std::vector<mux::directory_room> rooms_;
  std::vector<mux::found_person> people_;
  bool rooms_came_ = false, people_came_ = false;
};

}  // namespace mux::app
